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

error_t
S_file_exec_paths (struct protid *pi, task_t task, int flags,
                   const char *path, const char *abspath,
                   const char *argv, mach_msg_type_number_t argvlen,
                   const char *envp, mach_msg_type_number_t envplen,
                   const mach_port_t *fds, mach_msg_type_number_t fdslen,
                   const mach_port_t *portarray,
                   mach_msg_type_number_t portarraylen,
                   const int *intarray,
                   mach_msg_type_number_t intarraylen,
                   const mach_port_t *deallocnames,
                   mach_msg_type_number_t deallocnameslen,
                   const mach_port_t *destroynames,
                   mach_msg_type_number_t destroynameslen)
{
  if (!pi)
    return EOPNOTSUPP;

  /* TODO */
  return EROFS;
}
