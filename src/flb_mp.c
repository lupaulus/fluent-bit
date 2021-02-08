/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2019-2020 The Fluent Bit Authors
 *  Copyright (C) 2015-2018 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fluent-bit/flb_info.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_mem.h>
#include <fluent-bit/flb_log.h>
#include <fluent-bit/flb_mp.h>
#include <fluent-bit/flb_slist.h>
#include <fluent-bit/flb_record_accessor.h>

#include <msgpack.h>
#include <mpack/mpack.h>

/* don't do this at home */
#define pack_uint16(buf, d) _msgpack_store16(buf, (uint16_t) d)
#define pack_uint32(buf, d) _msgpack_store32(buf, (uint32_t) d)

int flb_mp_count(const void *data, size_t bytes)
{
    int count = 0;
    mpack_reader_t reader;

    mpack_reader_init_data(&reader, (const char *) data, bytes);
    while (mpack_reader_remaining(&reader, NULL) > 0) {
        count++;
        mpack_discard(&reader);
    }

    mpack_reader_destroy(&reader);
    return count;
}

/* Adjust a mspack header buffer size */
void flb_mp_set_map_header_size(char *buf, int size)
{
    uint8_t h;
    char *tmp = buf;

    h = tmp[0];
    if (h >> 4 == 0x8) { /* 1000xxxx */
        *tmp = (uint8_t) 0x8 << 4 | ((uint8_t) size);
    }
    else if (h == 0xde) {
        tmp++;
        pack_uint16(tmp, size);
    }
    else if (h == 0xdf) {
        tmp++;
        pack_uint32(tmp, size);
    }
}

void flb_mp_set_array_header_size(char *buf, int size)
{
    uint8_t h;
    char *tmp = buf;

    h = tmp[0];
    if (h >> 4 == 0x9) { /* 1001xxxx */
        *tmp = (uint8_t) 0x9 << 4 | ((uint8_t) size);
    }
    else if (h == 0xdc) {
        tmp++;
        pack_uint16(tmp, size);
    }
    else if (h == 0xdd) {
        tmp++;
        pack_uint32(tmp, size);
    }
}

/*
 * msgpack-c requires to set the number of the entries in a map beforehand. For our
 * use case this adds some complexity, having developers to count all possible
 * entries that might be added.
 *
 * As a workaround and to avoid map's recomposition over and over, this simple API
 * allows to initialize the array header, 'register' new entries (as counters) and
 * finalize, upon finalization the proper array header size is adjusted.
 *
 * To make things easier, we make sure msgpack-c always register an array type of
 * 32 bits (identified by 0xdf, for number of entries >= 65536). Yes, for every
 * array using this API it will use 2 more bytes, not a big ideal. So whoever
 * uses this API, use it only if you don't know the exact number of entries to add.
 *
 * MANDATORY: make sure to always initialize, register every entry and finalize,
 * otherwise you will get a corrupted or incomplete msgpack buffer.
 *
 * Usage example
 * =============
 *
 *  struct flb_mp_map_header mh;
 *
 *  flb_mp_map_header_init(&mh, mp_pck);
 *
 *  -- First key/value entry --
 *  flb_mp_map_header_append(&mh);
 *  msgpack_pack_str(mp_pck, 4);
 *  msgpack_pack_str_body(mp_pck, "cool", 4);
 *  msgpack_pack_true(mp_pck);
 *
 *  -- Second key/value entry --
 *  flb_mp_map_header_append(&mh);
 *  msgpack_pack_str(mp_pck, 4);
 *  msgpack_pack_str_body(mp_pck, "slow", 4);
 *  msgpack_pack_false(mp_pck);
 *
 *  -- Finalize Map --
 *  flb_mp_map_header_end(&mh);
 */

static inline void mp_header_type_init(struct flb_mp_map_header *mh,
                                       msgpack_packer *mp_pck,
                                       int type)
{
    msgpack_sbuffer *mp_sbuf;

    mp_sbuf = (msgpack_sbuffer *) mp_pck->data;

    /* map sbuffer */
    mh->data = mp_pck->data;

    /* Reset entries */
    mh->entries = 0;

    /* Store the next byte available */
    mh->offset = mp_sbuf->size;
}

int flb_mp_map_header_init(struct flb_mp_map_header *mh, msgpack_packer *mp_pck)
{
    /* Initialize context for a map */
    mp_header_type_init(mh, mp_pck, FLB_MP_MAP);

    /*
     * Pack a map with size = 65536, so we force the underlaying msgpack-c
     * to use a 32 bit buffer size (0xdf), reference:
     *
     * - https://github.com/msgpack/msgpack/blob/master/spec.md#map-format-family
     */
    return msgpack_pack_map(mp_pck, 65536);
}

