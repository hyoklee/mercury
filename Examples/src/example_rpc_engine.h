/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include <mercury_bulk.h>
#include <mercury.h>
#include <mercury_macros.h>
#include <mercury_bulk.h>

/* example_rpc_engine: API of generic utilities and progress engine hooks that
 * are reused across many RPC functions.  init and finalize() manage a
 * dedicated thread that will drive all HG progress
 */

#ifndef EXAMPLE_RPC_ENGINE_H
#define EXAMPLE_RPC_ENGINE_H

void hg_engine_init(na_bool_t listen, const char* local_addr);
void hg_engine_finalize(void);
hg_class_t* hg_engine_get_class(void);
void hg_engine_addr_lookup(const char* name, na_cb_t cb, void *arg);
void hg_engine_create_handle(na_addr_t addr, hg_id_t id,
    hg_handle_t *handle);

#endif /* EXAMPLE_RPC_ENGINE_H */
