/*
 * skiplistdb
 *
 *
 * skiplistdb is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#ifndef _SKIPLISTDB_H_
#define _SKIPLISTDB_H_

#include <stdio.h>
#include "strarray.h"

typedef enum {
        ZERO_SKIP,
        TWO_SKIP,
        INVALID_DB,
} DBType;

typedef enum {
        DB_OPEN,
        DB_CLOSE,
        DB_INIT,
        DB_LOCKED,
} DBState;

typedef enum {
        DB_DUMP_ACTIVE,
        DB_DUMP_ALL,
} DBDumpLevel;

/* Return codes */
enum {
        SDB_OK             =  0,
        SDB_DONE           =  1,
        SDB_IOERROR        = -1,
        SDB_AGAIN          = -2,
        SDB_EXISTS         = -3,
        SDB_INTERNAL       = -4,
        SDB_NOTFOUND       = -5,
        SDB_NOTIMPLEMENTED = -6,
        SDB_FULL           = -7,
        SDB_ERROR          = -8,
        SDB_INVALID_DB     = -9,
};

/* DB File Flags */
typedef enum {
        SDB_CREATE       = 0x01, /* Create the DB if non-existent */
        SDB_MBOXSORT     = 0x02, /* Use mailbox sort order ('.' sorts first) */
        SDB_CONVERT      = 0x04, /* Convert to named format, if not already */
        SDB_NOCOMPAT     = 0x08  /* Don't run DB compaction routines */
} SBDFlags;

struct txn;
struct dbdata;
struct skiplistdb;

/* callback */
typedef int foreach_p(void *rock,
                      unsigned char *key, size_t keylen,
                      unsigned char *data, size_t datalen);

typedef int foreach_cb(void *rock,
                       unsigned char *key, size_t keylen,
                       unsigned char *data, size_t datalen);


/*
 * The interface for the skiplist database backend
 */
struct skiplistdb_operations {
        int (*init)(DBType type, struct skiplistdb **db, struct txn **tid);
        int (*final)(struct skiplistdb *db);
        int (*open)(const char *dbdir, struct skiplistdb *db, int flags,
                    struct txn **tid);
        int (*close)(struct skiplistdb *db);
        int (*sync)(struct skiplistdb *db);
        int (*archive)(struct skiplistdb *db, const struct str_array *fnames, const char *dirname);
        int (*unlink)(struct skiplistdb *db, const char *fname, int flags);
        int (*fetch)(struct skiplistdb *db, unsigned char *key, size_t keylen,
                     unsigned char **data, size_t *datalen, struct txn **tid);
        int (*fetchlock)(struct skiplistdb *db, unsigned char *key, size_t keylen,
                         unsigned char **data, size_t *datalen, struct txn **tid);
        int (*fetchnext)(struct skiplistdb *db, unsigned char *key, size_t keylen,
                         unsigned char **foundkey, size_t *foundkeylen,
                         unsigned char **data, size_t *datalen, struct txn **tid);
        int (*foreach)(struct skiplistdb *db,
                        unsigned char *prefix, size_t prefixlen,
                        foreach_p *p, foreach_cb *cb, void *rock,
                        struct txn **tid);
        int (*add)(struct skiplistdb *db, unsigned char *key, size_t keylen,
                   unsigned char *data, size_t datalen, struct txn **tid);
        int (*remove)(struct skiplistdb *db, unsigned char *key, size_t keylen,
                      struct txn **tid, int force);
        int (*store)(struct skiplistdb *db, unsigned char *key, size_t keylen,
                     unsigned char *data, size_t datalen, struct txn **tid);
        int (*commit)(struct skiplistdb *db, struct txn **tid);
        int (*abort)(struct skiplistdb *db, struct txn **tid);
        int (*dump)(struct skiplistdb *db, DBDumpLevel level);
        int (*consistent)(struct skiplistdb *db);
        int (*repack)(struct skiplistdb *db);
        int (*cmp)(struct skiplistdb *db,
                   unsigned char *s1, int l1, unsigned char *s2, int l2);
};

struct skiplistdb {
        const char *name;
        DBType type;
        const struct skiplistdb_operations *op;
        unsigned int allocated:1;
        unsigned int initialised:1;
        void *priv;
};

const struct skiplistdb_operations base_ops;
#define SKIPLISTDB_INIT {NULL, INVALID_DB, &base_ops, 0, NULL }

int skiplistdb_init(DBType type, struct skiplistdb **db, struct txn **tid);
int skiplistdb_final(struct skiplistdb *db);
int skiplistdb_open(const char *dbdir, struct skiplistdb *db, int flags,
                    struct txn **tid);
int skiplistdb_close(struct skiplistdb *db);
int skiplistdb_sync(struct skiplistdb *db);
int skiplistdb_archive(struct skiplistdb *db, const struct str_array *fnames,
                       const char *dirname);
int skiplistdb_unlink(struct skiplistdb *db, const char *fname, int flags);
int skiplistdb_fetch(struct skiplistdb *db,
                     unsigned char *key, size_t keylen,
                     unsigned char **data, size_t *datalen,
                     struct txn **tid);
int skilistdb_fetchlock(struct skiplistdb *db,
                        unsigned char *key, size_t keylen,
                        unsigned char **data, size_t *datalen,
                        struct txn **tid);
int skiplistdb_fetchnext(struct skiplistdb *db,
                         unsigned char *key, size_t keylen,
                         unsigned char **foundkey, size_t *foundkeylen,
                         unsigned char **data, size_t *datalen,
                         struct txn **tid);
int skiplistdb_foreach(struct skiplistdb *db,
                       unsigned char *prefix, size_t prefixlen,
                       foreach_p *p, foreach_cb *cb, void *rock,
                       struct txn **tid);
int skiplistdb_add(struct skiplistdb *db,
                   unsigned char *key, size_t keylen,
                   unsigned char *data, size_t datalen,
                   struct txn **tid);
int skiplistdb_remove(struct skiplistdb *db,
                      unsigned char *key, size_t keylen,
                      struct txn **tid, int force);
int skiplistdb_store(struct skiplistdb *db,
                     unsigned char *key, size_t keylen,
                     unsigned char *data, size_t datalen,
                     struct txn **tid);
int skiplistdb_commit(struct skiplistdb *db,
                      struct txn **tid);
int skiplistdb_abort(struct skiplistdb *db,
                     struct txn **tid);
int skiplistdb_dump(struct skiplistdb *db,
                    DBDumpLevel level);
int skiplistdb_consistent(struct skiplistdb *db);
int skiplistdb_repack(struct skiplistdb *db);
int skiplistdb_cmp(struct skiplistdb *db,
                   unsigned char *s1, int l1, unsigned char *s2, int l2);


/* Utility functions for skiplistdb */
int skiplistdb_backends(void);

#endif  /* _SKIPLISTDB_H_ */
