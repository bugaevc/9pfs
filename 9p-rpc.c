#define _GNU_SOURCE 1

#include "9p-rpc.h"
#include "9p-err.h"
#include "9pfs.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <pthread.h>
#include <error.h>
#include <assert-backtrace.h>
#include <hurd/ihash.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct hurd_ihash interest_table
  = HURD_IHASH_INITIALIZER (offsetof (struct p9_response_interest, locp));
static int some_thread_is_reading = 0;
/* TODO: Use a more sophisticated free tag tracking mechanism.  */
static uint16_t next_tag = ~0;

static pthread_mutex_t write_mutex = PTHREAD_MUTEX_INITIALIZER;

error_t
p9_register_interest (struct p9_response_interest *interest)
{
  error_t err;

  pthread_mutex_lock (&mutex);
  interest->tag = next_tag++;
  err = hurd_ihash_add (&interest_table, interest->tag, interest);
  pthread_mutex_unlock (&mutex);

  return err;
}

static error_t
wait_for_header (struct p9_response_interest *interest)
{
  error_t err;
  struct p9_response_interest *other_interest;
  size_t nread;

  uint32_t size;
  unsigned char type;
  uint16_t tag;

  pthread_mutex_lock (&mutex);

 again:
  if (interest->size)
    {
      /* Somebody has read our message header; now it's up to us
         to read the body of the message.  The some_thread_is_reading
         token has been passed to us by whoever has read our header.  */
      assert_backtrace (some_thread_is_reading);
 read_our_header:
      hurd_ihash_locp_remove (&interest_table, interest->locp);
      pthread_mutex_unlock (&mutex);
      return 0;
    }

  if (some_thread_is_reading)
    {
      /* Wait for that thread to wake us up.  */
      err = pthread_hurd_cond_wait_np (&interest->cond, &mutex);
      if (err)
        {
          pthread_mutex_unlock (&mutex);
          // TODO: remove from ihash?
          return err;
        }
      goto again;
    }

  /* Noone else is reading, so we'll do it.  */
  some_thread_is_reading = 1;
  pthread_mutex_unlock (&mutex);

  /* Read a message header, converting it from
     little-endian to native endianness. */
  nread = fread (&size, sizeof (size), 1, p9_connection);
  if (nread != 1)
    goto out;
  size = le32toh (size);

  nread = fread (&type, sizeof (type), 1, p9_connection);
  if (nread != 1)
    goto out;

  nread = fread (&tag, sizeof (tag), 1, p9_connection);
  if (nread != 1)
    goto out;
  tag = le16toh (tag);

  pthread_mutex_lock (&mutex);

  /* See if we read the message we wanted.  */
  if (tag == interest->tag)
    {
      interest->size = size;
      interest->type = type;
      goto read_our_header;
    }

  other_interest = hurd_ihash_find (&interest_table, tag);
  if (!other_interest)
    {
      // TODO: unexpected message, but we still have to read it.
      assert_backtrace (0);
    }
  /* Fill in the other interest with the header we've read.  */
  assert_backtrace (other_interest->tag == tag);
  other_interest->size = size;
  other_interest->type = type;
  /* Wake it up, and wait for it to figure everything out.
     We pass it the some_thread_is_reading token.  */
  pthread_cond_signal (&other_interest->cond);
  goto again;

 out:
  pthread_mutex_lock (&mutex);
  some_thread_is_reading = 0;
  pthread_mutex_unlock (&mutex);
  return ferror (p9_connection) ?: EIO;
}


static uint32_t
p9_vserialized_size (const char *format, va_list ap)
{
  uint32_t total_size = 0;
  const char *f, *ptr;
  uint32_t size;

  for (f = format; *f; f++)
    switch (*f)
      {
      case '1':
        va_arg (ap, unsigned);
        total_size += 1;
        break;

      case '2':
        va_arg (ap, unsigned);
        total_size += 2;
        break;

      case '4':
        va_arg (ap, uint32_t);
        total_size += 4;
        break;

      case '8':
        va_arg (ap, uint64_t);
        total_size += 8;
        break;

      case 'd':
        size = va_arg (ap, uint32_t);
        total_size += 4;
        va_arg (ap, const char *);
        total_size += size;
        break;

      case 's':
        ptr = va_arg (ap, const char *);
        total_size += 2;
        total_size += strlen (ptr);
        break;

      default:
        assert_backtrace (!"unimplemented format specifier");
      }

  return total_size;
}