int flb_mp_array_header_init(struct flb_mp_map_header *mh, msgpack_packer *mp_pck)
{
    /* Initialize context for a map */
    mp_header_type_init(mh, mp_pck, FLB_MP_ARRAY);

    /*
     * Pack a map with size = 65536, so we force the underlaying msgpack-c
     * to use a 32 bit buffer size (0xdf), reference:
     *
     * - https://github.com/msgpack/msgpack/blob/master/spec.md#map-format-family
     */
    return msgpack_pack_array(mp_pck, 65536);
}


int flb_mp_map_header_append(struct flb_mp_map_header *mh)
{
    mh->entries++;
    return mh->entries;
}

int flb_mp_array_header_append(struct flb_mp_map_header *mh)
{
    mh->entries++;
    return mh->entries;
}

void flb_mp_map_header_end(struct flb_mp_map_header *mh)
{
    char *ptr;
    msgpack_sbuffer *mp_sbuf;

    mp_sbuf = mh->data;
    ptr = (char *) mp_sbuf->data + mh->offset;
    flb_mp_set_map_header_size(ptr, mh->entries);
}

void flb_mp_array_header_end(struct flb_mp_map_header *mh)
{
    char *ptr;
    msgpack_sbuffer *mp_sbuf;

    mp_sbuf = mh->data;
    ptr = (char *) mp_sbuf->data + mh->offset;
    flb_mp_set_array_header_size(ptr, mh->entries);
}

/*
 * Create an 'mp accessor' context: this context allows to create a list of
 * record accessor patterns based on a 'slist' context, where every slist string
 * buffer represents a key accessor.
 */
struct flb_mp_accessor *flb_mp_accessor_create(struct mk_list *slist_patterns)
{
    size_t size;
    struct mk_list *head;
    struct flb_slist_entry *entry;
    struct flb_record_accessor *ra;
    struct flb_mp_accessor *mpa;

    /* Allocate context */
    mpa = flb_calloc(1, sizeof(struct flb_mp_accessor));
    if (!mpa) {
        flb_errno();
        return NULL;
    }
    mk_list_init(&mpa->ra_list);

    mk_list_foreach(head, slist_patterns) {
        entry = mk_list_entry(head, struct flb_slist_entry, _head);

        /* Create the record accessor context */
        ra = flb_ra_create(entry->str, FLB_TRUE);
        if (!ra) {
            flb_error("[mp accessor] could not create entry for pattern '%s'",
                      entry->str);
            flb_mp_accessor_destroy(mpa);
            return NULL;
        }
        mk_list_add(&ra->_head, &mpa->ra_list);
    }

    if (mk_list_size(&mpa->ra_list) == 0) {
        return mpa;
    }

    size = sizeof(struct flb_mp_accessor_match) * mk_list_size(&mpa->ra_list);
    mpa->matches = flb_calloc(1, size);
    if (!mpa->matches) {
        flb_errno();
        flb_mp_accessor_destroy(mpa);
        return NULL;
    }

    return mpa;
}

static inline int accessor_key_find_match(struct flb_mp_accessor *mpa,
                                          msgpack_object *key)
{
    int i;
    int count;
    struct flb_mp_accessor_match *match;

    count = mk_list_size(&mpa->ra_list);
    for (i = 0; i < count; i++) {
        match = &mpa->matches[i];
        if (match->matched == FLB_FALSE) {
            continue;
        }

        if (match->start_key == key) {
            return i;
        }
    }

    return -1;
}

static inline int accessor_sub_pack(struct flb_mp_accessor_match *match,
                                    msgpack_packer *mp_pck,
                                    msgpack_object *key,
                                    msgpack_object *val)
{
    int i;
    int ret;
    msgpack_object *k;
    msgpack_object *v;
    struct flb_mp_map_header mh;

    if (match->key == key || match->key == val) {
        return FLB_FALSE;
    }

    if (key) {
        msgpack_pack_object(mp_pck, *key);
    }

    if (val->type == MSGPACK_OBJECT_MAP) {
        flb_mp_map_header_init(&mh, mp_pck);
        for (i = 0; i < val->via.map.size; i++) {
            k = &val->via.map.ptr[i].key;
            v = &val->via.map.ptr[i].val;

            ret = accessor_sub_pack(match, mp_pck, k, v);
            if (ret == FLB_TRUE) {
                flb_mp_map_header_append(&mh);
            }
        }
        flb_mp_map_header_end(&mh);
    }
    else if (val->type == MSGPACK_OBJECT_ARRAY) {
        flb_mp_array_header_init(&mh, mp_pck);
        for (i = 0; i < val->via.array.size; i++) {
            v = &val->via.array.ptr[i];
            ret = accessor_sub_pack(match, mp_pck, NULL, v);
            if (ret == FLB_TRUE) {
                flb_mp_array_header_append(&mh);
            }
        }
        flb_mp_array_header_end(&mh);
    }
    else {
        msgpack_pack_object(mp_pck, *val);
    }

    return FLB_TRUE;
}

