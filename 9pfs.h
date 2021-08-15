#define _GNU_SOURCE 1

#include "9p.h"
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>

/* Our connection to a 9P server.  */
extern FILE *p9_connection;

/* If set (not NULL, not -1), always use this
   user on the remote end.  */
extern char *p9_user_name;
extern uint32_t p9_uid;

/* UID and GID of the local 'nobody' user.  */
extern uid_t p9_nobody_uid;
extern gid_t p9_nobody_gid;

/* The aname to attach to.  */
extern char *p9_aname;

/* Mask to apply to file permissions.  */
extern mode_t p9_mode_mask;

/* Dump 9P exchanges.  */
extern int p9_dump_exchanges;

error_t p9_init_fs (void);

extern enum p9_version p9_version;
