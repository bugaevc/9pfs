#define _GNU_SOURCE 1

#include "9p.h"
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <error.h>
#include <hurd/ihash.h>

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
