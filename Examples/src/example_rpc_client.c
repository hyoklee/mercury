/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

#include "example_rpc_engine.h"
#include "example_rpc.h"

/* This is an example client program that issues 4 concurrent RPCs, each of
 * which includes a bulk transfer driven by the server.
 *
 * This example code is callback driven (one callback per rpc in this case).
 * The callback model could be avoided using the hg_request API
 * which provides a mechanism to wait for completion of an RPC or a subset of
 * RPCs.  This approach would have two drawbacks, however:
 * - would require a dedicated thread per concurrent RPC
 * - unclear how it would integrate with server-side activity if it were used
 *   in that scenario (for server-to-server communication)
 */

static int done = 0;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
static hg_id_t my_rpc_id;

static hg_return_t my_rpc_cb(const struct hg_cb_info *info);
static void run_my_rpc(int value);
static na_return_t lookup_cb(const struct na_cb_info *callback_info);

/* struct used to carry state of overall operation across callbacks */
struct my_rpc_state
{
    int value;
    hg_size_t size;
    void* buffer;
    hg_bulk_t bulk_handle;
    hg_handle_t handle;
};

int main(void)
{
    int i;

    /* start mercury and register RPC */

    /* NOTE: the address here is mainly used to identify the transport; this
     * is a client and will not be listening for requests.
     */
    hg_engine_init(NA_FALSE, "tcp://localhost:1234");
    my_rpc_id = my_rpc_register();

    /* issue 4 RPCs (these will proceed concurrently using callbacks) */
    for(i=0; i<4; i++)
        run_my_rpc(i);

    /* wait for callbacks to finish */
    pthread_mutex_lock(&done_mutex);
    while(done < 4)
        pthread_cond_wait(&done_cond, &done_mutex);
    pthread_mutex_unlock(&done_mutex);

    /* shut down */
    hg_engine_finalize();

    return(0);
}


static void run_my_rpc(int value)
{
    int *heap_value;

    heap_value = malloc(sizeof(*heap_value));
    assert(heap_value);
    *heap_value = value;

    /* address lookup.  This is an async operation as well so we continue in
     * a callback from here.
     */
    hg_engine_addr_lookup("tcp://localhost:1234", lookup_cb, heap_value);

    return;
}

static na_return_t lookup_cb(const struct na_cb_info *callback_info)
{
    na_addr_t svr_addr = callback_info->info.lookup.addr;
    my_rpc_in_t in;
    struct hg_info *hgi;
    int ret;
    struct my_rpc_state *my_rpc_state_p;

    assert(callback_info->ret == 0);

    /* set up state structure */
    my_rpc_state_p = malloc(sizeof(*my_rpc_state_p));
    my_rpc_state_p->size = 512;
    /* This includes allocating a src buffer for bulk transfer */
    my_rpc_state_p->buffer = calloc(1, 512);
    assert(my_rpc_state_p->buffer);
    sprintf((char*)my_rpc_state_p->buffer, "Hello world!\n");
    my_rpc_state_p->value = *((int*)callback_info->arg);
    free(callback_info->arg);

    /* create create handle to represent this rpc operation */
    hg_engine_create_handle(svr_addr, my_rpc_id, &my_rpc_state_p->handle);

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(my_rpc_state_p->handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &my_rpc_state_p->buffer, &my_rpc_state_p->size,
        HG_BULK_READ_ONLY, &in.bulk_handle);
    my_rpc_state_p->bulk_handle = in.bulk_handle;
    assert(ret == 0);

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above. 
     */ 
    in.input_val = my_rpc_state_p->value;
    ret = HG_Forward(my_rpc_state_p->handle, my_rpc_cb, my_rpc_state_p, &in);
    assert(ret == 0);
    return(NA_SUCCESS);
}

/* callback triggered upon receipt of rpc response */
static hg_return_t my_rpc_cb(const struct hg_cb_info *info)
{
    my_rpc_out_t out;
    int ret;
    struct my_rpc_state *my_rpc_state_p = info->arg;

    assert(info->ret == HG_SUCCESS);

    /* decode response */
    ret = HG_Get_output(info->info.forward.handle, &out);
    assert(ret == 0);

    printf("Got response ret: %d\n", out.ret);

    /* clean up resources consumed by this rpc */
    HG_Bulk_free(my_rpc_state_p->bulk_handle);
    HG_Free_output(info->info.forward.handle, &out);
    HG_Destroy(info->info.forward.handle);
    free(my_rpc_state_p->buffer);
    free(my_rpc_state_p);

    /* signal to main() that we are done */
    pthread_mutex_lock(&done_mutex);
    done++;
    pthread_cond_signal(&done_cond);
    pthread_mutex_unlock(&done_mutex);

    return(HG_SUCCESS);
}
