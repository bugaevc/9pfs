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

#define _GNU_SOURCE 1

#include "9pfs.h"
#include "9p-rpc.h"
#include "fs_S.h"
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <grp.h>

static char *
lookup_group (gid_t gid)
{
  int rc;
  unsigned char *buf;
  size_t bufsize;
  struct group grp, *pgrp;
  char *name;

  bufsize = sysconf (_SC_GETGR_R_SIZE_MAX);
  if (bufsize == -1)
    bufsize = 16384;

 again:
  buf = malloc (bufsize);
  if (buf == NULL)
    return NULL;

  errno = 0;
  rc = getgrgid_r (gid, &grp, buf, bufsize, &pgrp);
  if (rc != 0 && errno == ERANGE)
    {
      free (buf);
      bufsize *= 2;
      goto again;
    }

  if (rc != 0 || pgrp == NULL)
    {
      free (buf);
      errno = EINVAL;
      return NULL;
    }

  name = strdup (pgrp->gr_name);
  free (buf);
  return name;
}

error_t
S_file_chown (struct protid *pi, uid_t new_owner, gid_t new_group)
{
  error_t err;
  char *group_name;
  struct p9_stat s;

  if (!pi)
    return EOPNOTSUPP;
  if (p9_readonly)
    return EROFS;

  pi->po->np->last_stat = 0;

  if (p9_version >= P9_VERSION_2000_L)
    {
      uint32_t mask = 0;

      if (new_owner != -1)
        mask |= P9_SETATTR_MASK_UID;
      if (new_group != -1)
        mask |= P9_SETATTR_MASK_GID;

      return p9_rpc (P9_SETATTR_REQUEST,
                     "4444488888", pi->walk_fid, mask,
                     0, new_owner, new_group, 0, 0, 0, 0, 0,
                     "");
    }

  /* Core protocol disallows chaning file owner */
  if (new_owner != -1)
    return EOPNOTSUPP;
  if (new_group == -1)
    return 0;

  group_name = lookup_group (new_group);
  if (!group_name)
    {
      if (p9_version < P9_VERSION_2000_U)
        return EINVAL;
      group_name = strdup ("");
    }

  p9_stat_dont_touch (&s);
  free (s.gid);
  s.gid = group_name;
  s.n_gid = new_group;
  err = p9_wstat (pi->walk_fid, &s);
  p9_stat_free (&s);

  return err;
}
