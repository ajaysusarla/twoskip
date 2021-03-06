/*
 * zeroskip-record.c : zeroskip record management
 *
 * This file is part of skiplistdb.
 *
 * skiplistdb is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 *
 */

#include "zeroskip.h"
#include "zeroskip-priv.h"

#include <zlib.h>

/**
 ** Private functions
 **/

/* Caller should free buf
 */
static int zs_prepare_key_buf(unsigned char *key, size_t keylen,
                              unsigned char **buf, size_t *buflen)
{
        int ret = SDB_OK;
        unsigned char *kbuf;
        size_t kbuflen, finalkeylen, pos = 0;
        enum record_t type;

        kbuflen = ZS_KEY_BASE_REC_SIZE;
        type = REC_TYPE_KEY;

        if (keylen > MAX_SHORT_KEY_LEN)
                type |= REC_TYPE_LONG;

        /* Minimum buf size */
        finalkeylen = roundup64bits(keylen);
        kbuflen += finalkeylen;

        kbuf = xcalloc(1, kbuflen);

        if (type == REC_TYPE_KEY) {
                /* If it is a short key, the first 3 fields make up 64 bits */
                uint64_t val;
                val = ((uint64_t)kbuflen & ((1ULL << 40) - 1)); /* Val offset */
                val |= ((uint64_t)keylen << 40);     /* Key length */
                val |= ((uint64_t)type << 56);       /* Type */
                write_be64(kbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended Value offset */
                pos += sizeof(uint64_t);

        } else {
                /* A long key has the type followed by 56 bits of nothing */
                uint64_t val;
                val = ((uint64_t)type & ((1ULL << 56) - 1));
                write_be64(kbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, keylen);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, kbuflen);    /* Extended Value offset */
                pos += sizeof(uint64_t);
        }

        /* the key */
        memcpy(kbuf + pos, key, keylen);
        pos += keylen;

        *buflen = kbuflen;
        *buf = kbuf;

        return ret;
}

/* Caller should free buf
 */
static int zs_prepare_val_buf(unsigned char *val, size_t vallen,
                              unsigned char **buf, size_t *buflen)
{
        int ret = SDB_OK;
        unsigned char *vbuf;
        size_t vbuflen, finalvallen, pos = 0;
        enum record_t type;

        vbuflen = ZS_VAL_BASE_REC_SIZE;
        type = REC_TYPE_VALUE;

        if (vallen > MAX_SHORT_VAL_LEN)
                type |= REC_TYPE_LONG;

        /* Minimum buf size */
        finalvallen = roundup64bits(vallen);

        vbuflen += finalvallen;

        vbuf = xcalloc(1, vbuflen);

        if (type == REC_TYPE_VALUE) {
                /* The first 3 fields in a short key make up 64 bits */
                uint64_t val = 0;
                val = ((uint64_t)vallen & ((1UL << 32) - 1));  /* Val length */
                val |= ((uint64_t)type << 56);                 /* Type */
                write_be64(vbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(vbuf + pos, 0ULL);     /* Extended length */
                pos += sizeof(uint64_t);
        } else {
                /* A long val has the type followed by 56 bits of nothing */
                uint64_t val;
                val = ((uint64_t)type & ((1UL << 56) - 1));
                write_be64(vbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(vbuf + pos, vallen);
                pos += sizeof(uint64_t);
        }

        /* the value */
        memcpy(vbuf + pos, val, vallen);

        *buflen = vbuflen;
        *buf = vbuf;

        return ret;
}

/* Caller should free buf
 */
static int zs_prepare_delete_key_buf(unsigned char *key, size_t keylen,
                                     unsigned char **buf, size_t *buflen)
{
        int ret = SDB_OK;
        unsigned char *kbuf;
        size_t kbuflen, finalkeylen, pos = 0;
        uint64_t val;
        enum record_t type = REC_TYPE_DELETED;

        kbuflen = ZS_KEY_BASE_REC_SIZE;

        /* Minimum buf size */
        finalkeylen = roundup64bits(keylen);
        kbuflen += finalkeylen;

        kbuf = xcalloc(1, kbuflen);

        if (keylen <= MAX_SHORT_KEY_LEN) {
                val = ((uint64_t)0ULL & ((1ULL << 40) - 1));
                val |= ((uint64_t)keylen << 40);     /* Key length */
                val |= ((uint64_t)type << 56);       /* Type */
                write_be64(kbuf + pos, val);
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);     /* Extended Value offset */
                pos += sizeof(uint64_t);
        } else {
                val = ((uint64_t)type & ((1ULL << 56) - 1));
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, keylen);     /* Extended length */
                pos += sizeof(uint64_t);
                write_be64(kbuf + pos, 0ULL);       /* Extended Value offset */
                pos += sizeof(uint64_t);
        }

        /* the key */
        memcpy(kbuf + pos, key, keylen);
        pos += keylen;

        *buflen = kbuflen;
        *buf = kbuf;

        return ret;
}

