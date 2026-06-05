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

#include "9p-err.h"
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Try to convert error strings to Unix error codes.
   These patterns taken from src/cmd/9pfuse/errstr.c
   in Plan 9 from User Space.  */

struct p9_error_pattern
{
  const char *pattern;
  error_t err;
};

static const struct p9_error_pattern error_patterns[] =
{
  {"permitted", EPERM},
  {"permission", EACCES},
  {"access", EACCES},
  {"exists", EEXIST},
  {"exist", ENOENT},
  {"no such", ENOENT},
  {"not found", ENOENT},
  {"not implemented", ENOSYS},
  {"input/output", EIO},
  {"timeout", ETIMEDOUT},
  {"timed out", ETIMEDOUT},
  {"i/o", EIO},
  {"too long", E2BIG},
  {"interrupt", EINTR},
  {"no such", ENODEV},
  {"bad file", EBADF},
  {" fid ", EBADF},
  {"temporar", EAGAIN},
  {"memory", ENOMEM},
  {"is a directory", EISDIR},
  {"directory", ENOTDIR},
  {"argument", EINVAL},
  {"pipe", EPIPE},
  {"in use", EBUSY},
  {"busy", EBUSY},
  {"illegal", EINVAL},
  {"invalid", EINVAL},
  {"read-only", EROFS},
  {"read only", EROFS},
  {"stale ", ESTALE},
  {"proto", EPROTO},
  {"entry", ENOENT},
};

error_t
p9_error_to_err (const char *error_str)
{
  size_t i;

  for (i = 0; i < sizeof (error_patterns) / sizeof (error_patterns[0]); i++)
    if (strcasestr (error_str, error_patterns[i].pattern) != NULL)
      return error_patterns[i].err;

  return EIO;
}

/* In 9P2000.L, the Rlerror message carries numerical Linux errno.
   Try to convert it to our error values.  */

struct p9_lerror
{
  uint32_t lerr;
  error_t err;
};

static const struct p9_lerror lerrors[] =
{
  {1, EPERM},
  {2, ENOENT},
  {3, ESRCH},
  {4, EINTR},
  {5, EIO},
  {6, ENXIO},
  {7, E2BIG},
  {8, ENOEXEC},
  {9, EBADF},
  {10, ECHILD},
  {11, EAGAIN},
  {12, ENOMEM},
  {13, EACCES},
  {14, EFAULT},
  {15, ENOTBLK},
  {16, EBUSY},
  {17, EEXIST},
  {18, EXDEV},
  {19, ENODEV},
  {20, ENOTDIR},
  {21, EISDIR},
  {22, EINVAL},
  {23, ENFILE},
  {24, EMFILE},
  {25, ENOTTY},
  {26, ETXTBSY},
  {27, EFBIG},
  {28, ENOSPC},
  {29, ESPIPE},
  {30, EROFS},
  {31, EMLINK},
  {32, EPIPE},
  {33, EDOM},
  {34, ERANGE},
  {35, EDEADLK},
  {36, ENAMETOOLONG},
  {37, ENOLCK},
  {38, ENOSYS},
  {39, ENOTEMPTY},
  {40, ELOOP},
  {41, EWOULDBLOCK},
  {95, EOPNOTSUPP},
};

error_t
p9_lerror_to_err (uint32_t lerr)
{
  size_t i;

  for (i = 0; i < sizeof (lerrors) / sizeof (lerrors[0]); i++)
    if (lerr == lerrors[i].lerr)
      return lerrors[i].err;

  return EIO;
}
