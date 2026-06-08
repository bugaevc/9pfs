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
S_file_utimes (struct protid *pi, time_value_t new_atime,
               time_value_t new_mtime)
{
  error_t err;
  struct p9_stat s;

  if (!pi)
    return EOPNOTSUPP;
  if (p9_readonly)
    return EROFS;

  pi->po->np->last_stat = 0;

  if (p9_version >= P9_VERSION_2000_L)
    {
      enum p9_setattr_mask mask = 0;

      if (new_atime.microseconds == -2)
        ; /* omit */
      else if (new_atime.microseconds == -1)
        mask |= P9_SETATTR_MASK_ATIME;
      else
        mask |= P9_SETATTR_MASK_ATIME | P9_SETATTR_MASK_ATIME_SET;

      if (new_mtime.microseconds == -2)
        ; /* omit */
      else if (new_mtime.microseconds == -1)
        mask |= P9_SETATTR_MASK_MTIME;
      else
        mask |= P9_SETATTR_MASK_MTIME | P9_SETATTR_MASK_MTIME_SET;

      return p9_rpc (P9_SETATTR_REQUEST,
                     "4444488888", pi->walk_fid, mask, 0, 0, 0, 0,
                     new_atime.seconds, new_atime.microseconds * 1000,
                     new_mtime.seconds, new_mtime.microseconds * 1000,
                     "");
    }

  p9_stat_dont_touch (&s);

  if (new_atime.microseconds == -2)
    ; /* omit */
  else if (new_atime.microseconds == -1)
    s.atime = time (NULL);
  else
    s.atime = new_atime.seconds;

  if (new_mtime.microseconds == -2)
    ; /* omit */
  else if (new_mtime.microseconds == -1)
    s.mtime = time (NULL);
  else
    s.mtime = new_mtime.seconds;

  err = p9_wstat (pi->walk_fid, &s);
  p9_stat_free (&s);

  return err;
}
