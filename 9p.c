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

#include "9p.h"
#include "9pfs.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <errno.h>

/* These constant describe the minimum sizes of 9P2000 and 9P2000.u stats,
   as specified in stat.size; the actual size of the stat will be 2 bytes
   larger.  */
#define P9_STAT_MIN_SIZE (2 + 4 + 13 + 4 + 4 + 4 + 8 + 2 + 2 + 2 + 2)
#define P9_U_STAT_MIN_SIZE (P9_STAT_MIN_SIZE + 2 + 4 + 4 + 4)

void
p9_stat_dont_touch (struct p9_stat *s)
{
  s->size = P9_STAT_MIN_SIZE;
  if (p9_version >= P9_VERSION_2000_U)
    s->size = P9_U_STAT_MIN_SIZE;

  s->type = ~0;
  s->dev = ~0;
  s->qid.type = ~0;
  s->qid.version = ~0;
  s->qid.path = ~0;
  s->mode = ~0;
  s->atime = ~0;
  s->mtime = ~0;
  s->length = ~0;
  s->name = strdup ("");
  s->uid = strdup ("");
  s->gid = strdup ("");
  s->muid = strdup ("");

  if (p9_version >= P9_VERSION_2000_U)
    s->extension = strdup ("");
  else
    s->extension = NULL;

  s->n_uid = ~0;
  s->n_gid = ~0;
  s->n_muid = ~0;
}

void
p9_stat_free (struct p9_stat *s)
{
  free (s->name);
  free (s->uid);
  free (s->gid);
  free (s->muid);
  free (s->extension);
}

error_t
p9_stat_deserialize (struct p9_stat *s, size_t size,
                     const unsigned char *data,
                     size_t *restrict consumed)
{
  uint16_t str_len;

  /* On malformed data we return EIO, not EINVAL.
     This is because it's not our caller's fault.  */

  if (size < 2)
    return EIO;
  *consumed = 0;

  memcpy (&s->size, data, 2);
  s->size = le16toh (s->size);
  *consumed += 2;

  /* Check that the serialized size is sane.  */
  if (s->size + 2 > size)
    return EIO;
  if (s->size < P9_STAT_MIN_SIZE)
    return EIO;
  if (p9_version >= P9_VERSION_2000_U && s->size < P9_U_STAT_MIN_SIZE)
    return EIO;

  memcpy (&s->type, data + *consumed, 2);
  s->type = le16toh (s->type);
  *consumed += 2;

  memcpy (&s->dev, data + *consumed, 4);
  s->dev = le32toh (s->dev);
  *consumed += 4;

  memcpy (&s->qid.type, data + *consumed, 1);
  (*consumed)++;

  memcpy (&s->qid.version, data + *consumed, 4);
  s->qid.version = le32toh (s->qid.version);
  *consumed += 4;

  memcpy (&s->qid.path, data + *consumed, 8);
  s->qid.path = le64toh (s->qid.path);
  *consumed += 8;

  memcpy (&s->mode, data + *consumed, 4);
  s->mode = le32toh (s->mode);
  *consumed += 4;

  memcpy (&s->atime, data + *consumed, 4);
  s->atime = le32toh (s->atime);
  *consumed += 4;

  memcpy (&s->mtime, data + *consumed, 4);
  s->mtime = le32toh (s->mtime);
  *consumed += 4;

  memcpy (&s->length, data + *consumed, 8);
  s->length = le64toh (s->length);
  *consumed += 8;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len + 2 > s->size + 2)
    return EIO;
  s->name = strndup ((const char *) data + *consumed, str_len);
  if (s->name == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len + 2 > s->size + 2)
    return EIO;
  s->uid = strndup ((const char *) data + *consumed, str_len);
  if (s->uid == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len + 2 > s->size + 2)
    return EIO;
  s->gid = strndup ((const char *) data + *consumed, str_len);
  if (s->gid == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len > s->size + 2)
    return EIO;
  s->muid = strndup ((const char *) data + *consumed, str_len);
  if (s->muid == NULL)
    return errno;
  *consumed += str_len;

  if (p9_version < P9_VERSION_2000_U)
    return 0;

  if (*consumed + 2 > s->size + 2)
    return EIO;
  memcpy (&str_len, data + *consumed, 2);
  *consumed += 2;
  if (*consumed + str_len + 4 + 4 + 4 > s->size + 2)
    return EIO;
  s->extension = strndup ((const char *) data + *consumed, str_len);
  if (s->extension == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&s->n_uid, data + *consumed, 4);
  s->n_uid = le32toh (s->n_uid);
  *consumed += 4;

  memcpy (&s->n_gid, data + *consumed, 4);
  s->n_gid = le32toh (s->n_gid);
  *consumed += 4;

  memcpy (&s->n_muid, data + *consumed, 4);
  s->n_muid = le32toh (s->n_muid);
  *consumed += 4;

  return 0;
}

