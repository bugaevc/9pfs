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
S_dir_rmdir (struct protid *pi, const char *name)
{
  error_t err;
  uint32_t new_fid;
  uint16_t n_qids;
  struct p9_qid *qids;
  const char *parts[2];

  if (!pi)
    return EOPNOTSUPP;
  if (p9_readonly)
    return EROFS;

  if (p9_version >= P9_VERSION_2000_L)
    {
      /* Attempt unlinkat with AT_REMOVEDIR.  */
      err = p9_rpc (P9_UNLINKAT_REQUEST,
                    "4s4", pi->walk_fid, name, 0x200,
                    "");
      if (err != EOPNOTSUPP)
        return err;
    }

  new_fid = p9_fid_alloc ();
  parts[0] = name;
  parts[1] = NULL;
  err = p9_rpc (P9_WALK_REQUEST,
                "442S", pi->walk_fid, new_fid, 1, parts,
                "Q", &n_qids, &qids);
  if (err)
    return err;

  free (qids);

  err = p9_rpc (P9_REMOVE_REQUEST,
                "4", new_fid,
                "");
  /* Note: the remove request clunks the fid in either case.  */
  return err;
}
