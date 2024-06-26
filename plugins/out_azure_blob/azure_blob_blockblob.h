/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2024 The Fluent Bit Authors
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

#ifndef AZURE_BLOB_BLOCKBLOB_H
#define AZURE_BLOB_BLOCKBLOB_H

#include <fluent-bit/flb_output_plugin.h>
#include "azure_blob.h"

flb_sds_t azb_block_blob_uri(struct flb_azure_blob *ctx, char *tag, char *blockid,
                             uint64_t ms);
char *azb_block_blob_id(uint64_t *ms);
int azb_block_blob_commit(struct flb_azure_blob *ctx, char *blockid, char *tag,
                          uint64_t ms);

#endif
