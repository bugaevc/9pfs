#define _GNU_SOURCE 1

#include "9p.h"
#include "9pfs.h"
#include <stdlib.h>
#include <string.h>
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
  s->name = strndup (data + *consumed, str_len);
  if (s->name == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len + 2 > s->size + 2)
    return EIO;
  s->uid = strndup (data + *consumed, str_len);
  if (s->uid == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len + 2 > s->size + 2)
    return EIO;
  s->gid = strndup (data + *consumed, str_len);
  if (s->gid == NULL)
    return errno;
  *consumed += str_len;

  memcpy (&str_len, data + *consumed, 2);
  str_len = le16toh (str_len);
  *consumed += 2;
  if (*consumed + str_len > s->size + 2)
    return EIO;
  s->muid = strndup (data + *consumed, str_len);
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
  s->extension = strndup (data + *consumed, str_len);
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
