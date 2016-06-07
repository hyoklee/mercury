/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include "mercury_bulk.h"
#include "mercury_error.h"
#include "mercury_private.h"

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/
#define HG_BULK_MIN(a, b) \
    (a < b) ? a : b

/* Remove warnings when plugin does not use callback arguments */
#if defined(__cplusplus)
    #define HG_BULK_UNUSED
#elif defined(__GNUC__) && (__GNUC__ >= 4)
    #define HG_BULK_UNUSED __attribute__((unused))
#else
    #define HG_BULK_UNUSED
#endif

/************************************/
/* Local Type and Struct Definition */
/************************************/

/* HG Bulk op id */
struct hg_bulk_op_id {
    struct hg_class *hg_class;            /* HG class */
    hg_context_t *context;                /* Context */
    hg_cb_t callback;                     /* Callback */
    void *arg;                            /* Callback arguments */
    hg_atomic_int32_t completed;          /* Operation completed TODO needed ? */
    hg_atomic_int32_t canceled;           /* Operation canceled */
    unsigned int op_count;                /* Number of ongoing operations */
    hg_atomic_int32_t op_completed_count; /* Number of operations completed */
    hg_bulk_op_t op;                      /* Operation type */
    struct hg_bulk *hg_bulk_origin;       /* Origin handle */
    struct hg_bulk *hg_bulk_local;        /* Local handle */
    na_op_id_t *na_op_ids ;               /* NA operations IDs */
};

/* Segment used to transfer data and map to NA layer */
struct hg_bulk_segment {
    hg_ptr_t address; /* address of the segment */
    hg_size_t size;   /* size of the segment in bytes */
};

/* Wrapper on top of NA layer */
typedef na_return_t (*na_bulk_op_t)(
        na_class_t      *na_class,
        na_context_t    *context,
        na_cb_t          callback,
        void            *arg,
        na_mem_handle_t  local_mem_handle,
        na_ptr_t         local_address,
        na_offset_t      local_offset,
        na_mem_handle_t  remote_mem_handle,
        na_ptr_t         remote_address,
        na_offset_t      remote_offset,
        na_size_t        data_size,
        na_addr_t        remote_addr,
        na_op_id_t      *op_id
        );

/* Note to self, get_serialize_size may be updated accordingly */
struct hg_bulk {
    struct hg_class *hg_class;           /* HG class */
    hg_size_t total_size;                /* Total size of data abstracted */
    hg_uint32_t segment_count;           /* Number of segments */
    struct hg_bulk_segment *segments;    /* Array of segments */
    na_mem_handle_t *segment_handles;    /* Array of NA memory handles */
    hg_bool_t segment_published;         /* NA memory handles published */
    hg_bool_t segment_alloc;             /* Allocated memory to mirror data */
    hg_uint8_t flags;                    /* Permission flags */
    hg_bool_t eager_mode;                /* Eager transfer */
    hg_uint32_t ref_count;               /* Reference count */
};

/********************/
/* Local Prototypes */
/********************/

/**
 * Create handle.
 */
static hg_return_t
hg_bulk_create(
        struct hg_class *hg_class,
        hg_uint32_t count,
        void **buf_ptrs,
        const hg_size_t *buf_sizes,
        hg_uint8_t flags,
        struct hg_bulk **hg_bulk_ptr
        );

/**
 * Free handle.
 */
static hg_return_t
hg_bulk_free(
        struct hg_bulk *hg_bulk
        );

/**
 * Get info for bulk transfer.
 */
static void
hg_bulk_offset_translate(
        struct hg_bulk *hg_bulk,
        hg_size_t offset,
        hg_uint32_t *segment_start_index,
        hg_size_t *segment_start_offset
        );

/**
 * Access bulk handle and get segment addresses/sizes.
 */
static void
hg_bulk_access(
        struct hg_bulk *hg_bulk,
        hg_size_t offset,
        hg_size_t size,
        hg_uint8_t flags,
        hg_uint32_t max_count,
        void **buf_ptrs,
        hg_size_t *buf_sizes,
        hg_uint32_t *actual_count
        );

/**
 * Transfer callback.
 */
static na_return_t
hg_bulk_transfer_cb(
        const struct na_cb_info *callback_info
        );

/**
 * Transfer data pieces (private).
 */
static hg_return_t
hg_bulk_transfer_pieces(
        na_bulk_op_t na_bulk_op,
        na_addr_t origin_addr,
        struct hg_bulk *hg_bulk_origin,
        hg_size_t origin_segment_start_index,
        hg_size_t origin_segment_start_offset,
        struct hg_bulk *hg_bulk_local,
        hg_size_t local_segment_start_index,
        hg_size_t local_segment_start_offset,
        hg_size_t size,
        struct hg_bulk_op_id *hg_bulk_op_id,
        unsigned int *na_op_count
        );

/**
 * Transfer data.
 */
static hg_return_t
hg_bulk_transfer(
        hg_context_t *context,
        hg_cb_t callback,
        void *arg,
        hg_bulk_op_t op,
        na_addr_t origin_addr,
        struct hg_bulk *hg_bulk_origin,
        hg_size_t origin_offset,
        struct hg_bulk *hg_bulk_local,
        hg_size_t local_offset,
        hg_size_t size,
        hg_op_id_t *op_id
        );

/**
 * Complete operation ID.
 */
static hg_return_t
hg_bulk_complete(
        struct hg_bulk_op_id *hg_bulk_op_id
        );

/**
 * Add entry to completion queue.
 */
extern hg_return_t
hg_core_completion_add(
        struct hg_context *context,
        struct hg_completion_entry *hg_completion_entry
        );

