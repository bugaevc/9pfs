#define _GNU_SOURCE 1

#include <errno.h>
#include <stdint.h>

error_t p9_error_to_err (const char *error_str);
error_t p9_lerror_to_err (uint32_t lerr);
