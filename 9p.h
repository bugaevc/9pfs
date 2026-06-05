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

#ifndef _9P_H_
#define _9P_H_

#define _GNU_SOURCE 1

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

enum p9_version
{
  P9_VERSION_2000,
  P9_VERSION_2000_U,
  P9_VERSION_2000_L,
};

enum p9_message_type
{
  /* Message types defined by the 9P2000.L extension.  */

  P9_LERROR_REQUEST = 6,
  P9_LERROR_RESPONSE = 7,
  P9_STATFS_REQUEST = 8,
  P9_STATFS_RESPONSE = 9,

  P9_LOPEN_REQUEST = 12,
  P9_LOPEN_RESPONSE = 13,
  P9_LCREATE_REQUEST = 14,
  P9_LCREATE_RESPONSE = 15,
  P9_SYMLINK_REQUEST = 16,
  P9_SYMLINK_RESPONSE = 17,
  P9_MKNOD_REQUEST = 18,
  P9_MKNOD_RESPONSE = 19,
  P9_RENAME_REQUEST = 20,
  P9_RENAME_RESPONSE = 21,
  P9_READLINK_REQUEST = 22,
  P9_READLINK_RESPONSE = 23,
  P9_GETATTR_REQUEST = 24,
  P9_GETATTR_RESPONSE = 25,
  P9_SETATTR_REQUEST = 26,
  P9_SETATTR_RESPONSE = 27,

  P9_XATTRWALK_REQUEST = 30,
  P9_XATTRWALK_RESPONSE = 31,
  P9_XATTRCREATE_REQUEST = 32,
  P9_XATTRCREATE_RESPONSE = 33,

  P9_READDIR_REQUEST = 40,
  P9_READDIR_RESPONSE = 41,

  P9_FSYNC_REQUEST = 50,
  P9_FSYNC_RESPONSE = 51,
  P9_LOCK_REQUEST = 52,
  P9_LOCK_RESPONSE = 53,
  P9_GETLOCK_REQUEST = 54,
  P9_GETLOCK_RESPONSE = 55,

  P9_LINK_REQUEST = 70,
  P9_LINK_RESPONSE = 71,
  P9_MKDIR_REQUEST = 72,
  P9_MKDIR_RESPONSE = 73,
  P9_RENAMEAT_REQUEST = 74,
  P9_RENAMEAT_RESPONSE = 75,
  P9_UNLINKAT_REQUEST = 76,
  P9_UNLINKAT_RESPONSE = 77,

  /* Message types defined by the core 9P2000 protocol.  */

  P9_VERSION_REQUEST = 100,
  P9_VERSION_RESPONSE = 101,
  P9_AUTH_REQUEST = 102,
  P9_AUTH_RESPONSE = 103,
  P9_ATTACH_REQUEST = 104,
  P9_ATTACH_RESPONSE = 105,
  P9_ERROR_REQUEST = 106,
  P9_ERROR_RESPONSE = 107,
  P9_FLUSH_REQUEST = 108,
  P9_FLUSH_RESPONSE = 109,
  P9_WALK_REQUEST = 110,
  P9_WALK_RESPONSE = 111,
  P9_OPEN_REQUEST = 112,
  P9_OPEN_RESPONSE = 113,
  P9_CREATE_REQUEST = 114,
  P9_CREATE_RESPONSE = 115,
  P9_READ_REQUEST = 116,
  P9_READ_RESPONSE = 117,
  P9_WRITE_REQUEST = 118,
  P9_WRITE_RESPONSE = 119,
  P9_CLUNK_REQUEST = 120,
  P9_CLUNK_RESPONSE = 121,
  P9_REMOVE_REQUEST = 122,
  P9_REMOVE_RESPONSE = 123,
  P9_STAT_REQUEST = 124,
  P9_STAT_RESPONSE = 125,
  P9_WSTAT_REQUEST = 126,
  P9_WSTAT_RESPONSE = 127,
};

