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
#include <errno.h>
#include <hurd/iohelp.h>

error_t
S_io_reauthenticate (struct protid *protid,
                     mach_port_t rend_port)
{
  error_t err, err2;
  struct peropen *po;
  struct protid *new_pi;
  mach_port_t auth, new_right;

  if (!protid)
    return EOPNOTSUPP;

  auth = getauth ();
  if (!MACH_PORT_VALID (auth))
    return EGRATUITOUS;

  /* TODO: this maybe needs to hold the node lock? */

  po = protid->po;
  refcount_ref (&po->refcount);

  /* Make a new protid, without any user for now.
     This also prevents the port from being instantly installed
     into its bucket.  */
  do
    new_pi = p9_make_protid (po, NULL);
  while (!new_pi && errno == EINTR);

  if (!new_pi)
    {
      mach_port_deallocate (mach_task_self (), auth);
      p9_release_peropen (po);
      return errno;
    }

  new_right = ports_get_send_right (new_pi);
  assert_backtrace (MACH_PORT_VALID (new_right));

  err = iohelp_reauth (&new_pi->user, auth, rend_port,
                       new_right, TRUE);

  if (!err)
    {
      err2 = mach_port_deallocate (mach_task_self (), rend_port);
      assert_perror_backtrace (err2);
    }
  err2 = mach_port_deallocate (mach_task_self (), new_right);
  assert_perror_backtrace (err2);
  err2 = mach_port_deallocate (mach_task_self (), auth);
  assert_perror_backtrace (err2);

  if (!err)
    {
      err2 = mach_port_move_member (mach_task_self (), new_right,
                                    p9_bucket->portset);
      assert_perror_backtrace (err2);
    }

  ports_port_deref (new_pi);

  return err;
}
