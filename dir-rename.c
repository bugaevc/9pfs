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
S_dir_rename (struct protid *olddir, const char *oldname,
              struct protid *newdir, const char *newname, int excl)
{
  error_t err;
  uint32_t fid;
  uint16_t n_qids;
  struct p9_qid *qids;
  struct p9_stat stat;

  if (!olddir)
    return EOPNOTSUPP;
  if (!newdir)
    return EXDEV;
  if (p9_readonly)
    return EROFS;

  if (p9_version >= P9_VERSION_2000_L)
    {
      err = p9_rpc (P9_RENAMEAT_REQUEST,
                    "4s4s", olddir->walk_fid, oldname,
                    newdir->walk_fid, newname,
                    "");
      if (err != EOPNOTSUPP)
        return err;

      fid = p9_fid_alloc ();
      err = p9_rpc (P9_WALK_REQUEST,
                    "442s", olddir->walk_fid, fid, 1, oldname,
                    "Q", &n_qids, &qids);
      if (err)
        return err;
      free (qids);

      err = p9_rpc (P9_RENAME_REQUEST,
                    "44s", fid, newdir->walk_fid, newname,
                    "");
      p9_rpc (P9_CLUNK_REQUEST,
              "4", fid,
              "");
      return err;
    }

  if (olddir != newdir)
    /* Renaming while changing containing directory is not supported
       by the core 9P2000 protocol.  */
    return EXDEV;

  p9_stat_dont_touch (&stat);
  free (stat.name);
  stat.name = (char *) newname;
  err = p9_wstat (olddir->walk_fid, &stat);
  /* Avoid freeing newname.  */
  stat.name = NULL;
  p9_stat_free (&stat);

  return err;
}
