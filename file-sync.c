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
S_file_sync (struct protid *pi, int wait, int omit_metadata)
{
  error_t err;
  struct p9_stat s;

  if (!pi)
    return EOPNOTSUPP;
  if (p9_readonly)
    /* Should we return EROFS?  */
    return 0;


  if (p9_version >= P9_VERSION_2000_L)
    {
      /* Ensure we're open at least for something.  */
      if (!pi->server_open_flags)
        {
          err = p9_ensure_open (pi, O_READ);
          if (err)
            return err;
        }
      return p9_rpc (P9_FSYNC_REQUEST,
                     "44", pi->io_fid, omit_metadata,
                     "");
    }

  /* Use the special case of wstat.  */
  p9_stat_dont_touch (&s);
  err = p9_wstat (pi->walk_fid, &s);
  p9_stat_free (&s);
  return err;
}
