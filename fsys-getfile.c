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
S_fsys_getfile (struct port_info *control,
                const uid_t *uids,
                mach_msg_type_number_t nuids,
                const gid_t *gids,
                mach_msg_type_number_t ngids,
                const char *handle,
                mach_msg_type_number_t handle_len,
                mach_port_t *file,
                mach_msg_type_name_t *file_type)
{
  return EOPNOTSUPP;
}
