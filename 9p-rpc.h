/* 9P translator

   Copyright (C) 2021 Sergey Bugaev <bugaevc@gmail.com>

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>. */

/* Proudly written in GNU nano. */

#define _GNU_SOURCE 1

#include "9p.h"
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <error.h>
#include <hurd/ihash.h>

#define P9_STAT_BASIC "2241484448ssss"

/* Describes a thread interested in receiving an expected response
   with the given tag.  Once this response is received, also contains
   its header.  */
struct p9_response_interest
{
  pthread_cond_t cond;  /* the thread waits on this */
  uint16_t tag;  /* this tag is expected / was received */
  uint32_t size;  /* this size was received, or 0 while not received */
  unsigned char type; /* this type was received */
  hurd_ihash_locp_t locp;
};

/* The characters supported inside FORMAT are:

   1 -- 1-byte integer
   2 -- 2-byte integer
   4 -- 4-byte integer
   8 -- 8-byte integer
   d -- a blob of data
   s -- a string
   S -- an array of strings (send only)
   Q -- an array of qids (recv only)
   ? -- the rest is optional (recv only)

   The integers are passed by value on send, and by out-pointer on recv.
   A string is passed as (const char *) on send, and (const char **) on
   recv, into which the string buffer is malloced.  Data blobs are passed
   as a uint32_t size + buffer ptr on send and uint32_t *size + buffer ptr
   on recv; it's an error if more data is received than *size, and *size is
   updated to reflect the amount actually received.  An array of strings is
   passed in as (const char * const *) and should be null-terminated; the
   strings themselves can be either null-terminated or '/'-terminated.  An
   array of qids is passed in as uint16_t *count, struct p9_qid **qbuf, and
   malloced on return.  The '?' must correspond to an (int *) that will be
   set to 1 if more data was available in the message, or to 0 otherwise.
   */

uint32_t p9_serialized_size (const char *format, ...);

/* Register the interest in response with the message dispatch machinery.
   Do this before sending the request, otherwise it'll be racing against
   receiving the response and dispatching it.  */
error_t p9_register_interest (struct p9_response_interest *interest);

error_t p9_send (enum p9_message_type type,
                 uint16_t tag, const char *fmt, ...);

error_t p9_recv (struct p9_response_interest *interest,
                 enum p9_message_type request_type,
                 const char *fmt, ...);

/* Register interest, send, receive.  */
error_t p9_rpc (enum p9_message_type request_type,
                const char *request_format, ...);
