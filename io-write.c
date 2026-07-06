/* 9P translator

   Copyright (C) 2021-2026 Sergey Bugaev <bugaevc@gmail.com>

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

#include "9pfs.h"
#include "9p-rpc.h"
#include "io_S.h"
#include <fcntl.h>
#include <errno.h>
#include <mach.h>

error_t
S_io_write (struct protid *protid,
            const char *data,
            mach_msg_type_number_t datalen,
            loff_t offset,
            vm_size_t *amount)
{
  error_t err;
  struct peropen *po;
  struct node *np;
  uint64_t start;
  uint32_t nwritten;

  if (protid == NULL)
    return EOPNOTSUPP;

  po = protid->po;
  np = po->np;

  if (!(po->user_open_flags & O_WRITE))
    return EBADF;
  else if (p9_readonly)
    return EROFS;

  pthread_mutex_lock (&np->lock);

  err = p9_ensure_open (protid, O_WRITE);
  if (err)
    goto out;

  /* Cap amount to the size we can fit in one message.  */
  if (datalen > np->max_message_size - 24)
    datalen = np->max_message_size - 24;

  /* FIXME: Respect O_APPEND.  In 9P2000.L, perhaps we can use
     the native O_APPEND flags when opening?  */
  start = (offset == -1) ? po->offset : offset;

  /* Preemptively invalidate the stat cache.  */
  np->last_stat = 0;

  err = p9_rpc (P9_WRITE_REQUEST,
                "48d", protid->io_fid, start, datalen, data,
                "4", &nwritten);
  if (err)
    goto out;
  if (nwritten > datalen)
    return EIO;

  *amount = nwritten;
  if (offset != -1)
    po->offset += nwritten;

 out:
  pthread_mutex_unlock (&np->lock);

  return err;
}
