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
#include "fsys_S.h"
#include <errno.h>

error_t
S_fsys_getpriv (struct port_info *control,
                mach_port_t *host_priv,
                mach_msg_type_name_t *host_priv_type,
                mach_port_t *device_master,
                mach_msg_type_name_t *device_master_type,
                mach_port_t *fstask,
                mach_msg_type_name_t *fstask_type)
{
  return EOPNOTSUPP;
}
