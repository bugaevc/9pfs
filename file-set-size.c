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
#include "fs_S.h"
#include <errno.h>

error_t
S_file_set_size (struct protid *pi, loff_t new_size)
{
  error_t err;
  struct p9_stat s;

  if (!pi)
    return EOPNOTSUPP;
  if (new_size < 0)
    return EINVAL;
  if (!(pi->po->user_open_flags & O_WRITE))
    return EBADF;
  /* Should not really happen due to the above check.  */
  if (p9_readonly)
    return EROFS;

  pi->po->np->last_stat = 0;

  if (p9_version >= P9_VERSION_2000_L)
    return p9_rpc (P9_SETATTR_REQUEST,
                   "4444488888", pi->walk_fid, P9_SETATTR_MASK_SIZE,
                   0, 0, 0, new_size, 0, 0, 0, 0,
                   "");

  p9_stat_dont_touch (&s);
  s.length = new_size;
  err = p9_wstat (pi->walk_fid, &s);
  p9_stat_free (&s);

  return err;
}
