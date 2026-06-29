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
#include <hurd/fshelp.h>
#include <errno.h>

error_t
S_io_identity (struct protid *protid,
               mach_port_t *id,
               mach_msg_type_name_t *id_type,
               mach_port_t *fsys,
               mach_msg_type_name_t *fsys_type,
               ino64_t *fileno)
{
  error_t err;
  io_statbuf_t st;

  if (!protid)
    return EOPNOTSUPP;

  err = S_io_stat (protid, &st);
  if (err)
    return err;
  err = fshelp_get_identity (p9_bucket, st.st_ino, id);
  if (err)
    return err;

  *id_type = MACH_MSG_TYPE_MAKE_SEND;
  *fsys = p9_fsys_identity;
  *fsys_type = MACH_MSG_TYPE_MAKE_SEND;
  *fileno = st.st_ino;
  return 0;
}
