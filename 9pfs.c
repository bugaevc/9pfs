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
#include "9p-rpc.h"
#include <string.h>
#include <mach.h>
#include <hurd/fsys.h>
#include <hurd/ports.h>

enum p9_version p9_version;

struct port_class *p9_control_class;
struct port_class *p9_protid_class;
struct port_bucket *p9_bucket;
struct port_info *p9_control;

void
p9_startup (void)
{
  error_t err;
  mach_port_t underlying, right, bootstrap;
  uint32_t max_message_size = 16 * 1024 * 1024;
  char *remote_version;

  /* Create the port classes, bucket, and control port.  */
  p9_control_class = ports_create_class (0, 0);
  p9_protid_class = ports_create_class (p9_release_protid, 0);
  p9_bucket = ports_create_bucket ();

  if (p9_settrans_path)
    {
      underlying = file_name_lookup (p9_settrans_path, O_CREAT, 0);
      if (underlying == MACH_PORT_NULL)
        error (10, errno, "Cannot open %s", p9_settrans_path);
    }
  else
    {
      /* Obtain and sanity check the bootstrap port.  */
      err = task_get_bootstrap_port (mach_task_self (), &bootstrap);
      assert_perror_backtrace (err);

      if (bootstrap == MACH_PORT_NULL)
        error (10, 0, "Must be started as a translator");
    }

  /* Negotiate the 9P connection.  */
  err = p9_rpc (P9_VERSION_REQUEST,
                "4s", max_message_size, "9P2000.L",
                "4s", &max_message_size, &remote_version);
  if (err)
    error (1, err, "Failed to negotitate protocol version");

  if (strcmp (remote_version, "9P2000.L") == 0)
    p9_version = P9_VERSION_2000_L;
  else if (strcmp (remote_version, "9P2000.u") == 0)
    p9_version = P9_VERSION_2000_U;
  else
    p9_version = P9_VERSION_2000;

  if (max_message_size == 0)
    error (1, 0, "Remote server sent max message size of 0");

  err = ports_create_port (p9_control_class, p9_bucket,
                           sizeof (struct port_info), &p9_control);
  assert_perror_backtrace (err);

  right = ports_get_right (p9_control);

  if (p9_settrans_path)
    {
      /* Set ourselves as a translator.  */
      err = file_set_translator (underlying, 0, FS_TRANS_SET, 0, "",
                                 0, right, MACH_MSG_TYPE_MAKE_SEND);
      if (err)
        error (11, err, "file_set_translator");
    }
  else
    {
      /* Check in with whoever started us.  */
      err = fsys_startup (bootstrap, 0,
                          right, MACH_MSG_TYPE_MAKE_SEND,
                          &underlying);
      if (err)
        error (11, err, "fsys_startup");

      err = mach_port_deallocate (mach_task_self (), bootstrap);
      assert_perror_backtrace (err);
    }

    err = mach_port_deallocate (mach_task_self (), underlying);
    assert_perror_backtrace (err);
}

error_t
p9_ensure_open (struct protid *pi, int flags)
{
  error_t err;
  unsigned char core_mode;
  uint32_t l_flags;

  uint32_t new_fid;
  uint32_t max_message_size;
  uint16_t n_qids;
  struct p9_qid qid;

  /* Check if it's already open with (at least) those flags.  */
  if ((pi->server_open_flags & flags) == flags)
    return 0;

  switch (flags | pi->server_open_flags)
    {
    case O_READ:
      core_mode = 0;
      l_flags = 0;
      break;
    case O_WRITE:
      core_mode = 1;
      l_flags = 0;
      break;
    case O_RDWR:
      core_mode = 2;
      l_flags = 2;
      break;
    default:
      assert_backtrace (!"Bad flags");
    }

  /* TODO: Do the below requests concurrently.  */

  if (pi->io_fid != P9_NO_FID)
    {
      /* Close any existing I/O fid.  */
      err = p9_rpc (P9_CLUNK_REQUEST,
                    "4", pi->io_fid, "");
      if (err)
        error (0, err, "Failed to clunk fid");
      pi->io_fid = P9_NO_FID;
      pi->server_open_flags = 0;
    }

  /* Duplicate the walk fid to a fresh one that
     will become the new I/O fid.  */
  new_fid = p9_fid_alloc ();
  err = p9_rpc (P9_WALK_REQUEST,
                "442", pi->walk_fid, new_fid, 0,
                "2", &n_qids);
  if (err)
    return err;

  pi->io_fid = new_fid;

  if (p9_version >= P9_VERSION_2000_L)
    err = p9_rpc (P9_LOPEN_REQUEST,
                  "44", new_fid, l_flags,
                  "1484", &qid.type, &qid.version, &qid.path,
                  &max_message_size);
  else
    err = p9_rpc (P9_OPEN_REQUEST,
                  "41", new_fid, core_mode,
                  "1484", &qid.type, &qid.version, &qid.path,
                  &max_message_size);

  if (!err)
    {
      pi->server_open_flags |= flags;
      /* Some servers are known to send a 0 iounit; ignore it.  */
      if (max_message_size > 0)
        pi->po->np->max_message_size = max_message_size;
    }

  return err;
}