static int zs_read_key_rec(struct zsdb_file *f, size_t *offset,
                           struct zs_key *key)
{
        unsigned char *bptr;
        unsigned char *fptr;
        uint64_t data;

        bptr = f->mf->ptr;
        fptr = bptr + *offset;

        data = read_be64(fptr);
        key->base.type = data >> 56;

        if (key->base.type == REC_TYPE_KEY) {
                key->base.slen = data >> 40;
                key->base.sval_offset = data & ((1ULL >> 40) - 1);
                key->base.llen = 0;
                key->base.lval_offset = 0;
        } else if (key->base.type == REC_TYPE_LONG_KEY) {
                key->base.slen = 0;
                key->base.sval_offset = 0;
                key->base.llen = read_be64(fptr + 8);
                key->base.lval_offset = read_be64(fptr + 16);
        }

        key->data = fptr + ZS_KEY_BASE_REC_SIZE;

        return SDB_OK;
}

static int zs_read_val_rec(struct zsdb_file *f, size_t *offset,
                           struct zs_val *val)
{
        unsigned char *bptr;
        unsigned char *fptr;
        uint64_t data;

        bptr = f->mf->ptr;
        fptr = bptr + *offset;

        data = read_be64(fptr);
        val->base.type = data >> 56;

        if (val->base.type == REC_TYPE_VALUE) {
                val->base.slen = data & ((1UL >> 32) - 1);
                val->base.nullpad = 0;
                val->base.llen = 0;
        } else if (val->base.type == REC_TYPE_LONG_VALUE) {
                val->base.slen = 0;
                val->base.nullpad = 0;
                val->base.llen = read_be64(fptr + 8);
        }

        val->data = fptr + ZS_VAL_BASE_REC_SIZE;

        return SDB_OK;
}

static int zs_read_key_val_record(struct zsdb_file *f, size_t *offset,
                                  foreach_cb *cb, void *cbdata)
{
        int ret = SDB_OK;
        struct zs_key key;
        struct zs_val val;
        size_t keylen, vallen;


        zs_read_key_rec(f, offset, &key);

        *offset += (key.base.type == REC_TYPE_KEY) ?
                key.base.sval_offset : key.base.lval_offset;

        zs_read_val_rec(f, offset, &val);

        keylen = (key.base.type == REC_TYPE_KEY) ?
                key.base.slen : key.base.llen;
        vallen = (val.base.type == REC_TYPE_VALUE) ?
                val.base.slen : val.base.llen;

        *offset += ZS_VAL_BASE_REC_SIZE +
                roundup64bits(vallen);

        if (cb) {
                cb(cbdata, key.data, keylen, val.data, vallen);
        }
        return ret;
}

