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

#ifndef _9PFS_H_
#define _9PFS_H_

#define _GNU_SOURCE 1

#include "9p.h"
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <refcount.h>
#include <sys/stat.h>
#include <hurd/ports.h>

/* The "source" of this connection.  */
extern char *p9_source;

/* Our connection to a 9P server.  */
extern FILE *p9_conn_in, *p9_conn_out;

/* If set (not NULL, not -1), always use this
   user on the remote end.  */
extern char *p9_user_name;
extern uint32_t p9_uid;

extern boolean_t p9_readonly;

/* UID and GID of the local 'nobody' user.  */
extern uid_t p9_nobody_uid;
extern gid_t p9_nobody_gid;

/* The aname to attach to.  */
extern char *p9_aname;

/* Mask to apply to file permissions.  */
extern mode_t p9_mode_mask;

/* Dump 9P exchanges.  */
extern int p9_dump_exchanges;

/* If set, settrans on this path instead of replying
   on the bootstrap port.  */
extern char *p9_settrans_path;

extern enum p9_version p9_version;
/* Global limit on message sizes, including all the headings.  */
extern uint32_t p9_max_message_size;

extern struct port_class *p9_control_class;
extern struct port_class *p9_protid_class;
extern struct port_bucket *p9_bucket;
extern struct port_bucket *p9_pager_bucket;
extern struct port_info *p9_control;

extern mach_port_t p9_fsys_identity;

extern const struct argp p9_runtime_argp;

void p9_startup (void);
error_t p9_append_args (char **argz, size_t *argz_len);

struct p9_dir_buffer
{
  void *base;
  void *ptr;
  size_t allocated_size;
  size_t remaining_size;
};

struct node
{
  pthread_mutex_t lock;
  refcounts_t refcounts;
  io_statbuf_t stat;
  time_t last_stat;
  struct p9_qid qid;
  size_t max_message_size;
  char *path;
  struct pager *pager;
  int pager_fid;

  struct node *parent;
  struct node *children;
  size_t children_count;
};

struct peropen
{
  refcount_t refcount;
  loff_t offset;
  struct node *np;
  mach_port_t root_parent;
  uint64_t root_qid_path;
  int user_open_flags;
};

struct protid
{
  struct port_info pi;
  struct iouser *user;
  struct peropen *po;
  uint32_t walk_fid, io_fid;
  int server_open_flags;

  struct
  {
    int next_entry;
    uint64_t next_offset;
    struct p9_dir_buffer buffer;
  } dir;
};

struct node *p9_make_node (void);
struct peropen *p9_make_peropen (struct node *np, int flags,
                                 struct peropen *context);
struct protid *p9_make_protid (struct peropen *po,
                               struct iouser *cred);
void p9_release_protid (void *arg);
void p9_release_peropen (struct peropen *po);
void p9_nref (struct node *np);
void p9_nrele (struct node *np);
void p9_nput (struct node *np);

enum
{
  /* A null among fids.  */
  P9_NO_FID = ((uint32_t) -1)
};

uint32_t p9_fid_alloc (void);
void p9_fid_free (uint32_t fid);

enum
{
  /* Modes settable with io_*_openmodes () calls.  */
  HONORED_STATE_MODES = O_APPEND|O_ASYNC|O_FSYNC|O_NONBLOCK|O_NOATIME,
  /* Modes that only have meaning during open () */
  OPENONLY_STATE_MODES = O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS|O_NONBLOCK|O_EXLOCK|O_SHLOCK
};

error_t p9_ensure_open (struct protid *pi, int flags);

/* For MIG.  */
typedef struct protid *protid_t;

static inline struct protid * __attribute__ ((unused))
begin_using_protid_port (mach_port_t port)
{
  return ports_lookup_port (p9_bucket, port, p9_protid_class);
}

static inline struct protid * __attribute__ ((unused))
begin_using_protid_payload (unsigned long payload)
{
  return ports_lookup_payload (p9_bucket, payload, p9_protid_class);
}

static inline void __attribute__ ((unused))
end_using_protid_port (struct protid *cred)
{
  if (cred)
    ports_port_deref (cred);
}

static inline struct port_info * __attribute__ ((unused))
begin_using_control_port (fsys_t port)
{
  return ports_lookup_port (p9_bucket, port, p9_control_class);
}

static inline struct port_info * __attribute__ ((unused))
begin_using_control_payload (unsigned long payload)
{
  return ports_lookup_payload (p9_bucket, payload, p9_control_class);
}

static inline void __attribute__ ((unused))
end_using_control_port (struct port_info *cred)
{
  if (cred)
    ports_port_deref (cred);
}

#endif