uint32_t
p9_serialized_size (const char *format, ...)
{
  va_list ap;
  uint32_t size;

  va_start (ap, format);
  size = p9_vserialized_size (format, ap);
  va_end (ap);

  return size;
}

static void
p9_do_vsend (const char *format, va_list *ap)
{
  const char *f, *ptr;
  unsigned char n1;
  uint16_t n2;
  uint32_t n4;
  uint64_t n8;
  const struct p9_qid qid, *qptr;
  size_t size, nwritten;

  for (f = format; *f; f++)
    switch (*f)
      {
      case '1':
        n1 = va_arg (*ap, unsigned);
        if (p9_dump_exchanges)
          fprintf (stderr, " %" PRIu8, n1);
        nwritten = fwrite (&n1, 1, 1, p9_connection);
        if (nwritten != 1)
          return;
        break;

      case '2':
        n2 = va_arg (*ap, unsigned);
        if (p9_dump_exchanges)
          fprintf (stderr, " %" PRIu16, n2);
        n2 = htole16 (n2);
        nwritten = fwrite (&n2, 2, 1, p9_connection);
        if (nwritten != 1)
          return;
        break;

      case '4':
        n4 = va_arg (*ap, uint32_t);
        if (p9_dump_exchanges)
          fprintf (stderr, " %" PRIu32, n4);
        n4 = htole32 (n4);
        nwritten = fwrite (&n4, 4, 1, p9_connection);
        if (nwritten != 1)
          return;
        break;

      case '8':
        n8 = va_arg (*ap, uint64_t);
        if (p9_dump_exchanges)
          fprintf (stderr, " %" PRIu64, n8);
        n8 = htole64 (n8);
        nwritten = fwrite (&n8, 8, 1, p9_connection);
        if (nwritten != 1)
          return;
        break;

      case 'd':
        size = va_arg (*ap, uint32_t);
        ptr = va_arg (*ap, const char *);
        if (p9_dump_exchanges)
          fprintf (stderr, " %" PRIu32 " <data>", size);
        n4 = htole32 (size);
        nwritten = fwrite (&n4, 4, 1, p9_connection);
        if (nwritten != 1)
          return;
        nwritten = fwrite (ptr, 1, size, p9_connection);
        if (nwritten != size)
          return;
        break;

      case 's':
        ptr = va_arg (*ap, const char *);
        size = strlen (ptr);
        assert_backtrace (size <= UINT16_MAX);
        if (p9_dump_exchanges)
          fprintf (stderr, " %" PRIu32 " \"%s\"", size, ptr);
        n2 = htole16 (size);
        nwritten = fwrite (&n2, 2, 1, p9_connection);
        if (nwritten != 1)
          return;
        nwritten = fwrite (ptr, 1, size, p9_connection);
        if (nwritten != size)
          return;
        break;

      default:
        assert_backtrace (!"unimplemented format specifier");
      }
}

static error_t
p9_vsend (unsigned char type, uint16_t tag,
          const char *format, va_list *ap)
{
  error_t err;
  uint32_t size, nwritten;
  va_list aq;

  /* Count the number of bytes we will need.  */
  va_copy (aq, *ap);
  size = 4 + 1 + 2 + p9_vserialized_size (format, aq);
  va_end (aq);

  if (p9_dump_exchanges)
    fprintf (stderr, "-> %" PRIu32 " %" PRIu8 " %" PRIu16,
             size, type, tag);

  size = htole32 (size);
  tag = htole16 (tag);

  pthread_mutex_lock (&write_mutex);

  /* Write out the header.  */
  nwritten = fwrite (&size, sizeof (size), 1, p9_connection);
  if (nwritten != 1)
    goto out;

  nwritten = fwrite (&type, 1, 1, p9_connection);
  if (nwritten != 1)
    goto out;

  nwritten = fwrite (&tag, sizeof (tag), 1, p9_connection);
  if (nwritten != 1)
    goto out;

  /* Write out the body.  */
  p9_do_vsend (format, ap);

out:
  pthread_mutex_unlock (&write_mutex);
  err = ferror (p9_connection);
  if (!err)
    fflush (p9_connection);

  if (p9_dump_exchanges)
    fprintf (stderr, "\n");

  return err;
}