/*
 * Remove keys or nested keys from a map. It compose the final result in a
 * new buffer. On error, it returns -1, if the map was modified it returns FLB_TRUE,
 * if no modification was required it returns FLB_FALSE.
 */
int flb_mp_accessor_keys_remove(struct flb_mp_accessor *mpa,
                                msgpack_object *map,
                                void **out_buf, size_t *out_size)
{
    int i;
    int ret;
    int rule_id = 0;
    int matches = 0;
    msgpack_object *key;
    msgpack_object *val;
    msgpack_object *s_key;
    msgpack_object *o_key;
    msgpack_object *o_val;
    struct mk_list *head;
    struct flb_record_accessor *ra;
    struct flb_mp_accessor_match *match;
    struct flb_mp_map_header mh;
    msgpack_sbuffer mp_sbuf;
    msgpack_packer mp_pck;

    if (map->via.map.size == 0) {
        return FLB_FALSE;
    }

    mk_list_foreach(head, &mpa->ra_list) {
        ra = mk_list_entry(head, struct flb_record_accessor, _head);

        /* Apply the record accessor rule against the map */
        ret = flb_ra_get_kv_pair(ra, *map, &s_key, &o_key, &o_val);
        if (ret == 0) {
            /* There is a match, register in the matches table */
            match = &mpa->matches[rule_id];
            match->matched = FLB_TRUE;
            match->start_key = s_key;        /* Initial key path that matched */
            match->key = o_key;              /* Final key that matched */
            match->val = o_val;              /* Final value */
            match->ra = ra;                  /* Record accessor context */
            matches++;
        }
        rule_id++;
    }

    /* If no matches, no modifications were made */
    if (matches == 0) {
        return FLB_FALSE;
    }

    /* Some rules matched, compose a new outgoing buffer */
    msgpack_sbuffer_init(&mp_sbuf);
    msgpack_packer_init(&mp_pck, &mp_sbuf, msgpack_sbuffer_write);

    /* Initialize map */
    flb_mp_map_header_init(&mh, &mp_pck);

    for (i = 0; i < map->via.map.size; i++) {
        key = &map->via.map.ptr[i].key;
        val = &map->via.map.ptr[i].val;

        /*
         * For every entry on the path, check if we should do a step-by-step
         * repackaging or just pack the whole object.
         *
         * Just check: does this 'key' exists on any path of the record
         * accessor patterns ?
         *
         * Find if the active key in the map, matches an accessor rule, if
         * if match we get the match id as return value, otherwise -1.
         */
        ret = accessor_key_find_match(mpa, key);
        if (ret == -1) {
            /* No matches, it's ok to pack the kv pair */
            flb_mp_map_header_append(&mh);
            msgpack_pack_object(&mp_pck, *key);
            msgpack_pack_object(&mp_pck, *val);
        }
        else {
            /* The key has a match. Now we do a step-by-step packaging */
            match = &mpa->matches[ret];
            ret = accessor_sub_pack(match, &mp_pck, key, val);
            if (ret == FLB_TRUE) {
                flb_mp_map_header_append(&mh);
            }
        }
    }
    flb_mp_map_header_end(&mh);

    *out_buf = mp_sbuf.data;
    *out_size = mp_sbuf.size;

    return FLB_TRUE;
}

void flb_mp_accessor_destroy(struct flb_mp_accessor *mpa)
{
    struct mk_list *tmp;
    struct mk_list *head;
    struct flb_record_accessor *ra;

    if (!mpa) {
        return;
    }

    mk_list_foreach_safe(head, tmp, &mpa->ra_list) {
        ra = mk_list_entry(head, struct flb_record_accessor, _head);
        mk_list_del(&ra->_head);
        flb_ra_destroy(ra);
    }

    if (mpa->matches) {
        flb_free(mpa->matches);
    }
    flb_free(mpa);
}
