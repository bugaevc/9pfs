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

#include "9pfs.h"
#include "io_S.h"
#include <unistd.h>
#include <errno.h>

error_t
S_io_pathconf (struct protid *protid,
               int name, int *value)
{
  if (!protid)
    return EOPNOTSUPP;

  switch (name)
    {
    /* FIXME: Surely 9P and/or the servers do limit these.  */
    case _PC_LINK_MAX:
    case _PC_MAX_CANON:
    case _PC_MAX_INPUT:
    case _PC_PIPE_BUF:
    case _PC_VDISABLE:
    case _PC_SOCK_MAXBUF:
    case _PC_PATH_MAX:
    case _PC_REC_MAX_XFER_SIZE:
    case _PC_REC_INCR_XFER_SIZE:
    case _PC_SYMLINK_MAX:
      *value = -1;
      break;

    case _PC_NAME_MAX:
      *value = 1024;
      break;

    case _PC_CHOWN_RESTRICTED:
    case _PC_NO_TRUNC:
    case _PC_2_SYMLINKS:
      *value = 1;
      break;

    case _PC_PRIO_IO:
    case _PC_SYNC_IO:
    case _PC_ASYNC_IO:
      *value = 0;
      break;

    case _PC_FILESIZEBITS:
      /* Why, yes, we support 64-bit file sizes.  */
      *value = 64;
      break;

    case _PC_REC_MIN_XFER_SIZE:
    case _PC_REC_XFER_ALIGN:
    case _PC_ALLOC_SIZE_MIN:
      *value = vm_page_size;
      break;

    default:
      return EINVAL;
    }

  return 0;
}