error_t
p9_send (enum p9_message_type type,
         uint16_t tag, const char *fmt, ...)
{
  error_t err;
  va_list ap;
  va_start (ap, fmt);

  err = p9_vsend (type, tag, fmt, &ap);

  va_end (ap);

  return err;
}

static error_t
p9_do_recv (struct p9_response_interest *interest,
            enum p9_message_type request_type,
            const char *fmt, ...);

static error_t
p9_do_vrecv (struct p9_response_interest *interest,
             enum p9_message_type request_type,
             const char *format, va_list ap)
{
  error_t err = 0;
  const char *f;
  char *ptr, **dptr;
  uint32_t bytes_read = 4 + 1 + 2;
  size_t nread;
  uint32_t size;
  uint16_t string_size;

  unsigned char *n1p;
  uint16_t *n2p;
  uint32_t *n4p;
  uint64_t *n8p;
  int *intp;
  struct p9_qid qid, *qptr;

  if (interest->type == request_type + 1)
    {
      /* All good.  */
    }
  else if (interest->type == P9_LERROR_RESPONSE)
    {
      uint32_t p9_2000l_error;
      err = p9_do_recv (interest, P9_LERROR_REQUEST,
                        "4", &p9_2000l_error);
      if (err)
        return err;
      return p9_lerror_to_err (p9_2000l_error);
    }
  else if (interest->type == P9_ERROR_RESPONSE)
    {
      char *error_string = NULL;
      uint32_t unix_errno;

      if (p9_version >= P9_VERSION_2000_U)
        err = p9_do_recv (interest, P9_ERROR_REQUEST,
                          "s4", &error_string, &unix_errno);
      else
        err = p9_do_recv (interest, P9_ERROR_REQUEST,
                          "s", &error_string);
      if (err)
        return err;
      err = p9_error_to_err (error_string);
      free (error_string);
      return err;
    }
  else
    {
      /* Other than in error replies,
         we only expect matching types.  */
      err = EIEIO;
      goto out;
    }

  if (p9_dump_exchanges)
    fprintf (stderr, "<- %" PRIu32 " %" PRIu8 " %" PRIu16,
             interest->size, interest->type, interest->tag);

  for (f = format; *f; f++)
    {
      if (*f != '?' && bytes_read >= interest->size)
        {
          error (0, 0, "Already read %" PRIu32 " bytes of %" PRIu32
                 " bytes in a message of type %" PRIu8 ", and still"
                 " expecting more", bytes_read, interest->size,
                 interest->type);
          err = EIO;
          goto out;
        }
      switch (*f)
        {
        case '?':
          intp = va_arg (ap, int *);
          /* Do we have more data in the message?  */
          *intp = bytes_read < interest->size;
          if (!*intp)
            goto out;
          break;

        case '1':
          n1p = va_arg (ap, unsigned char *);
          nread = fread (n1p, 1, 1, p9_connection);
          if (nread != 1)
            goto out;
          bytes_read++;
          if (p9_dump_exchanges)
            fprintf (stderr, " %" PRIu8, *n1p);
          break;

        case '2':
          n2p = va_arg (ap, uint16_t *);
          nread = fread (n2p, 2, 1, p9_connection);
          if (nread != 1)
            goto out;
          bytes_read += 2;
          *n2p = le16toh (*n2p);
          if (p9_dump_exchanges)
            fprintf (stderr, " %" PRIu16, *n2p);
          break;

        case '4':
          n4p = va_arg (ap, uint32_t *);
          nread = fread (n4p, 4, 1, p9_connection);
          if (nread != 1)
            goto out;
          bytes_read += 4;
          *n4p = le32toh (*n4p);
          if (p9_dump_exchanges)
            fprintf (stderr, " %" PRIu32, *n4p);
          break;

        case '8':
          n8p = va_arg (ap, uint64_t *);
          nread = fread (n8p, 8, 1, p9_connection);
          if (nread != 1)
            goto out;
          bytes_read += 8;
          *n8p = le64toh (*n8p);
          if (p9_dump_exchanges)
            fprintf (stderr, " %" PRIu64, *n8p);
          break;

        case 'd':
          n4p = va_arg (ap, uint32_t *);
          size = *n4p;
          nread = fread (n4p, 4, 1, p9_connection);
          if (nread != 1)
            goto out;
          bytes_read += 4;
          *n4p = le32toh (*n4p);
          if (p9_dump_exchanges)
            fprintf (stderr, " %" PRIu32 " <data>", *n4p);
          if (*n4p > size)
            {
              err = EOVERFLOW;
              goto out;
            }
          ptr = va_arg (ap, char *);
          nread = fread (ptr, 1, *n4p, p9_connection);
          if (nread != *n4p)
            goto out;
          bytes_read += *n4p;
          break;

        case 's':
          dptr = va_arg (ap, char **);
          nread = fread (&string_size, 2, 1, p9_connection);
          if (nread != 1)
            goto out;
          bytes_read += 2;
          string_size = le16toh (string_size);
          *dptr = malloc (string_size + 1);
          if (!*dptr)
            {
              err = errno;
              goto out;
            }
          nread = fread (*dptr, 1, string_size, p9_connection);
          if (nread != string_size)
            goto out;
          bytes_read += string_size;
          (*dptr)[string_size] = 0;
          if (p9_dump_exchanges)
            fprintf (stderr, " %" PRIu16 " \"%s\"",
                     string_size, *dptr);
          break;

        default:
          assert_backtrace (!"unimplemented format specifier");
        }
    }

out:
  if (!ferror (p9_connection) && bytes_read < interest->size)
    {
      /* Hmm, we've read less than the whole message; weird.
         Read the rest of bytes and move on to the next message.  */
      char buffer[interest->size - bytes_read];
      error (0, 0, "When reading message of type %" PRIu8 ", read only %"
             PRIu32 " of %" PRIu32 " bytes in the message",
             interest->type, bytes_read, interest->size);
      fread (buffer, 1, interest->size - bytes_read, p9_connection);
    }

  if (p9_dump_exchanges)
    fprintf (stderr, "\n");

  pthread_mutex_lock (&mutex);
  some_thread_is_reading = 0;
  /* If there are threads waiting, wake somebody up.  */
  HURD_IHASH_ITERATE (&interest_table, value)
    {
      struct p9_response_interest *interest = value;
      pthread_cond_signal (&interest->cond);
      break;
    }
  pthread_mutex_unlock (&mutex);

  if (err)
    return err;
  return ferror (p9_connection);
}

