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

#define FILE_INTRAN protid_t begin_using_protid_port (file_t)
#define FILE_INTRAN_PAYLOAD protid_t begin_using_protid_payload
#define FILE_DESTRUCTOR end_using_protid_port (protid_t)

#define IO_INTRAN protid_t begin_using_protid_port (io_t)
#define IO_INTRAN_PAYLOAD protid_t begin_using_protid_payload
#define IO_DESTRUCTOR end_using_protid_port (protid_t)

#define FSYS_INTRAN port_info_t begin_using_control_port (fsys_t)
#define FSYS_INTRAN_PAYLOAD port_info_t begin_using_control_payload
#define FSYS_DESTRUCTOR end_using_control_port (port_info_t)

#define FILE_IMPORTS import "9pfs.h";
#define IO_IMPORTS import "9pfs.h";
#define FSYS_IMPORTS import "9pfs.h";