/**
 * Trigger callback from bulk op ID.
 */
hg_return_t
hg_bulk_trigger_entry(
        struct hg_bulk_op_id *hg_bulk_op_id
        );

/**
 * NA_Put wrapper
 */
static HG_INLINE na_return_t
hg_bulk_na_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t local_mem_handle,
    na_ptr_t HG_BULK_UNUSED local_address, na_offset_t local_offset,
    na_mem_handle_t remote_mem_handle, na_ptr_t HG_BULK_UNUSED remote_address,
    na_offset_t remote_offset, na_size_t data_size, na_addr_t remote_addr,
    na_op_id_t *op_id)
{
    return NA_Put(na_class, context, callback, arg, local_mem_handle,
            local_offset, remote_mem_handle, remote_offset, data_size,
            remote_addr, op_id);
}

/**
 * NA_Get wrapper
 */
static HG_INLINE na_return_t
hg_bulk_na_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
    void *arg, na_mem_handle_t local_mem_handle,
    na_ptr_t HG_BULK_UNUSED local_address, na_offset_t local_offset,
    na_mem_handle_t remote_mem_handle, na_ptr_t HG_BULK_UNUSED remote_address,
    na_offset_t remote_offset, na_size_t data_size, na_addr_t remote_addr,
    na_op_id_t *op_id)
{
    return NA_Get(na_class, context, callback, arg, local_mem_handle,
            local_offset, remote_mem_handle, remote_offset, data_size,
            remote_addr, op_id);
}

/**
 * NA_Cancel wrapper
 */
static HG_INLINE na_return_t
hg_bulk_na_cancel(na_class_t *na_class, na_context_t *context, na_cb_t callback,
               void *arg, na_mem_handle_t local_mem_handle,
               na_ptr_t HG_BULK_UNUSED local_address, na_offset_t local_offset,
               na_mem_handle_t remote_mem_handle, na_ptr_t HG_BULK_UNUSED remote_address,
               na_offset_t remote_offset, na_size_t data_size, na_addr_t remote_addr,
               na_op_id_t *op_id)
{
    return NA_Cancel(na_class, context, op_id);
}


/**
 * Memcpy
 */
static HG_INLINE na_return_t
hg_bulk_memcpy_put(na_class_t HG_BULK_UNUSED *na_class,
    na_context_t HG_BULK_UNUSED *context, na_cb_t callback, void *arg,
    na_mem_handle_t HG_BULK_UNUSED local_mem_handle, na_ptr_t local_address,
    na_offset_t local_offset, na_mem_handle_t HG_BULK_UNUSED remote_mem_handle,
    na_ptr_t remote_address, na_offset_t remote_offset, na_size_t data_size,
    na_addr_t HG_BULK_UNUSED remote_addr, na_op_id_t HG_BULK_UNUSED *op_id)
{
    struct na_cb_info na_cb_info;

    na_cb_info.arg = arg;
    na_cb_info.ret = NA_SUCCESS;
    memcpy((void *) (remote_address + remote_offset),
            (const void *) (local_address + local_offset), data_size);
    return callback(&na_cb_info);
}

/**
 * Memcpy
 */
static HG_INLINE na_return_t
hg_bulk_memcpy_get(na_class_t HG_BULK_UNUSED *na_class,
    na_context_t HG_BULK_UNUSED *context, na_cb_t callback, void *arg,
    na_mem_handle_t HG_BULK_UNUSED local_mem_handle, na_ptr_t local_address,
    na_offset_t local_offset, na_mem_handle_t HG_BULK_UNUSED remote_mem_handle,
    na_ptr_t remote_address, na_offset_t remote_offset, na_size_t data_size,
    na_addr_t HG_BULK_UNUSED remote_addr, na_op_id_t HG_BULK_UNUSED *op_id)
{
    struct na_cb_info na_cb_info;

    na_cb_info.arg = arg;
    na_cb_info.ret = NA_SUCCESS;
    memcpy((void *) (local_address + local_offset),
            (const void *) (remote_address + remote_offset), data_size);
    return callback(&na_cb_info);
}




/*******************/
/* Local Variables */
/*******************/

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_bulk_create(struct hg_class *hg_class, hg_uint32_t count,
    void **buf_ptrs, const hg_size_t *buf_sizes, hg_uint8_t flags,
    struct hg_bulk **hg_bulk_ptr)
{
    struct hg_bulk *hg_bulk = NULL;
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;
    unsigned int i;

