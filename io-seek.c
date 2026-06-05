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
#include "io_S.h"
#include <fcntl.h>
#include <errno.h>
#include <mach.h>

error_t
S_io_seek (struct protid *protid,
           loff_t offset,
           int whence,
           loff_t *newoffset)
{
  error_t err = 0;
  struct peropen *po;
  struct node *np;

  if (protid == NULL)
    return EOPNOTSUPP;

  po = protid->po;
  np = po->np;

  pthread_mutex_lock (&np->lock);

  /* Update OFFSET to reflect the desired offset
     from the beginning of the file.  */
  switch (whence)
    {
    case SEEK_CUR:
      offset += po->offset;
      break;

    case SEEK_END:
      /* TODO: validate stat? */
      offset += np->stat.st_size;
      break;

    case SEEK_SET:
      break;

    default:
      err = EINVAL;
      break;
    }

  if (!err && offset >= 0)
    *newoffset = po->offset = offset;

  pthread_mutex_unlock (&np->lock);
  return err;
}
