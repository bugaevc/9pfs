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
#include <hurd/pager.h>
#include <string.h>

error_t
pager_read_page (struct user_pager_info *upi,
                 vm_offset_t page,
                 vm_address_t *buf,
                 int *write_lock)
{
  error_t err, err2;
  struct node *np = (struct node *) upi;
  uint32_t size;

  if (np->pager_fid == P9_NO_FID)
    return EGRATUITOUS;

  size = vm_page_size;
  *buf = 0;
  err = vm_allocate (mach_task_self (), buf, size, TRUE);
  if (err)
    return err;

  /* TODO: cap the message size, retry short reads in a loop */
  err = p9_rpc (P9_READ_REQUEST,
                "484", np->pager_fid, (uint64_t) page, size,
                "d", &size, *buf);
  if (err)
    {
      err2 = vm_deallocate (mach_task_self (), *buf, vm_page_size);
      assert_perror_backtrace (err2);
      return err;
    }
  memset ((char *) *buf + size, 0, vm_page_size - size);
  /* TODO: track whether we can write to the file */
  *write_lock = 1;
  return 0;
}

error_t
pager_write_page (struct user_pager_info *upi,
                  vm_offset_t page,
                  vm_address_t buf)
{
  return EOPNOTSUPP;
}

error_t
pager_write_pages (struct user_pager_info *upi,
                   vm_offset_t offset,
                   vm_address_t data,
                   vm_size_t length,
                   vm_size_t *written)
{
  return EOPNOTSUPP;
}

error_t
pager_unlock_page (struct user_pager_info *upi,
                   vm_offset_t page)
{
  return EOPNOTSUPP;
}

void
pager_notify_evict (struct user_pager_info *upi,
                    vm_offset_t page)
{
  assert_backtrace (!"unrequested notifications");
}

error_t
pager_report_extent (struct user_pager_info *upi,
                     vm_address_t *offset,
                     vm_size_t *size)
{
  /* TODO: get size */
  *offset = 0;
  *size = SIZE_MAX;
  return 0;
}

void
pager_clear_user_data (struct user_pager_info *upi)
{
}

void
pager_dropweak (struct user_pager_info *upi)
{
  struct node *np = (struct node *) upi;
  struct pager *pager;

  pthread_mutex_lock (&np->lock);
  pager = np->pager;
  assert_backtrace (pager_get_upi (pager) == upi);

  np->pager = NULL;
  ports_port_deref_weak (pager);
  p9_nput (np);
}
