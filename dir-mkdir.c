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
#include <unistd.h> /* getegid */

error_t
S_dir_mkdir (struct protid *pi, const char *name, mode_t mode)
{
  error_t err;
  struct p9_qid qid;
  uint32_t new_fid;
  uint16_t n_qids;
  uint32_t max_message_size;

  if (!pi)
    return EOPNOTSUPP;
  if (p9_readonly)
    return EROFS;

  if (p9_version >= P9_VERSION_2000_L)
    {
      /* TODO: obtain gid from pi iouser idvec */
      return p9_rpc (P9_MKDIR_REQUEST,
                     "4s44", pi->walk_fid, name, mode, getegid (),
                     "148", &qid.type, &qid.version, &qid.path);
    }

  /* Duplicate the fid.  */
  new_fid = p9_fid_alloc ();
  err = p9_rpc (P9_WALK_REQUEST,
                "442", pi->walk_fid, new_fid, 0,
                "2", &n_qids);
  if (err)
    return err;

  err = p9_rpc (P9_CREATE_REQUEST,
                "4s41", new_fid, name, mode | P9_MODE_DIR, 0,
                "1484", &qid.type, &qid.version, &qid.path,
                &max_message_size);
  p9_rpc (P9_CLUNK_REQUEST,
          "4", new_fid,
          "");

  return err;
}