static int zs_read_one_active_record(struct zsdb_file *f, size_t *offset,
                                     foreach_cb *cb, void *cbdata)
{
        unsigned char *bptr, *fptr;
        uint64_t data;
        enum record_t rectype;

        if (!f->is_open)
                return SDB_IOERROR;

        bptr = f->mf->ptr;
        fptr = bptr + *offset;

        data = read_be64(fptr);
        rectype = data >> 56;

        switch(rectype) {
        case REC_TYPE_KEY:
        case REC_TYPE_LONG_KEY:
                zs_read_key_val_record(f, offset, cb, cbdata);
                break;
        case REC_TYPE_VALUE:
        case REC_TYPE_LONG_VALUE:
                printf("Control shouldn't reach here.\n");
                break;
        case REC_TYPE_COMMIT:
                *offset = *offset + ZS_SHORT_COMMIT_REC_SIZE;
                break;
        case REC_TYPE_LONG_COMMIT:
                *offset = *offset + ZS_LONG_COMMIT_REC_SIZE;
                break;
        case REC_TYPE_2ND_HALF_COMMIT:
                break;
        case REC_TYPE_FINAL:
                break;
        case REC_TYPE_LONG_FINAL:
                break;
        case REC_TYPE_DELETED:
        {
                struct zs_key key;
                zs_read_key_rec(f, offset, &key);
                printf("Deleted key!\n");
        }
                break;
        case REC_TYPE_UNUSED:
                break;
        default:
                break;
        }

        return SDB_OK;
}

/**
 ** External functions
 **/
int zs_write_keyval_record(struct zsdb_file *f,
                           unsigned char *key, size_t keylen,
                           unsigned char *data, size_t datalen)
{
        int ret = SDB_OK;
        size_t keybuflen, valbuflen;
        unsigned char *keybuf, *valbuf;
        size_t mfsize, nbytes;

        assert(f);

        if (!f->is_open)
                return SDB_INTERNAL;

        ret = zs_prepare_key_buf(key, keylen, &keybuf, &keybuflen);
        if (ret != SDB_OK) {
                return SDB_IOERROR;
        }

        ret = zs_prepare_val_buf(data, datalen, &valbuf, &valbuflen);
        if (ret != SDB_OK) {
                return SDB_IOERROR;
        }

        /* Get the current mappedfile size */
        ret = mappedfile_size(&f->mf, &mfsize);
        if (ret) {
                fprintf(stderr, "Could not get mappedfile size\n");
                goto done;
        }

        /* write key buffer */
        ret = mappedfile_write(&f->mf, (void *)keybuf, keybuflen, &nbytes);
        if (ret) {
                fprintf(stderr, "Error writing key\n");
                ret = SDB_IOERROR;
                goto done;
        }

        /* assert(nbytes == keybuflen); */

        /* write value buffer */
        ret = mappedfile_write(&f->mf, (void *)valbuf, valbuflen, &nbytes);
        if (ret) {
                fprintf(stderr, "Error writing key\n");
                ret = SDB_IOERROR;
                goto done;
        }

        /* assert(nbytes == valbuflen); */

        /* If we failed writing the value buffer, then restore the db file to
         * the original size we had before updating */
        if (ret != SDB_OK) {
                mappedfile_truncate(&f->mf, mfsize);
        }

        /* Flush the change to disk */
        ret = mappedfile_flush(&f->mf);
        if (ret) {
                /* TODO: try again before giving up */
                fprintf(stderr, "Error flushing data to disk.\n");
                ret = SDB_IOERROR;
                goto done;
        }

done:
        xfree(keybuf);
        xfree(valbuf);

        return ret;
}