    hg_bulk = (struct hg_bulk *) malloc(sizeof(struct hg_bulk));
    if (!hg_bulk) {
        HG_LOG_ERROR("Could not allocate handle");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    hg_bulk->hg_class = hg_class;
    hg_bulk->total_size = 0;
    hg_bulk->segment_count = count;
    hg_bulk->segments = NULL;
    hg_bulk->segment_handles = NULL;
    hg_bulk->segment_published = HG_FALSE;
    hg_bulk->segment_alloc = (!buf_ptrs);
    hg_bulk->flags = flags;
    hg_bulk->eager_mode = HG_FALSE;
    hg_bulk->ref_count = 1;

    /* Allocate segment sizes */
    hg_bulk->segments = (struct hg_bulk_segment *) malloc(hg_bulk->segment_count
            * sizeof(struct hg_bulk_segment));
    if (!hg_bulk->segments) {
        HG_LOG_ERROR("Could not allocate segment array");
        ret = HG_NOMEM_ERROR;
        goto done;
    }

    /* Allocate segment handles */
    hg_bulk->segment_handles = (na_mem_handle_t *) malloc(hg_bulk->segment_count
            * sizeof(na_mem_handle_t));
    if (!hg_bulk->segment_handles) {
        HG_LOG_ERROR("Could not allocate mem handle array");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    for (i = 0; i < hg_bulk->segment_count; i++) {
        hg_bulk->segment_handles[i] = NA_MEM_HANDLE_NULL;
    }

    /* TODO should check that part */
    /*
    if (hg_bulk_na_class_g->mem_handle_create_segments) {
        for (i = 0; i < segment_count; i++) {
            hg_bulk->total_size += bulk_segments[i].size;
        }
        hg_bulk->segments[0].address = bulk_segments[0].address;
        hg_bulk->segments[0].size = hg_bulk->total_size;
        hg_bulk->segment_handles[0] = NA_MEM_HANDLE_NULL;

        na_ret = NA_Mem_handle_create_segments(hg_bulk_na_class_g,
                (struct na_segment *) bulk_segments, segment_count,
                na_flags, &hg_bulk->segment_handles[0]);
        if (na_ret != NA_SUCCESS) {
            HG_LOG_ERROR("NA_Mem_handle_create_segments failed");
            ret = HG_NA_ERROR;
            goto done;
        }
    }
    */

    /* Loop over the list of segments and register them */
    for (i = 0; i < hg_bulk->segment_count; i++) {
        hg_bulk->segments[i].size = buf_sizes[i];
        if (!hg_bulk->segments[i].size) {
            HG_LOG_ERROR("Invalid segment size");
            ret = HG_INVALID_PARAM;
            goto done;
        }
        hg_bulk->total_size += hg_bulk->segments[i].size;

        if (buf_ptrs && buf_ptrs[i])
            hg_bulk->segments[i].address = (hg_ptr_t) buf_ptrs[i];
        else {
            /* Use calloc to avoid uninitialized memory used for transfer */
            hg_bulk->segments[i].address = (hg_ptr_t) calloc(
                    hg_bulk->segments[i].size, sizeof(char));
            if (!hg_bulk->segments[i].address) {
                HG_LOG_ERROR("Could not allocate segment");
                ret = HG_NOMEM_ERROR;
                goto done;
            }
        }

        na_ret = NA_Mem_handle_create(hg_class->na_class,
                (void *) hg_bulk->segments[i].address,
                hg_bulk->segments[i].size, flags, &hg_bulk->segment_handles[i]);
        if (na_ret != NA_SUCCESS) {
            HG_LOG_ERROR("NA_Mem_handle_create failed");
            ret = HG_NA_ERROR;
            goto done;
        }

        na_ret = NA_Mem_register(hg_class->na_class, hg_bulk->segment_handles[i]);
        if (na_ret != NA_SUCCESS) {
            HG_LOG_ERROR("NA_Mem_register failed");
            ret = HG_NA_ERROR;
            goto done;
        }
    }

    *hg_bulk_ptr = hg_bulk;

done:
    if (ret != HG_SUCCESS) {
        hg_bulk_free(hg_bulk);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_bulk_free(struct hg_bulk *hg_bulk)
{
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;
    unsigned int i;

    if (!hg_bulk) goto done;

    if (--hg_bulk->ref_count) {
        goto done;
    }

    if (hg_bulk->segment_handles) {
        /* Unregister/free NA memory handles */
        if (hg_bulk->segment_published) {
            for (i = 0; i < hg_bulk->segment_count; i++) {
                na_ret = NA_Mem_unpublish(hg_bulk->hg_class->na_class,
                        hg_bulk->segment_handles[i]);
                if (na_ret != NA_SUCCESS) {
                    HG_LOG_ERROR("NA_Mem_unpublish failed");
                }
            }
            hg_bulk->segment_published = HG_FALSE;
        }

        for (i = 0; i < hg_bulk->segment_count; i++) {
            na_ret = NA_Mem_deregister(hg_bulk->hg_class->na_class,
                    hg_bulk->segment_handles[i]);
            if (na_ret != NA_SUCCESS) {
                HG_LOG_ERROR("NA_Mem_deregister failed");
            }

            na_ret = NA_Mem_handle_free(hg_bulk->hg_class->na_class,
                    hg_bulk->segment_handles[i]);
            if (na_ret != NA_SUCCESS) {
                HG_LOG_ERROR("NA_Mem_handle_free failed");
            }
        }

        free(hg_bulk->segment_handles);
    }

    /* Free segments */
    if (hg_bulk->segment_alloc) {
        for (i = 0; i < hg_bulk->segment_count; i++) {
            free((void *) hg_bulk->segments[i].address);
        }
    }
    free(hg_bulk->segments);
    free(hg_bulk);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
hg_bulk_offset_translate(struct hg_bulk *hg_bulk, hg_size_t offset,
    hg_uint32_t *segment_start_index, hg_size_t *segment_start_offset)
{
    hg_uint32_t i, new_segment_start_index = 0;
    hg_size_t new_segment_offset = offset, next_offset = 0;

    /* Get start index and handle offset */
    for (i = 0; i < hg_bulk->segment_count; i++) {
        next_offset += hg_bulk->segments[i].size;
        if (offset < next_offset) {
            new_segment_start_index = i;
            break;
        }
        new_segment_offset -= hg_bulk->segments[i].size;
    }

    *segment_start_index = new_segment_start_index;
    *segment_start_offset = new_segment_offset;
}

/*---------------------------------------------------------------------------*/
static void
hg_bulk_access(struct hg_bulk *hg_bulk, hg_size_t offset, hg_size_t size,
    hg_uint8_t flags, hg_uint32_t max_count, void **buf_ptrs,
    hg_size_t *buf_sizes, hg_uint32_t *actual_count)
{
    hg_uint32_t segment_index;
    hg_size_t segment_offset;
    hg_size_t remaining_size = size;
    hg_uint32_t count = 0;

    /* TODO use flags */
    (void) flags;

    hg_bulk_offset_translate(hg_bulk, offset, &segment_index,
            &segment_offset);

    while ((remaining_size > 0) && (count < max_count)) {
        hg_ptr_t segment_address;
        hg_size_t segment_size;

        /* Can only transfer smallest size */
        segment_size = hg_bulk->segments[segment_index].size
                - segment_offset;

        /* Remaining size may be smaller */
        segment_size = HG_BULK_MIN(remaining_size, segment_size);
        segment_address = hg_bulk->segments[segment_index].address +
                (hg_ptr_t) segment_offset;

        /* Fill segments */
        if (buf_ptrs) buf_ptrs[count] = (void *) segment_address;
        if (buf_sizes) buf_sizes[count] = segment_size;
        /*
        printf("Segment %d: address=0x%lX\n", count, segment_address);
        printf("Segment %d: size=%zu\n", count, segment_size);
        */

        /* Decrease remaining size from the size of data we transferred */
        remaining_size -= segment_size;

        /* Change segment */
        segment_index++;
        segment_offset = 0;
        count++;
    }

    if (actual_count) *actual_count = count;
}

/*---------------------------------------------------------------------------*/
static na_return_t
hg_bulk_transfer_cb(const struct na_cb_info *callback_info)
{
    struct hg_bulk_op_id *hg_bulk_op_id =
            (struct hg_bulk_op_id *) callback_info->arg;
    na_return_t ret = NA_SUCCESS;

    if (callback_info->ret == NA_CANCELED) {
        /* If canceled, mark handle as canceled */
        hg_atomic_cas32(&hg_bulk_op_id->canceled, 0, 1);
    } else if (callback_info->ret != NA_SUCCESS) {
        HG_LOG_ERROR("Error in NA callback");
        ret = NA_PROTOCOL_ERROR;
        goto done;
    }

    /* When all NA transfers that correspond to bulk operation complete
     * add HG user callback to completion queue
     */
    if ((unsigned int) hg_atomic_incr32(&hg_bulk_op_id->op_completed_count)
            == hg_bulk_op_id->op_count) {
        hg_bulk_complete(hg_bulk_op_id);
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_bulk_transfer_pieces(na_bulk_op_t na_bulk_op, na_addr_t origin_addr,
    struct hg_bulk *hg_bulk_origin, hg_size_t origin_segment_start_index,
    hg_size_t origin_segment_start_offset, struct hg_bulk *hg_bulk_local,
    hg_size_t local_segment_start_index, hg_size_t local_segment_start_offset,
    hg_size_t size, struct hg_bulk_op_id *hg_bulk_op_id,
    unsigned int *na_op_count)
{
    hg_size_t origin_segment_index = origin_segment_start_index;
    hg_size_t local_segment_index = local_segment_start_index;
    hg_size_t origin_segment_offset = origin_segment_start_offset;
    hg_size_t local_segment_offset = local_segment_start_offset;
    hg_size_t remaining_size = size;
    unsigned int count = 0;
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;

    for (;;) {
        hg_size_t origin_transfer_size, local_transfer_size, transfer_size;

        /* Can only transfer smallest size */
        origin_transfer_size = hg_bulk_origin->segments[origin_segment_index].size
                - origin_segment_offset;
        local_transfer_size = hg_bulk_local->segments[local_segment_index].size
                - local_segment_offset;
        transfer_size = HG_BULK_MIN(origin_transfer_size, local_transfer_size);

        /* Remaining size may be smaller */
        transfer_size = HG_BULK_MIN(remaining_size, transfer_size);

        if (na_bulk_op) {
            na_op_id_t op_id = 0;
            na_ret = na_bulk_op(hg_bulk_origin->hg_class->na_class,
                    hg_bulk_origin->hg_class->na_context,
                    hg_bulk_transfer_cb, hg_bulk_op_id,
                    hg_bulk_local->segment_handles[local_segment_index],
                    hg_bulk_local->segments[local_segment_index].address,
                    local_segment_offset,
                    hg_bulk_origin->segment_handles[origin_segment_index],
                    hg_bulk_origin->segments[origin_segment_index].address,
                    origin_segment_offset, transfer_size, origin_addr,
                    &hg_bulk_op_id->na_op_ids[count]);
            if (na_ret != NA_SUCCESS) {
                HG_LOG_ERROR("Could not transfer data");
                ret = HG_NA_ERROR;
                break;
            }
            else {
                hg_bulk_op_id->na_op_ids[count] = op_id;
            }
            
        }
        count++;

        /* Decrease remaining size from the size of data we transferred
         * and exit if everything has been transferred */
        remaining_size -= transfer_size;
        if (!remaining_size)
            break;

        /* Increment offsets from the size of data we transferred */
        origin_segment_offset += transfer_size;
        local_segment_offset += transfer_size;

        /* Change segment if new offset exceeds segment size */
        if (origin_segment_offset >=
            hg_bulk_origin->segments[origin_segment_index].size) {
            origin_segment_index++;
            origin_segment_offset = 0;
        }
        if (local_segment_offset >=
            hg_bulk_local->segments[local_segment_index].size) {
            local_segment_index++;
            local_segment_offset = 0;
        }
    }

    /* Set number of NA operations issued */
    if (na_op_count)
        *na_op_count = count;

    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_bulk_transfer(hg_context_t *context, hg_cb_t callback, void *arg,
    hg_bulk_op_t op, na_addr_t origin_addr, struct hg_bulk *hg_bulk_origin,
    hg_size_t origin_offset, struct hg_bulk *hg_bulk_local,
    hg_size_t local_offset, hg_size_t size, hg_op_id_t *op_id)
{
    hg_uint32_t origin_segment_start_index, local_segment_start_index;
    hg_size_t origin_segment_start_offset, local_segment_start_offset;
    struct hg_bulk_op_id *hg_bulk_op_id = NULL;
    na_bulk_op_t na_bulk_op;
    hg_bool_t is_self = NA_Addr_is_self(hg_bulk_origin->hg_class->na_class,
            origin_addr);
    hg_return_t ret = HG_SUCCESS;

    /* Map op to NA op */
    switch (op) {
        case HG_BULK_PUSH:
            na_bulk_op = (is_self) ? hg_bulk_memcpy_put : hg_bulk_na_put;
            break;
        case HG_BULK_PULL:
            /* Eager mode can only be used when data is pulled from origin */
            na_bulk_op = (is_self || hg_bulk_origin->eager_mode) ?
                    hg_bulk_memcpy_get : hg_bulk_na_get;
            break;
        default:
            HG_LOG_ERROR("Unknown bulk operation");
            ret = HG_INVALID_PARAM;
            goto done;
    }

    /* Allocate op_id */
    hg_bulk_op_id = (struct hg_bulk_op_id *) malloc(
            sizeof(struct hg_bulk_op_id));
    if (!hg_bulk_op_id) {
        HG_LOG_ERROR("Could not allocate HG Bulk operation ID");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    hg_bulk_op_id->hg_class = hg_bulk_origin->hg_class;
    hg_bulk_op_id->context = context;
    hg_bulk_op_id->callback = callback;
    hg_bulk_op_id->arg = arg;
    hg_atomic_set32(&hg_bulk_op_id->completed, 0);
    hg_atomic_set32(&hg_bulk_op_id->canceled, 0);
    hg_bulk_op_id->op_count = 0;
    hg_atomic_set32(&hg_bulk_op_id->op_completed_count, 0);
    hg_bulk_op_id->op = op;
    hg_bulk_op_id->hg_bulk_origin = hg_bulk_origin;
    hg_bulk_op_id->hg_bulk_local = hg_bulk_local;
    hg_bulk_op_id->na_op_ids = NULL;

    /* Translate bulk_offset */
    hg_bulk_offset_translate(hg_bulk_origin, origin_offset,
            &origin_segment_start_index, &origin_segment_start_offset);

    /* Translate block offset */
    hg_bulk_offset_translate(hg_bulk_local, local_offset,
            &local_segment_start_index, &local_segment_start_offset);

    /* Figure out number of NA operations required */
    hg_bulk_transfer_pieces(NULL, NA_ADDR_NULL, hg_bulk_origin,
            origin_segment_start_index, origin_segment_start_offset,
            hg_bulk_local, local_segment_start_index,
            local_segment_start_offset, size, NULL, &hg_bulk_op_id->op_count);
    if (!hg_bulk_op_id->op_count) {
        HG_LOG_ERROR("Could not get bulk op_count");
        ret = HG_INVALID_PARAM;
        goto done;
    }
    else {
        /* Allocate memory for na op_ids. */
        hg_bulk_op_id->na_op_ids = malloc(sizeof(na_op_id_t) * hg_bulk_op_id->op_count);
        if (hg_bulk_op_id->na_op_ids == NULL) {
            HG_LOG_ERROR("Could not allocate memory for op_ids.");
        }
        
    }


    /* Allocate memory for NA operation IDs */
    hg_bulk_op_id->na_op_ids = malloc(sizeof(na_op_id_t) * hg_bulk_op_id->op_count);
    if (!hg_bulk_op_id->na_op_ids) {
        HG_LOG_ERROR("Could not allocate memory for op_ids");
        ret = HG_NOMEM_ERROR;
        goto done;
    }

    /* Assign op_id */
    if (op_id && op_id != HG_OP_ID_IGNORE) *op_id = (hg_op_id_t) hg_bulk_op_id;

    /* Do actual transfer */
    ret = hg_bulk_transfer_pieces(na_bulk_op, origin_addr, hg_bulk_origin,
            origin_segment_start_index, origin_segment_start_offset,
            hg_bulk_local, local_segment_start_index,
            local_segment_start_offset, size, hg_bulk_op_id, NULL);
    if (ret != HG_SUCCESS) {
        HG_LOG_ERROR("Could not transfer data pieces");
        goto done;
    }

done:
    if (ret != HG_SUCCESS && hg_bulk_op_id) {
        free(hg_bulk_op_id->na_op_ids);
        free(hg_bulk_op_id);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
static hg_return_t
hg_bulk_complete(struct hg_bulk_op_id *hg_bulk_op_id)
{
    hg_context_t *context = hg_bulk_op_id->context;
    struct hg_completion_entry *hg_completion_entry = NULL;
    hg_return_t ret = HG_SUCCESS;

    /* Mark operation as completed. */
    hg_atomic_incr32(&hg_bulk_op_id->completed);

    hg_completion_entry = (struct hg_completion_entry *) malloc(sizeof(struct hg_completion_entry));
    if (!hg_completion_entry) {
        HG_LOG_ERROR("Could not allocate HG completion entry");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    hg_completion_entry->op_type = HG_BULK;
    hg_completion_entry->op_id.hg_bulk_op_id = hg_bulk_op_id;

    ret = hg_core_completion_add(context, hg_completion_entry);
    if (ret != HG_SUCCESS) {
        HG_LOG_ERROR("Could not add HG completion entry to completion queue");
        goto done;
    }

done:
    if (ret != HG_SUCCESS)
        free(hg_completion_entry);
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_bulk_trigger_entry(struct hg_bulk_op_id *hg_bulk_op_id)
{
    hg_return_t ret = HG_SUCCESS;

    /* Execute callback */
    if (hg_bulk_op_id->callback) {
        struct hg_cb_info hg_cb_info;

        hg_cb_info.arg = hg_bulk_op_id->arg;
        hg_cb_info.ret =
            hg_atomic_get32(&hg_bulk_op_id->canceled) ? HG_CANCELED : HG_SUCCESS;
        hg_cb_info.type = HG_CB_BULK;
        hg_cb_info.info.bulk.op = hg_bulk_op_id->op;
        hg_cb_info.info.bulk.origin_handle = (hg_bulk_t) hg_bulk_op_id->hg_bulk_origin;
        hg_cb_info.info.bulk.local_handle = (hg_bulk_t) hg_bulk_op_id->hg_bulk_local;

        hg_bulk_op_id->callback(&hg_cb_info);
    }

    /* Free op */
    free(hg_bulk_op_id->na_op_ids);
    free(hg_bulk_op_id);

    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_create(hg_class_t *hg_class, hg_uint32_t count, void **buf_ptrs,
    const hg_size_t *buf_sizes, hg_uint8_t flags, hg_bulk_t *handle)
{
    struct hg_bulk *hg_bulk = NULL;
    hg_return_t ret = HG_SUCCESS;

    if (!hg_class) {
        HG_LOG_ERROR("NULL HG class");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (!count) {
        HG_LOG_ERROR("Invalid number of segments");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (!buf_sizes) {
        HG_LOG_ERROR("NULL segment pointer");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    switch (flags) {
        case HG_BULK_READWRITE:
            break;
        case HG_BULK_READ_ONLY:
            break;
        case HG_BULK_WRITE_ONLY:
            break;
        default:
            HG_LOG_ERROR("Unrecognized handle flag");
            ret = HG_INVALID_PARAM;
            goto done;
    }

    ret = hg_bulk_create(hg_class, count, buf_ptrs, buf_sizes, flags, &hg_bulk);
    if (ret != HG_SUCCESS) {
        HG_LOG_ERROR("Could not create bulk handle");
        goto done;
    }

    *handle = (hg_bulk_t) hg_bulk;

done:
    if (ret != HG_SUCCESS) {
        hg_bulk_free(hg_bulk);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_free(hg_bulk_t handle)
{
    hg_return_t ret = HG_SUCCESS;
    struct hg_bulk *hg_bulk = (struct hg_bulk *) handle;

    if (!hg_bulk) goto done;

    ret = hg_bulk_free(hg_bulk);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_access(hg_bulk_t handle, hg_size_t offset, hg_size_t size,
    hg_uint8_t flags, hg_uint32_t max_count, void **buf_ptrs,
    hg_size_t *buf_sizes, hg_uint32_t *actual_count)
{
    struct hg_bulk *hg_bulk = (struct hg_bulk *) handle;
    hg_uint32_t count = 0;
    hg_return_t ret = HG_SUCCESS;

    if (!hg_bulk) {
        HG_LOG_ERROR("NULL memory handle passed");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (!size || !max_count) goto done;

    hg_bulk_access(hg_bulk, offset, size, flags, max_count, buf_ptrs,
            buf_sizes, &count);

done:
    if (ret == HG_SUCCESS) {
        if (actual_count) *actual_count = count;
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_size_t
HG_Bulk_get_size(hg_bulk_t handle)
{
    hg_size_t ret = 0;
    struct hg_bulk *hg_bulk = (struct hg_bulk *) handle;

    if (!hg_bulk) {
        HG_LOG_ERROR("NULL bulk handle");
        goto done;
    }

    ret = hg_bulk->total_size;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_uint32_t
HG_Bulk_get_segment_count(hg_bulk_t handle)
{
    struct hg_bulk *hg_bulk = (struct hg_bulk *) handle;
    hg_uint32_t ret = 0;

    if (!hg_bulk) {
        HG_LOG_ERROR("NULL bulk handle");
        goto done;
    }

    ret = hg_bulk->segment_count;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_size_t
HG_Bulk_get_serialize_size(hg_bulk_t handle, hg_bool_t request_eager)
{
    struct hg_bulk *hg_bulk = (struct hg_bulk *) handle;
    hg_size_t ret = 0;
    hg_uint32_t i;

    if (!hg_bulk) {
        HG_LOG_ERROR("NULL bulk handle");
        goto done;
    }

    ret = sizeof(hg_bulk->total_size) + sizeof(hg_bulk->segment_count)
            + hg_bulk->segment_count * sizeof(struct hg_bulk_segment)
            + sizeof(hg_bulk->eager_mode);
    for (i = 0; i < hg_bulk->segment_count; i++) {
        ret += NA_Mem_handle_get_serialize_size(
                hg_bulk->hg_class->na_class, hg_bulk->segment_handles[i]);
    }
    if (request_eager && (hg_bulk->flags == HG_BULK_READ_ONLY))
        ret += hg_bulk->total_size;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_serialize(void *buf, hg_size_t buf_size, hg_bool_t request_eager,
    hg_bulk_t handle)
{
    struct hg_bulk *hg_bulk = (struct hg_bulk *) handle;
    char *buf_ptr = (char*) buf;
    hg_size_t buf_size_left = buf_size;
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;
    hg_bool_t eager_mode;
    hg_uint32_t i;

    if (!hg_bulk) {
        HG_LOG_ERROR("NULL memory handle passed");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    /* Publish handle at this point if not published yet */
    if (!hg_bulk->segment_published) {
        for (i = 0; i < hg_bulk->segment_count; i++) {
            na_ret = NA_Mem_publish(hg_bulk->hg_class->na_class,
                    hg_bulk->segment_handles[i]);
            if (na_ret != NA_SUCCESS) {
                HG_LOG_ERROR("NA_Mem_publish failed");
                ret = HG_NA_ERROR;
                goto done;
            }
        }
        hg_bulk->segment_published = HG_TRUE;
    }

    if (buf_size < HG_Bulk_get_serialize_size(handle, request_eager)) {
        HG_LOG_ERROR("Buffer size too small for serializing parameter");
        ret = HG_SIZE_ERROR;
        goto done;
    }

    /* Add the size of the data */
    memcpy(buf_ptr, &hg_bulk->total_size, sizeof(hg_size_t));
    buf_ptr += sizeof(hg_size_t);
    buf_size_left -= sizeof(hg_size_t);

    /* Add the number of handles */
    memcpy(buf_ptr, &hg_bulk->segment_count, sizeof(hg_uint32_t));
    buf_ptr += sizeof(hg_uint32_t);
    buf_size_left -= sizeof(hg_uint32_t);

    /* Add the list of sizes */
    for (i = 0; i < hg_bulk->segment_count; i++) {
        memcpy(buf_ptr, &hg_bulk->segments[i], sizeof(struct hg_bulk_segment));
        buf_ptr += sizeof(struct hg_bulk_segment);
        buf_size_left -= sizeof(struct hg_bulk_segment);
    }

    /* Add the NA memory handles */
    for (i = 0; i < hg_bulk->segment_count; i++) {
        na_ret = NA_Mem_handle_serialize(hg_bulk->hg_class->na_class,
                buf_ptr, buf_size_left, hg_bulk->segment_handles[i]);
        if (na_ret != NA_SUCCESS) {
            HG_LOG_ERROR("Could not serialize memory handle");
            ret = HG_NA_ERROR;
            break;
        }
        buf_ptr += NA_Mem_handle_get_serialize_size(
                hg_bulk->hg_class->na_class, hg_bulk->segment_handles[i]);
        buf_size_left -= NA_Mem_handle_get_serialize_size(
                hg_bulk->hg_class->na_class, hg_bulk->segment_handles[i]);
    }

    /* Eager mode is used only when data is set to HG_BULK_READ_ONLY */
    eager_mode = (request_eager && (hg_bulk->flags == HG_BULK_READ_ONLY));
    memcpy(buf_ptr, &eager_mode, sizeof(hg_bool_t));
    buf_ptr += sizeof(hg_bool_t);
    buf_size_left -= sizeof(hg_bool_t);

    /* Add the serialized data */
    if (eager_mode) {
        for (i = 0; i < hg_bulk->segment_count; i++) {
            memcpy(buf_ptr, (const void *) hg_bulk->segments[i].address,
                    hg_bulk->segments[i].size);
            buf_ptr += hg_bulk->segments[i].size;
            buf_size_left -= hg_bulk->segments[i].size;
        }
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_deserialize(hg_class_t *hg_class, hg_bulk_t *handle, const void *buf,
    hg_size_t buf_size)
{
    struct hg_bulk *hg_bulk = NULL;
    const char *buf_ptr = (const char*) buf;
    hg_size_t buf_size_left = buf_size;
    hg_return_t ret = HG_SUCCESS;
    na_return_t na_ret;
    hg_uint32_t i;

    if (!handle) {
        HG_LOG_ERROR("NULL pointer to memory handle passed");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    hg_bulk = (struct hg_bulk *) malloc(sizeof(struct hg_bulk));
    if (!hg_bulk) {
        HG_LOG_ERROR("Could not allocate handle");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    hg_bulk->hg_class = hg_class;
    hg_bulk->total_size = 0;
    hg_bulk->segment_count = 0;
    hg_bulk->segments = NULL;
    hg_bulk->segment_handles = NULL;
    hg_bulk->segment_published = HG_FALSE;
    hg_bulk->segment_alloc = HG_FALSE;
    hg_bulk->flags = 0;
    hg_bulk->eager_mode = HG_FALSE;
    hg_bulk->ref_count = 1;

    /* Get the size of the data */
    memcpy(&hg_bulk->total_size, buf_ptr, sizeof(hg_size_t));
    buf_ptr += sizeof(hg_size_t);
    buf_size_left -= sizeof(hg_size_t);
    if (!hg_bulk->total_size) {
        HG_LOG_ERROR("NULL total size");
        ret = HG_SIZE_ERROR;
        goto done;
    }

    /* Get the number of handles */
    memcpy(&hg_bulk->segment_count, buf_ptr, sizeof(hg_uint32_t));
    buf_ptr += sizeof(hg_uint32_t);
    buf_size_left -= sizeof(hg_uint32_t);
    if (!hg_bulk->segment_count) {
        HG_LOG_ERROR("NULL segment count");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    /* Add the segment array */
    hg_bulk->segments = (struct hg_bulk_segment *) malloc(
            hg_bulk->segment_count * sizeof(struct hg_bulk_segment));
    if (!hg_bulk->segments) {
        HG_LOG_ERROR("Could not allocate segment array");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    for (i = 0; i < hg_bulk->segment_count; i++) {
        memcpy(&hg_bulk->segments[i], buf_ptr, sizeof(struct hg_bulk_segment));
        buf_ptr += sizeof(struct hg_bulk_segment);
        buf_size_left -= sizeof(struct hg_bulk_segment);
        if (!hg_bulk->segments[i].size) {
            HG_LOG_ERROR("NULL segment size");
            ret = HG_SIZE_ERROR;
            goto done;
        }
        /* fprintf(stderr, "Segment[%lu] = %lu bytes\n", i, hg_bulk->segments[i].size); */
    }

    hg_bulk->segment_handles = (na_mem_handle_t *) malloc(
            hg_bulk->segment_count * sizeof(na_mem_handle_t));
    if (!hg_bulk->segment_handles) {
        HG_LOG_ERROR("Could not allocate mem handle list");
        ret = HG_NOMEM_ERROR;
        goto done;
    }
    for (i = 0; i < hg_bulk->segment_count; i++) {
        hg_bulk->segment_handles[i] = NA_MEM_HANDLE_NULL;
    }
    for (i = 0; i < hg_bulk->segment_count; i++) {
        na_size_t serialize_size = NA_Mem_handle_get_serialize_size(
                hg_bulk->hg_class->na_class, hg_bulk->segment_handles[i]);
        na_ret = NA_Mem_handle_deserialize(hg_bulk->hg_class->na_class,
                &hg_bulk->segment_handles[i],
                buf_ptr, buf_size_left);
        if (na_ret != NA_SUCCESS) {
            HG_LOG_ERROR("Could not deserialize memory handle");
            ret = HG_NA_ERROR;
            goto done;
        }
        buf_ptr += serialize_size;
        buf_size_left -= serialize_size;
    }

    /* Add whether data is serialized or not */
    memcpy(&hg_bulk->eager_mode, buf_ptr, sizeof(hg_bool_t));
    buf_ptr += sizeof(hg_bool_t);
    buf_size_left -= sizeof(hg_bool_t);

    /* Add the serialized data */
    if (hg_bulk->eager_mode) {
        hg_bulk->segment_alloc = HG_TRUE;
        for (i = 0; i < hg_bulk->segment_count; i++) {
            /* Use calloc to avoid uninitialized memory used for transfer */
            hg_bulk->segments[i].address = (hg_ptr_t) calloc(
                    hg_bulk->segments[i].size, sizeof(char));
            if (!hg_bulk->segments[i].address) {
                HG_LOG_ERROR("Could not allocate segment");
                ret = HG_NOMEM_ERROR;
                goto done;
            }
            memcpy((void *) hg_bulk->segments[i].address, buf_ptr,
                    hg_bulk->segments[i].size);
            buf_ptr += hg_bulk->segments[i].size;
            buf_size_left -= hg_bulk->segments[i].size;
        }
    }

    *handle = hg_bulk;

done:
    if (ret != HG_SUCCESS) {
        hg_bulk_free(hg_bulk);
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_transfer(hg_context_t *context, hg_cb_t callback, void *arg,
    hg_bulk_op_t op, na_addr_t origin_addr, hg_bulk_t origin_handle,
    hg_size_t origin_offset, hg_bulk_t local_handle, hg_size_t local_offset,
    hg_size_t size, hg_op_id_t *op_id)
{
    struct hg_bulk *hg_bulk_origin = (struct hg_bulk *) origin_handle;
    struct hg_bulk *hg_bulk_local = (struct hg_bulk *) local_handle;
    hg_return_t ret = HG_SUCCESS;

    if (!context) {
        HG_LOG_ERROR("NULL HG bulk context");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (origin_addr == NA_ADDR_NULL) {
        HG_LOG_ERROR("NULL addr passed");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (!hg_bulk_origin || !hg_bulk_local) {
        HG_LOG_ERROR("NULL memory handle passed");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (!size) {
        HG_LOG_ERROR("Transfer size must be non-zero");
        ret = HG_SIZE_ERROR;
        goto done;
    }

    if (size > hg_bulk_origin->total_size) {
        HG_LOG_ERROR("Exceeding size of memory exposed by origin handle");
        ret = HG_SIZE_ERROR;
        goto done;
    }

    if (size > hg_bulk_local->total_size) {
        HG_LOG_ERROR("Exceeding size of memory exposed by local handle");
        ret = HG_SIZE_ERROR;
        goto done;
    }

    switch (op) {
        case HG_BULK_PUSH:
        case HG_BULK_PULL:
            break;
        default:
            HG_LOG_ERROR("Unknown bulk operation");
            ret = HG_INVALID_PARAM;
            goto done;
    }

    ret = hg_bulk_transfer(context, callback, arg, op, origin_addr,
        hg_bulk_origin, origin_offset, hg_bulk_local, local_offset, size,
        op_id);
    if (ret != HG_SUCCESS) {
        HG_LOG_ERROR("Could not transfer data");
        goto done;
    }

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_return_t
HG_Bulk_cancel(hg_op_id_t op_id)
{
    struct hg_bulk_op_id *hg_bulk_op_id = (struct hg_bulk_op_id *) op_id;
    hg_return_t ret = HG_SUCCESS;

    if (!hg_bulk_op_id) {
        HG_LOG_ERROR("NULL HG bulk operation ID");
        ret = HG_INVALID_PARAM;
        goto done;
    }

    if (HG_UTIL_TRUE != hg_atomic_cas32(&hg_bulk_op_id->completed, 1, 0)) {
        /* Cancel all NA operations issued */
        unsigned int i = 0;

        for (i = 0; i < hg_bulk_op_id->op_count; i++) {
            na_return_t na_ret;

            /* Race codition may occur. Watch out. */
            na_ret = NA_Cancel(hg_bulk_op_id->hg_class->na_class,
                hg_bulk_op_id->hg_class->na_context,
                hg_bulk_op_id->na_op_ids[i]);
            if (na_ret != NA_SUCCESS) {
                HG_LOG_ERROR("Could not cancel op id");
                ret = HG_NA_ERROR;
                goto done;
            }
        }
    }


done:
    return ret;
}
