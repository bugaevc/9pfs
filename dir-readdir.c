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
#include "fs_S.h"
#include <dirent.h>
#include <string.h>
#include <errno.h>

static size_t
round_size_up (size_t size, size_t align)
{
  return (size + align - 1) / align * align;
}

error_t
S_dir_readdir (struct protid *pi,
               char **data,
               mach_msg_type_number_t *datacnt,
               boolean_t *data_dealloc,
               int entry,
               int nentries,
               vm_size_t bufsize,
               int *amt)
{
  error_t err;
  struct node *np;
  struct p9_dir_buffer *db;
  struct dirent64 *dirent;
  int can_reuse_io_fid;
  enum p9_message_type request_type;
  uint32_t receive_size;

  vm_size_t data_buffer_size = *datacnt;
  int data_was_allocated = 0;

  if (!pi)
    return EOPNOTSUPP;

  np = pi->po->np;
  db = &pi->dir.buffer;

  *amt = 0;
  *datacnt = 0;

  pthread_mutex_lock (&np->lock);

  int
  make_space (size_t space)
  {
    error_t err;
    void *addr;
    size_t required_size = *datacnt + space;
    size_t alloc_size = round_page (required_size);

    if (bufsize != 0 && required_size > bufsize)
      return 0;
    if (required_size <= data_buffer_size)
      return 1;

    err = vm_allocate (mach_task_self (), (vm_address_t *) &addr,
                       alloc_size, 1);
    if (err)
      return 0;
    memcpy (addr, *data, *datacnt);
    if (data_was_allocated)
      vm_deallocate (mach_task_self (), (vm_address_t) *data,
                     data_buffer_size);
    *data = addr;
    data_buffer_size = alloc_size;
    data_was_allocated = 1;
    *data_dealloc = 1;

    return 1;
  }

 parse_existing:
  while (entry >= pi->dir.next_entry && db->remaining_size > 0
         && (*amt < nentries || nentries == -1))
    {
      /* Look through the buffered data.  */
      if (p9_version >= P9_VERSION_2000_L)
        {
          struct p9_qid qid;
          size_t consumed = 0;
          unsigned char type;
          char *name;
          size_t namelen, reclen;

          err = p9_dirent_deserialize (db->remaining_size, db->ptr,
                                       &consumed, &pi->dir.next_offset,
                                       &qid, &type, &name);
          if (err)
            goto out;

          if (entry > pi->dir.next_entry)
            goto next_entry_l;

          namelen = strlen (name);
          reclen = round_size_up (sizeof (struct dirent64) + namelen,
                                  __alignof__ (struct dirent64));
          if (!make_space (reclen))
            break;
          dirent = (struct dirent64 *) (*data + *datacnt);
          assert_backtrace ((uintptr_t) dirent
                            % __alignof__ (struct dirent64) == 0);
          dirent->d_ino = qid.path;
          dirent->d_reclen = reclen;
          dirent->d_type = type;  /* DT values match between Linux and Hurd */
          dirent->d_namlen = namelen;
          memcpy (&dirent->d_name[0], name, namelen);
          dirent->d_name[namelen] = 0;

          *datacnt += reclen;
          (*amt)++;
          entry++;

 next_entry_l:
          free (name);
          db->ptr += consumed;
          db->remaining_size -= consumed;
          pi->dir.next_entry++;
        }
      else
        {
          struct p9_stat s = { 0 };
          size_t consumed = 0;
          size_t namelen, reclen;
          dev_t dev;
          int is_blockdev;

          err = p9_stat_deserialize (&s, db->remaining_size,
                                     db->ptr, &consumed);
          if (err)
            goto out;

          if (entry > pi->dir.next_entry)
            goto next_entry;

          /* Record takes up as much space as struct dirent64, plus the space
             for the name, rounded up to the alignment.  Note that d_name is
             declared as a single-character array, which takes up exactly as
             much space as we need for the null terminator.  */
          namelen = strlen (s.name);
          reclen = round_size_up (sizeof (struct dirent64) + namelen,
                                  __alignof__ (struct dirent64));

          if (!make_space (reclen))
            {
              p9_stat_free (&s);
              break;
            }

          if (s.extension == NULL
              || !p9_parse_extension_device (s.extension, &dev,
                                             &is_blockdev))
            is_blockdev = 0;

          dirent = (struct dirent64 *) (*data + *datacnt);
          assert_backtrace ((uintptr_t) dirent
                            % __alignof__ (struct dirent64) == 0);
          dirent->d_ino = s.qid.path;
          dirent->d_reclen = reclen;
          dirent->d_type = p9_mode_to_d_type (s.mode, is_blockdev);
          dirent->d_namlen = namelen;
          memcpy (&dirent->d_name[0], s.name, namelen);
          dirent->d_name[namelen] = 0;

          *datacnt += reclen;
          (*amt)++;
          entry++;

 next_entry:
          db->ptr += consumed;
          db->remaining_size -= consumed;
          pi->dir.next_entry++;
          pi->dir.next_offset += consumed;

          p9_stat_free (&s);
        }
    }

  /* This is the end of the buffered data, deallocate it.  */
  if (db->remaining_size == 0 && db->base != NULL)
    {
      err = vm_deallocate (mach_task_self (), (vm_address_t) db->base,
                           db->allocated_size);
      assert_perror_backtrace (err);
      db->base = NULL;
      db->ptr = NULL;
      db->allocated_size = 0;
    }

  /* If we managed to fill in some entries (or none were
     requested in the first place), this is it.  */
  if (*amt > 0 || nentries == 0)
    goto out;

  /* We're going to load more data from the server.
     See if we can reuse the open I/O fid.  */
  can_reuse_io_fid = pi->io_fid != P9_NO_FID && entry >= pi->dir.next_entry;

  /* TODO: Do the below requests concurrently.  */

  if (!can_reuse_io_fid)
    {
      /* Reset server_open_flags to force the p9_ensure_open ()
         call below to recreate the fid.  */
      pi->server_open_flags = 0;
      /* The new fid will start from the beginning.  */
      pi->dir.next_entry = 0;
      pi->dir.next_offset = 0;

      /* Open a new fid for I/O.  */
      err = p9_ensure_open (pi, O_READ);
      if (err)
        goto out;
    }

  /* Allocate memory to hold read data.  */
  receive_size = vm_page_size * 2;
  if (receive_size > np->max_message_size - 24)
    receive_size = np->max_message_size - 24;
  err = vm_allocate (mach_task_self (), (vm_address_t *) &db->base,
                     receive_size, 1);
  if (err)
    goto out;
  db->allocated_size = receive_size;

  /* Actually request the data from the server.  */
  if (p9_version >= P9_VERSION_2000_L)
    request_type = P9_READDIR_REQUEST;
  else
    request_type = P9_READ_REQUEST;

  err = p9_rpc (request_type,
                "484", pi->io_fid, pi->dir.next_offset,
                db->allocated_size,
                "d", &receive_size, db->base);

  /* If we got nothing, make sure to clean up.  */
  if (err || receive_size == 0)
    {
      vm_deallocate (mach_task_self (), (vm_address_t) db->base,
                     db->allocated_size);
      db->base = NULL;
      db->allocated_size = 0;
      db->remaining_size = 0;
    }
  if (err)
    goto out;

  /* If there are no more entries, just say that.  */
  if (receive_size == 0)
    {
      *data = NULL;
      *datacnt = 0;
      goto out;
    }

  db->ptr = db->base;
  db->remaining_size = receive_size;
  goto parse_existing;

out:
  pthread_mutex_unlock (&np->lock);
  if (err && data_was_allocated)
    {
      vm_deallocate (mach_task_self (), (vm_address_t) *data,
                     data_buffer_size);
      *data = NULL;
      *datacnt = 0;
      *data_dealloc = 0;
    }
  return err;
}
