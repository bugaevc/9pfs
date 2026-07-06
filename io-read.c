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
S_io_read (struct protid *protid,
           char **data,
           mach_msg_type_number_t *datalen,
           loff_t offset,
           vm_size_t amount)
{
  error_t err;
  struct peropen *po;
  struct node *np;
  loff_t start;
  uint32_t nread;
  int data_is_allocated = 0;

  if (protid == NULL)
    return EOPNOTSUPP;

  po = protid->po;
  np = po->np;

  if (!(po->user_open_flags & O_READ))
    return EBADF;

  /* Cap amount to the size we can fit in one message.  */
  if (amount > np->max_message_size - 24)
    amount = np->max_message_size - 24;

  if (amount > *datalen)
    {
      err = vm_allocate (mach_task_self (), (vm_address_t *) data,
                         amount, 1);
      if (err)
        amount = *datalen;
      else
        data_is_allocated = 1;
    }

  pthread_mutex_lock (&np->lock);

  /* TODO: If user_open_flags & O_NOLINK and we don't have
     an io_fid yet, ensure we have stat, and either parse symlink
     target from there, or use Treadlink.  */
  err = p9_ensure_open (protid, O_READ);
  if (err)
    goto out;

  /* Cap amount again.  */
  nread = amount;
  if (nread > np->max_message_size - 24)
    nread = np->max_message_size - 24;

  start = (offset == -1) ? po->offset : offset;

  err = p9_rpc (P9_READ_REQUEST,
                "484", protid->io_fid, (uint64_t) start, nread,
                "d", &nread, *data);
  if (err)
    goto out;

  *datalen = nread;
  if (offset == -1)
    po->offset += nread;

 out:
  pthread_mutex_unlock (&np->lock);

  if (data_is_allocated)
    {
      /* On error, deallocate the whole buffer.
         On success, deallocate the unused part.  */
      if (err)
        vm_deallocate (mach_task_self (), (vm_address_t) *data, amount);
      else if (round_page (nread) < round_page (amount))
        vm_deallocate (mach_task_self (),
                       (vm_address_t) *data + round_page (nread),
                       round_page (amount) - round_page (nread));
    }

  return err;
}