enum p9_getattr_mask
{
  P9_GETATTR_MASK_MODE = 0x1,
  P9_GETATTR_MASK_NLINK = 0x2,
  P9_GETATTR_MASK_UID = 0x4,
  P9_GETATTR_MASK_GID = 0x8,
  P9_GETATTR_MASK_RDEV = 0x10,
  P9_GETATTR_MASK_ATIME = 0x20,
  P9_GETATTR_MASK_MTIME = 0x40,
  P9_GETATTR_MASK_CTIME = 0x80,
  P9_GETATTR_MASK_INO = 0x100,
  P9_GETATTR_MASK_SIZE = 0x200,
  P9_GETATTR_MASK_BLOCKS = 0x400,

  /* These fields are reserved for future use.  */
  P9_GETATTR_MASK_BTIME = 0x800,
  P9_GETATTR_MASK_GEN = 0x1000,
  P9_GETATTR_MASK_DATA_VERSION = 0x2000,

  /* Masks.  */
  P9_GETATTR_MASK_ALL_DEFINED = 0x7ff,
  P9_GETATTR_MASK_ALL_KNOWN = 0x3fff,
};

enum p9_setattr_mask
{
  P9_SETATTR_MASK_MODE = 0x1,
  P9_SETATTR_MASK_UID = 0x2,
  P9_SETATTR_MASK_GID = 0x4,
  P9_SETATTR_MASK_SIZE = 0x8,
  P9_SETATTR_MASK_ATIME = 0x10,
  P9_SETATTR_MASK_MTIME = 0x20,
  P9_SETATTR_MASK_CTIME = 0x40,
  P9_SETATTR_MASK_ATIME_SET = 0x80,
  P9_SETATTR_MASK_MTIME_SET = 0x100,
};

struct p9_qid
{
  unsigned char type;
  uint32_t version;
  uint64_t path;
};

struct p9_stat
{
  /* Fields defined by the core 9P2000 protocol.  */
  uint16_t size;
  uint16_t type;
  uint32_t dev;
  struct p9_qid qid;
  uint32_t mode;
  uint32_t atime;
  uint32_t mtime;
  uint64_t length;
  char *name;
  char *uid;
  char *gid;
  char *muid;

  /* Fields defined by the 9P2000.u extension.  */
  char *extension;
  uint32_t n_uid;
  uint32_t n_gid;
  uint32_t n_muid;
};

void p9_stat_dont_touch (struct p9_stat *s);
void p9_stat_free (struct p9_stat *s);

/* Send wstat request on FID for STAT.  */
error_t p9_wstat (uint32_t fid, const struct p9_stat *s);

/* Parse a serialized stat from an in-memory buffer at DATA of size
   SIZE.  Set CONSUMED to the number of bytes of input consumed.  */
error_t p9_stat_deserialize (struct p9_stat *s, size_t size,
                             const unsigned char *data,
                             size_t *restrict consumed);

error_t
p9_dirent_deserialize (size_t size, const unsigned char *data,
                       size_t *restrict consumed,
                       uint64_t *next_offset,
                       struct p9_qid *qid,
                       unsigned char *type, char **name);

enum p9_mode
{
  /* Bits defined by the 9P2000.u extension. */
  P9_MODE_SETGID =    0x40000,
  P9_MODE_SETUID =    0x80000,
  P9_MODE_SOCKET =   0x100000,
  P9_MODE_FIFO =     0x200000,
  P9_MODE_DEV =      0x800000,
  P9_MODE_SYMLINK = 0x2000000,

  /* Bits defined by the core 9P2000 protocol.  */
  P9_MODE_TMP =     0x4000000,
  P9_MODE_EXCL =   0x20000000,
  P9_MODE_APPEND = 0x40000000,
  P9_MODE_DIR =    0x80000000,
};

/* Parse the 9P2000.u stat extension string for a device.  */
int p9_parse_extension_device (const char *extension,
                               dev_t *dev, int *is_blockdev);

mode_t p9_mode_to_mode (uint32_t p9_mode, int is_blockdev);
uint32_t mode_to_p9_mode (mode_t mode);
uint32_t p9_mode_to_d_type (uint32_t p9_mode, int is_blockdev);

uid_t p9_uid_to_uid (const char *name, uint32_t n_uid);
gid_t p9_gid_to_gid (const char *name, uint32_t n_gid);

#endif