static error_t
p9_do_recv (struct p9_response_interest *interest,
            enum p9_message_type request_type,
            const char *fmt, ...)
{
  error_t err;
  va_list ap;

  va_start (ap, fmt);
  err = p9_do_vrecv (interest, request_type, fmt, ap);
  va_end (ap);

  return err;
}

static error_t
p9_vrecv (struct p9_response_interest *interest,
          enum p9_message_type request_type,
          const char *fmt, va_list ap)
{
  error_t err;

  err = wait_for_header (interest);
  if (err)
    return err;

  /* We own the some_thread_is_reading token now.  */

  return p9_do_vrecv (interest, request_type, fmt, ap);
}

error_t
p9_recv (struct p9_response_interest *interest,
          enum p9_message_type request_type,
          const char *fmt, ...)
{
  error_t err;
  va_list ap;

  va_start (ap, fmt);
  err = p9_vrecv (interest, request_type, fmt, ap);
  va_end (ap);

  return err;
}

error_t
p9_rpc (enum p9_message_type request_type,
        const char *request_format, ...)
{
  error_t err;
  va_list ap;
  const char *response_format;
  struct p9_response_interest interest = { 0 };

  pthread_cond_init (&interest.cond, NULL);

  err = p9_register_interest (&interest);
  if (err)
    return err;

  va_start (ap, request_format);

  err = p9_vsend (request_type, interest.tag,
                  request_format, &ap);
  if (err)
    goto out;

  response_format = va_arg (ap, const char *);

  err = p9_vrecv (&interest, request_type,
                  response_format, ap);
  pthread_cond_destroy (&interest.cond);
 out:
  va_end (ap);
  return err;
}