error_t
p9_dirent_deserialize (size_t size, const unsigned char *data,
                       size_t *restrict consumed,
                       uint64_t *next_offset,
                       struct p9_qid *qid,
                       unsigned char *type, char **name)
{
  uint16_t name_len;

  if (size < 13 + 8 + 1 + 2)
    return EIO;
  *consumed = 0;

  memcpy (&qid->type, data + *consumed, 1);
  (*consumed)++;

  memcpy (&qid->version, data + *consumed, 4);
  qid->version = le32toh (qid->version);
  *consumed += 4;

  memcpy (&qid->path, data + *consumed, 8);
  qid->path = le64toh (qid->path);
  *consumed += 8;

  memcpy (next_offset, data + *consumed, 8);
  *next_offset = le64toh (*next_offset);
  *consumed += 8;

  memcpy (type, data + *consumed, 1);
  (*consumed)++;

  memcpy (&name_len, data + *consumed, 2);
  name_len = le16toh (name_len);
  *consumed += 2;
  if (*consumed + name_len > size)
    return EIO;
  *name = strndup ((const char *) data + *consumed, name_len);
  if (name == NULL)
    return errno;
  *consumed += name_len;

  return 0;
}

mode_t
p9_mode_to_mode (uint32_t p9_mode, int is_blockdev)
{
  mode_t mode = p9_mode & 0777;

  if (p9_version >= P9_VERSION_2000_U)
    {
      if (p9_mode & P9_MODE_SETGID)
        mode |= S_ISGID;
      if (p9_mode & P9_MODE_SETUID)
        mode |= S_ISUID;
      if (p9_mode & P9_MODE_SOCKET)
        mode |= S_IFSOCK;
      if (p9_mode & P9_MODE_FIFO)
        mode |= S_IFIFO;
      if (p9_mode & P9_MODE_DEV)
        mode |= is_blockdev ? S_IFBLK : S_IFCHR;
      if (p9_mode & P9_MODE_SYMLINK)
        mode |= S_IFLNK;
    }

  if (p9_mode & P9_MODE_DIR)
    mode |= S_IFDIR;

  if ((mode & S_IFMT) == 0)
    mode |= S_IFREG;

  return mode;
}

uint32_t
mode_to_p9_mode (mode_t mode)
{
  uint32_t p9_mode = mode & 0777;

  if (p9_version >= P9_VERSION_2000_U)
    {
      if (mode & S_ISGID)
        p9_mode |= P9_MODE_SETGID;
      if (mode & S_ISUID)
        p9_mode |= P9_MODE_SETUID;
      if (mode & S_IFSOCK)
        p9_mode |= P9_MODE_SOCKET;
      if (mode & S_IFIFO)
        p9_mode |= P9_MODE_FIFO;
      if (mode & (S_IFCHR | S_IFBLK))
        p9_mode |= P9_MODE_DEV;
      if (mode & S_IFLNK)
        p9_mode |= P9_MODE_SYMLINK;
    }

  if (mode & S_IFDIR)
    p9_mode |= P9_MODE_DIR;

  return p9_mode;
}

uint32_t
p9_mode_to_d_type (uint32_t p9_mode, int is_blockdev)
{
  if (p9_version >= P9_VERSION_2000_U)
    {
      if (p9_mode & P9_MODE_SOCKET)
        return DT_SOCK;
      if (p9_mode & P9_MODE_FIFO)
        return DT_FIFO;
      if (p9_mode & P9_MODE_DEV)
        return is_blockdev ? DT_BLK : DT_CHR;
      if (p9_mode & P9_MODE_SYMLINK)
        return DT_LNK;
    }

  if (p9_mode & P9_MODE_DIR)
    return DT_DIR;

  return DT_REG;
}

int
p9_parse_extension_device (const char *extension,
                           dev_t *dev, int *is_blockdev)
{
  int minor, major;
  char c;
  int scanned;

  scanned = sscanf (extension, "%c%d%d", &c, &major, &minor);
  if (scanned != 3)
    return 0;

  if (c == 'c')
    *is_blockdev = 0;
  else if (c == 'b')
    *is_blockdev = 1;
  else
    return 0;

  *dev = makedev (major, minor);
  return 1;
}

uid_t
p9_uid_to_uid (const char *name, uint32_t n_uid)
{
  if (p9_user_name == NULL || name == NULL)
    return p9_nobody_uid;

  if (strcmp (p9_user_name, name) == 0)
    return getuid ();
  return p9_nobody_uid;
}

gid_t
p9_gid_to_gid (const char *name, uint32_t n_gid)
{
  return p9_nobody_gid;
}
