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
#include <errno.h>
#include <error.h>
#include <string.h>
#include <unistd.h>

static inline uint64_t
div_round_up (uint64_t a, uint64_t b)
{
  uint64_t c = a / b;
  return (a % b) ? c + 1 : c;
}

error_t
S_io_stat (struct protid *pi, io_statbuf_t *statbuf)
{
  error_t err;
  struct node *np;

  if (!pi)
    return EOPNOTSUPP;

  np = pi->po->np;

  /* Cache stats for up to 5 seconds.  */
  if (time (NULL) <= np->last_stat + 5)
    {
      *statbuf = np->stat;
      return 0;
    }

  np->stat.st_fsid = getpid ();
  np->stat.st_fstype = FSTYPE_MISC;

  if (p9_version >= P9_VERSION_2000_L)
    {
      /* In 9P2000.L, there's a dedicated getattr request
         which returns Unix stat info.  */
      uint64_t valid_mask;
      uint32_t mode, uid, gid;
      uint64_t nlink, rdev, size, blksize, blocks;
      uint64_t atime_sec, atime_nsec, mtime_sec, mtime_nsec;
      uint64_t ctime_sec, ctime_nsec, btime_sec, btime_nsec;
      uint64_t gen, data_version;

      err = p9_rpc (P9_GETATTR_REQUEST,
                    "48", pi->walk_fid, (uint64_t) P9_GETATTR_MASK_ALL_DEFINED,
                    "8148444888888888888888", &valid_mask,
                    &np->qid.type, &np->qid.version, &np->qid.path,
                    &mode, &uid, &gid, &nlink, &rdev, &size, &blksize, &blocks,
                    &atime_sec, &atime_nsec, &mtime_sec, &mtime_nsec,
                    &ctime_sec, &ctime_nsec, &btime_sec, &btime_nsec,
                    &gen, &data_version);
      if (err)
        return err;

      if (valid_mask & P9_GETATTR_MASK_INO)
        np->stat.st_ino = np->qid.path;

      if (valid_mask & P9_GETATTR_MASK_MODE)
        np->stat.st_mode = mode & (p9_mode_mask | ~0777);
      else
        np->stat.st_mode = p9_mode_mask;

      if (valid_mask & P9_GETATTR_MASK_NLINK)
        np->stat.st_nlink = nlink;
      else
        np->stat.st_nlink = 1;

      if (valid_mask & P9_GETATTR_MASK_UID)
        np->stat.st_uid = p9_uid_to_uid (NULL, uid);
      else
        np->stat.st_uid = p9_nobody_uid;

      if (valid_mask & P9_GETATTR_MASK_GID)
        np->stat.st_gid = p9_gid_to_gid (NULL, gid);
      else
        np->stat.st_gid = p9_nobody_gid;

      if (valid_mask & P9_GETATTR_MASK_RDEV)
        np->stat.st_rdev = rdev;
      else
        np->stat.st_rdev = 0;

      if (valid_mask & P9_GETATTR_MASK_SIZE)
        np->stat.st_size = size;
      else
        np->stat.st_size = 0;

      if (valid_mask & P9_GETATTR_MASK_BLOCKS)
        np->stat.st_blksize = blksize;
      else
        np->stat.st_blksize = 512;

      if (valid_mask & P9_GETATTR_MASK_BLOCKS)
        np->stat.st_blocks = blocks;
      else
        np->stat.st_blocks = div_round_up (size, 512);

      if (valid_mask & P9_GETATTR_MASK_ATIME)
        {
          np->stat.st_atim.tv_sec = atime_sec;
          np->stat.st_atim.tv_nsec = atime_nsec;
        }
      else
        memset (&np->stat.st_atim, 0,
                sizeof (struct timespec));

      if (valid_mask & P9_GETATTR_MASK_MTIME)
        {
          np->stat.st_mtim.tv_sec = mtime_sec;
          np->stat.st_mtim.tv_nsec = mtime_nsec;
        }
      else
        memset (&np->stat.st_mtim, 0,
                sizeof (struct timespec));

      if (valid_mask & P9_GETATTR_MASK_CTIME)
        {
          np->stat.st_ctim.tv_sec = ctime_sec;
          np->stat.st_ctim.tv_nsec = ctime_nsec;
        }
      else
	memset (&np->stat.st_ctim, 0,
                sizeof (struct timespec));
    }
  else
    {
      uint16_t stat_size;
      int long_stat, is_blockdev = 0;
      struct p9_stat s = { 0 };
      /* In plain 9P2000, we have to use Plan 9 stat, and
         map it to our Unix semantics as well as possible.  */
      err = p9_rpc (P9_STAT_REQUEST,
                    "4", pi->walk_fid,
                    "2" P9_STAT_BASIC "?s444", &stat_size, &s.size,
                    &s.type, &s.dev,
                    &s.qid.type, &s.qid.version, &s.qid.path,
                    &s.mode, &s.atime, &s.mtime, &s.length,
                    &s.name, &s.uid, &s.gid, &s.muid,
                    &long_stat,
                    &s.extension, &s.n_uid, &s.n_gid, &s.n_muid);
      if (err)
        {
          /* Make sure to free the partially-filled struct.  */
          p9_stat_free (&s);
          return err;
        }

      if (long_stat && p9_version < P9_VERSION_2000_U)
        {
          error (0, 0, "Unexpectedly long stat in core 9P");
          /* Forcefully ignore it; who knows what the server
             decided to put there.  */
          long_stat = 0;
        }

      np->qid = s.qid;

      if (long_stat && s.extension)
        p9_parse_extension_device (s.extension, &np->stat.st_rdev,
                                   &is_blockdev);
      else
        np->stat.st_rdev = 0;

      np->stat.st_ino = s.qid.path;
      np->stat.st_mode
          = p9_mode_to_mode (s.mode, is_blockdev) & (p9_mode_mask | ~0777);
      np->stat.st_nlink = 1;
      np->stat.st_uid = p9_uid_to_uid (s.uid, long_stat ? s.n_uid : ~0);
      np->stat.st_gid = p9_gid_to_gid (s.gid, long_stat ? s.n_gid : ~0);
      np->stat.st_size = s.length;
      np->stat.st_blksize = 512;
      np->stat.st_blocks = div_round_up (s.length, 512);
      np->stat.st_atime = s.atime;
      np->stat.st_mtime = s.mtime;
      np->stat.st_ctime = s.mtime;

      p9_stat_free (&s);
    }

  np->last_stat = time (NULL);
  *statbuf = np->stat;
  return 0;
}
