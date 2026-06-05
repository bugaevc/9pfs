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
#include "fs_S.h"
#include <errno.h>
#include <unistd.h>
#include <hurd/fshelp.h>

error_t
S_file_getcontrol (struct protid *pi, mach_port_t *control,
                   mach_msg_type_name_t *control_type)
{
  error_t err;
  struct stat64 pseudo_root_stat;

  if (!pi)
    return EOPNOTSUPP;

  /* Make up a "root node stat" to appease fshelp_iscontroller.  */
  memset (&pseudo_root_stat, 0, sizeof (pseudo_root_stat));
  pseudo_root_stat.st_uid = geteuid ();
  pseudo_root_stat.st_gid = getegid ();

  err = fshelp_iscontroller (&pseudo_root_stat, pi->user);
  if (err)
    return err;

  *control = ports_get_right (p9_control);
  *control_type = MACH_MSG_TYPE_MAKE_SEND;
  return 0;
}