int zs_write_commit_record(struct zsdb_file *f)
{
        int ret = SDB_OK;
        size_t buflen, nbytes, pos = 0;
        unsigned char buf[24], *ptr;
        uint32_t crc;

        assert(f);
        if (!f->is_open)
                return SDB_INTERNAL;

        memset(&buf, 0, sizeof(buf));
        ptr = buf;

        if (f->mf->crc32_data_len > MAX_SHORT_VAL_LEN) { /* long commit */
                uint32_t lccrc;
                uint64_t val = 0;
                struct zs_long_commit lc;

                lc.type1 = REC_TYPE_LONG_COMMIT;
                lc.length = f->mf->crc32_data_len;
                lc.type2 = REC_TYPE_LONG_COMMIT;

                /* Compute CRC32 */
                lccrc = crc32(0L, Z_NULL, 0);
                lccrc = crc32(lccrc, (void *)&lc,
                              sizeof(struct zs_long_commit) - sizeof(uint32_t));
                crc = crc32_end(&f->mf);
                lc.crc32 = crc32_combine(crc, lccrc, sizeof(uint32_t));

                /* Create long commit */
                val = ((uint64_t)lc.type1 & ((1ULL << 56) - 1));
                write_be64(ptr + pos, val);
                pos += sizeof(uint64_t);

                write_be64(ptr + pos, lc.length);
                pos += sizeof(uint64_t);


                val = 0;
                val = ((uint64_t)lc.crc32 & ((1UL << 32) - 1)); /* crc */
                val |= ((uint64_t)lc.type2 << 56);               /* type */
                write_be64(ptr + pos, val);
                pos += sizeof(uint64_t);

                buflen = ZS_LONG_COMMIT_REC_SIZE;
        } else {                                         /* short commit */
                uint64_t val = 0;
                uint32_t sccrc;
                struct zs_short_commit sc;

                sc.type = REC_TYPE_COMMIT;
                sc.length = f->mf->crc32_data_len;

                /* Compute CRC32 */
                sccrc = crc32(0L, Z_NULL, 0);
                sccrc = crc32(sccrc, (void *)&sc,
                              sizeof(struct zs_short_commit) - sizeof(uint32_t));
                crc = crc32_end(&f->mf);
                sc.crc32 = crc32_combine(crc, sccrc, sizeof(uint32_t));

                val = ((uint64_t)sc.crc32 & ((1UL << 32) - 1)); /* crc */
                val |= ((uint64_t)sc.length << 32);             /* length */
                val |= ((uint64_t)sc.type << 56);               /* type */
                write_be64(ptr + pos, val);
                pos += sizeof(uint64_t);

                buflen = ZS_SHORT_COMMIT_REC_SIZE;
        }

        ret = mappedfile_write(&f->mf, (void *)buf, buflen, &nbytes);
        if (ret) {
                fprintf(stderr, "Error writing commit record.\n");
                ret = SDB_IOERROR;
                goto done;
        }

        /* assert(nbytes == buflen); */

        /* Flush the change to disk */
        ret = mappedfile_flush(&f->mf);
        if (ret) {
                /* TODO: try again before giving up */
                fprintf(stderr, "Error flushing commit record to disk.\n");
                ret = SDB_IOERROR;
                goto done;
        }

done:
        return ret;
}

int zs_write_delete_record(struct zsdb_file *f,
                           unsigned char *key, size_t keylen)
{
        int ret = SDB_OK;
        unsigned char *dbuf;
        size_t dbuflen, mfsize, nbytes;

        assert(f);

        if (!f->is_open)
                return SDB_INTERNAL;

        ret = zs_prepare_delete_key_buf(key, keylen, &dbuf, &dbuflen);
        if (ret != SDB_OK) {
                return SDB_IOERROR;
        }

        /* Get the current mappedfile size */
        ret = mappedfile_size(&f->mf, &mfsize);
        if (ret) {
                fprintf(stderr, "Could not get mappedfile size\n");
                goto done;
        }

        /* write delete buffer */
        ret = mappedfile_write(&f->mf, (void *)dbuf, dbuflen, &nbytes);
        if (ret) {
                fprintf(stderr, "Error writing key\n");
                ret = SDB_IOERROR;
                goto done;
        }

        /* assert(nbytes == keybuflen); */

        /* If we failed writing the delete buffer, then restore the db file to
         * the original size we had before updating */
        if (ret != SDB_OK) {
                mappedfile_truncate(&f->mf, mfsize);
        }

        /* Flush the change to disk */
        ret = mappedfile_flush(&f->mf);
        if (ret) {
                /* TODO: try again before giving up */
                fprintf(stderr, "Error flushing data to disk.\n");
                ret = SDB_IOERROR;
                goto done;
        }

done:
        xfree(dbuf);
        return ret;
}

int zs_active_record_foreach(struct zsdb_file *f, foreach_cb *cb, void *cbdata)
{
        int ret = SDB_OK;
        size_t dbsize = 0, offset = ZS_HDR_SIZE;

        mappedfile_size(&f->mf, &dbsize);
        if (dbsize == 0 || dbsize <= ZS_HDR_SIZE) {
                fprintf(stderr, "No records in zeroskip DB\n");
                return SDB_IOERROR;
        }

        while (offset < dbsize) {
                ret = zs_read_one_active_record(f, &offset, cb, cbdata);
        }

        return ret;
}
