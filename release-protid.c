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

#include "9pfs.h"
#include "9p-rpc.h"
#include <hurd/iohelp.h>

void
p9_release_protid (void *arg)
{
  error_t err;
  struct protid *pi = arg;

  if (pi->walk_fid != P9_NO_FID)
    {
      err = p9_rpc (P9_CLUNK_REQUEST,
                    "4", pi->walk_fid, "");
      if (err)
        error (0, err, "Failed to clunk walk fid %d", pi->walk_fid);
    }

  if (pi->io_fid != P9_NO_FID)
    {
      err = p9_rpc (P9_CLUNK_REQUEST,
                    "4", pi->io_fid, "");
      if (err)
        error (0, err, "Failed to clunk I/O fid %d", pi->io_fid);
    }

  if (pi->dir.buffer.base != NULL)
    {
      err = vm_deallocate (mach_task_self (),
                           (vm_address_t) pi->dir.buffer.base,
                           pi->dir.buffer.allocated_size);
      assert_perror_backtrace (err);
    }

  iohelp_free_iouser (pi->user);
  p9_release_peropen (pi->po);
}
