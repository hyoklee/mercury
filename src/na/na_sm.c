/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include "na_private.h"
#include "na_error.h"

#include "mercury_queue.h"
#include "mercury_thread_mutex.h"
#include "mercury_time.h"
#include "mercury_atomic.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>              /* O_ flags */
#include <unistd.h>             /* ftruncate, getpid */
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/mman.h>

/****************/
/* Local Macros */
/****************/

/* Msg sizes */
#define NA_SM_UNEXPECTED_SIZE 4096
#define NA_SM_EXPECTED_SIZE  NA_SM_UNEXPECTED_SIZE 

/* Max tag */
#define NA_SM_MAX_TAG (NA_TAG_UB >> 2)
#define SHM_FILE "/mercury.shm"

#define NA_SM_PRIVATE_DATA(na_class) \
    ((struct na_sm_private_data *)(na_class->private_data))

/************************************/
/* Local Type and Struct Definition */
/************************************/
typedef struct na_sm_op_id na_sm_op_id_t;
typedef struct na_sm_addr na_sm_addr_t;

/* na_sm_addr */
struct na_sm_addr {
    pid_t pid;             /* remote process id */
    struct iovec remote[1];     /* remote address  */
    char* sm_path;         /* Path to shared memory */
    na_bool_t  unexpected; /* Address generated from unexpected recv */
    na_bool_t  self;       /* Boolean for self */
};

struct na_sm_mem_handle {
    na_ptr_t base;     /* Initial address of memory */
    na_size_t size;    /* Size of memory */
    na_uint8_t attr;   /* Flag of operation access */
};

typedef enum na_sm_rma_op {
    NA_SM_RMA_PUT, /* Request a put operation */
    NA_SM_RMA_GET  /* Request a get operation */
} na_sm_rma_op_t;

struct na_sm_info_lookup {
    na_addr_t addr;
};

struct na_sm_info_send_unexpected {
    na_sm_op_id_t *op_id; /* SM operation id */

};

struct na_sm_info_recv_unexpected {
    void *buf;
};

struct na_sm_info_send_expected {
    na_sm_op_id_t *op_id; /* SM operation id */
};

struct na_sm_info_recv_expected {

};

struct na_sm_info_put {

};

struct na_sm_info_get {

};

/* TODO */
struct na_sm_op_id {
    na_context_t *context;
    na_cb_type_t type;
    na_cb_t callback; /* Callback */
    void *arg;
    na_bool_t completed; /* Operation completed */
    na_bool_t canceled;  /* Operation canceled */
    union {
      struct na_sm_info_lookup lookup;
      struct na_sm_info_send_unexpected send_unexpected;
      struct na_sm_info_recv_unexpected recv_unexpected;
      struct na_sm_info_send_expected send_expected;
      struct na_sm_info_recv_expected recv_expected;
      struct na_sm_info_put put;
      struct na_sm_info_get get;
    } info;
};

/* TODO check what is needed here */
struct na_sm_private_data {
    char *listen_addr;                            /* Listen addr */

    hg_thread_mutex_t test_unexpected_mutex;      /* Mutex */
    hg_queue_t *unexpected_msg_queue;             /* Unexpected message queue */
    hg_thread_mutex_t unexpected_msg_queue_mutex; /* Mutex */
    hg_queue_t *unexpected_op_queue;              /* Unexpected op queue */
    hg_thread_mutex_t unexpected_op_queue_mutex;  /* Mutex */
};

/********************/
/* Local Prototypes */
/********************/

/* check_protocol */
static na_bool_t
na_sm_check_protocol(
        const char *protocol_name
        );

/* initialize */
static na_return_t
na_sm_initialize(
        na_class_t           *na_class,
        const struct na_info *na_info,
        na_bool_t             listen
        );

/**
 * initialize
 */
static na_return_t
na_sm_init(
        na_class_t *na_class
        );

/* finalize */
static na_return_t
na_sm_finalize(
        na_class_t *na_class
        );

static na_return_t
na_sm_context_create(
        na_class_t          *na_class,
        na_plugin_context_t *context
        );

static na_return_t
na_sm_context_destroy(
        na_class_t          *na_class,
        na_plugin_context_t  context
        );

/* addr_lookup */
static na_return_t
na_sm_addr_lookup(
        na_class_t   *na_class,
        na_context_t *context,
        na_cb_t       callback,
        void         *arg,
        const char   *name,
        na_op_id_t   *op_id
        );

/* addr_self */
static na_return_t
na_sm_addr_self(
        na_class_t *na_class,
        na_addr_t  *addr
        );

/* addr_free */
static na_return_t
na_sm_addr_free(
        na_class_t *na_class,
        na_addr_t   addr
        );

/* addr_is_self */
static na_bool_t
na_sm_addr_is_self(
        na_class_t *na_class,
        na_addr_t   addr
        );

/* addr_to_string */
static na_return_t
na_sm_addr_to_string(
        na_class_t *na_class,
        char       *buf,
        na_size_t  *buf_size,
        na_addr_t   addr
        );

/* msg_get_max */
static na_size_t
na_sm_msg_get_max_expected_size(
        na_class_t *na_class
        );

static na_size_t
na_sm_msg_get_max_unexpected_size(
        na_class_t *na_class
        );

static na_tag_t
na_sm_msg_get_max_tag(
        na_class_t *na_class
        );

/* msg_send_unexpected */
static na_return_t
na_sm_msg_send_unexpected(
        na_class_t   *na_class,
        na_context_t *context,
        na_cb_t       callback,
        void         *arg,
        const void   *buf,
        na_size_t     buf_size,
        na_addr_t     dest,
        na_tag_t      tag,
        na_op_id_t   *op_id
        );

/* msg_recv_unexpected */
static na_return_t
na_sm_msg_recv_unexpected(
        na_class_t   *na_class,
        na_context_t *context,
        na_cb_t       callback,
        void         *arg,
        void         *buf,
        na_size_t     buf_size,
        na_op_id_t   *op_id
        );

/* msg_send_expected */
static na_return_t
na_sm_msg_send_expected(
        na_class_t   *na_class,
        na_context_t *context,
        na_cb_t       callback,
        void         *arg,
        const void   *buf,
        na_size_t     buf_size,
        na_addr_t     dest,
        na_tag_t      tag,
        na_op_id_t   *op_id
        );

/* msg_recv_expected */
static na_return_t
na_sm_msg_recv_expected(
        na_class_t   *na_class,
        na_context_t *context,
        na_cb_t       callback,
        void         *arg,
        void         *buf,
        na_size_t     buf_size,
        na_addr_t     source,
        na_tag_t      tag,
        na_op_id_t   *op_id
        );

/* mem_handle */
static na_return_t
na_sm_mem_handle_create(
        na_class_t      *na_class,
        void            *buf,
        na_size_t        buf_size,
        unsigned long    flags,
        na_mem_handle_t *mem_handle
        );

static na_return_t
na_sm_mem_handle_free(
        na_class_t      *na_class,
        na_mem_handle_t  mem_handle
        );

static na_return_t
na_sm_mem_register(
        na_class_t      *na_class,
        na_mem_handle_t  mem_handle
        );

static na_return_t
na_sm_mem_deregister(
        na_class_t      *na_class,
        na_mem_handle_t  mem_handle
        );

/* mem_handle serialization */
static na_size_t
na_sm_mem_handle_get_serialize_size(
        na_class_t      *na_class,
        na_mem_handle_t  mem_handle
        );

static na_return_t
na_sm_mem_handle_serialize(
        na_class_t      *na_class,
        void            *buf,
        na_size_t        buf_size,
        na_mem_handle_t  mem_handle
        );

static na_return_t
na_sm_mem_handle_deserialize(
        na_class_t      *na_class,
        na_mem_handle_t *mem_handle,
        const void      *buf,
        na_size_t        buf_size
        );

/* put */
static na_return_t
na_sm_put(
        na_class_t      *na_class,
        na_context_t    *context,
        na_cb_t          callback,
        void            *arg,
        na_mem_handle_t  local_mem_handle,
        na_offset_t      local_offset,
        na_mem_handle_t  remote_mem_handle,
        na_offset_t      remote_offset,
        na_size_t        length,
        na_addr_t        remote_addr,
        na_op_id_t      *op_id
        );

/* get */
static na_return_t
na_sm_get(
        na_class_t      *na_class,
        na_context_t    *context,
        na_cb_t          callback,
        void            *arg,
        na_mem_handle_t  local_mem_handle,
        na_offset_t      local_offset,
        na_mem_handle_t  remote_mem_handle,
        na_offset_t      remote_offset,
        na_size_t        length,
        na_addr_t        remote_addr,
        na_op_id_t      *op_id
        );

/* progress */
static na_return_t
na_sm_progress(
        na_class_t   *na_class,
        na_context_t *context,
        unsigned int  timeout
        );


/* TODO probably need that */
static na_return_t
na_sm_complete(
        struct na_sm_op_id *na_sm_op_id
        );

static void
na_sm_release(
        struct na_cb_info *callback_info,
        void              *arg
        );

/* cancel */
static na_return_t
na_sm_cancel(
        na_class_t   *na_class,
        na_context_t *context,
        na_op_id_t    op_id
        );

/*******************/
/* Local Variables */
/*******************/

const na_class_t na_sm_class_g = {
        NULL,                                /* private_data */
        "sm",                                /* name */
        na_sm_check_protocol,                /* check_protocol */
        na_sm_initialize,                    /* initialize */
        na_sm_finalize,                      /* finalize */
        na_sm_context_create,                /* context_create */
        na_sm_context_destroy,               /* context_destroy */
        na_sm_addr_lookup,                   /* addr_lookup */
        na_sm_addr_free,                     /* addr_free */
        na_sm_addr_self,                     /* addr_self */
        NULL,                                /* addr_dup */
        na_sm_addr_is_self,                  /* addr_is_self */
        na_sm_addr_to_string,                /* addr_to_string */
        na_sm_msg_get_max_expected_size,     /* msg_get_max_expected_size */
        na_sm_msg_get_max_unexpected_size,   /* msg_get_max_expected_size */
        na_sm_msg_get_max_tag,               /* msg_get_max_tag */
        na_sm_msg_send_unexpected,           /* msg_send_unexpected */
        na_sm_msg_recv_unexpected,           /* msg_recv_unexpected */
        na_sm_msg_send_expected,             /* msg_send_expected */
        na_sm_msg_recv_expected,             /* msg_recv_expected */
        na_sm_mem_handle_create,             /* mem_handle_create */
        NULL,                                /* mem_handle_create_segment */
        na_sm_mem_handle_free,               /* mem_handle_free */
        na_sm_mem_register,                  /* mem_register */
        na_sm_mem_deregister,                /* mem_deregister */
        NULL,                                /* mem_publish */
        NULL,                                /* mem_unpublish */
        na_sm_mem_handle_get_serialize_size, /* mem_handle_get_serialize_size */
        na_sm_mem_handle_serialize,          /* mem_handle_serialize */
        na_sm_mem_handle_deserialize,        /* mem_handle_deserialize */
        na_sm_put,                           /* put */
        na_sm_get,                           /* get */
        na_sm_progress,                      /* progress */
        na_sm_cancel                         /* cancel */
};

/********************/
/* Plugin callbacks */
/********************/

/*---------------------------------------------------------------------------*/
static na_bool_t
na_sm_check_protocol(const char *protocol_name)
{
    na_bool_t accept = NA_FALSE;
    if (!strncmp("sm", protocol_name, 2)){
        accept = NA_TRUE;
    }
    else {
        printf("%s\n", protocol_name);
    }
    return accept;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_initialize(na_class_t * na_class, const struct na_info *na_info,
        na_bool_t listen)
{
    na_return_t ret = NA_SUCCESS;
    fprintf(stderr, "comes here\n");
    return ret;
    
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_init(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_finalize(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_context_create(na_class_t NA_UNUSED *na_class,
        na_plugin_context_t *context)
{
    na_return_t ret = NA_SUCCESS;
    /* path to shared memory */
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_context_destroy(na_class_t NA_UNUSED *na_class,
        na_plugin_context_t context)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_lookup(na_class_t NA_UNUSED *na_class, na_context_t *context,
        na_cb_t callback, void *arg, const char *name, na_op_id_t *op_id)
{
    struct na_sm_op_id *na_sm_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    na_sm_addr_t *na_sm_addr = NULL;
    
    /* Allocate op_id */
    na_sm_op_id = (na_sm_op_id_t *) calloc(1, sizeof(*na_sm_op_id));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_RECV_UNEXPECTED;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    
    /* Allocte addr */
    na_sm_addr = (na_sm_addr_t *)malloc(sizeof(*na_sm_addr));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM address");
        free(na_sm_op_id);
        return NA_NOMEM_ERROR;
    }
    na_sm_addr->sm_path = NULL;
    na_sm_addr->sm_path = (char *)malloc(sizeof(char)*PATH_MAX);
    strncpy(na_sm_addr->sm_path, SHM_FILE, 12);
    
    if (!na_sm_addr->sm_path) {
        NA_LOG_ERROR("Could not allocate NA SM path");
        free(na_sm_op_id);
        free(na_sm_addr);
        return NA_NOMEM_ERROR;
    }
    na_sm_addr->unexpected = NA_FALSE;
    na_sm_addr->pid = getpid();
    
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_self(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_free(na_class_t NA_UNUSED *na_class, na_addr_t addr)
{
    na_sm_addr_t *na_sm_addr = (na_sm_addr_t *)addr;
    na_return_t ret = NA_SUCCESS;
    
    if (!na_sm_addr) {
        NA_LOG_ERROR("NULL SM addr");
        return  NA_INVALID_PARAM;
    }
    
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_bool_t
na_sm_addr_is_self(na_class_t NA_UNUSED *na_class, na_addr_t addr)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_to_string(na_class_t *na_class, char *buf,
        na_size_t *buf_size, na_addr_t addr)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_size_t
na_sm_msg_get_max_expected_size(na_class_t NA_UNUSED *na_class)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_size_t
na_sm_msg_get_max_unexpected_size(na_class_t NA_UNUSED *na_class)
{
    na_size_t max_unexpected_size = NA_SM_UNEXPECTED_SIZE;
    
    return max_unexpected_size;
}

/*---------------------------------------------------------------------------*/
static na_tag_t
na_sm_msg_get_max_tag(na_class_t NA_UNUSED *na_class)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send_unexpected(na_class_t NA_UNUSED *na_class,
        na_context_t *context, na_cb_t callback, void *arg, const void *buf,
        na_size_t buf_size, na_addr_t dest, na_tag_t tag, na_op_id_t *op_id)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
        na_cb_t callback, void *arg, void *buf, na_size_t buf_size,
        na_op_id_t *op_id)
{
    na_sm_op_id_t *na_sm_op_id = NULL;
    na_return_t ret = NA_SUCCESS;
    /* Allocate na_op_id */
    na_sm_op_id = (na_sm_op_id_t *)calloc(1, sizeof(*na_sm_op_id));
    if(!na_sm_op_id){
        NA_LOG_ERROR("Could not allocate NA SM operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_RECV_UNEXPECTED;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->info.recv_unexpected.buf = buf;
    
    *op_id = (na_op_id_t) na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
        na_cb_t callback, void *arg, const void *buf, na_size_t buf_size,
        na_addr_t dest, na_tag_t tag, na_op_id_t *op_id)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_recv_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
        na_cb_t callback, void *arg, void *buf, na_size_t buf_size,
        na_addr_t source, na_tag_t tag, na_op_id_t *op_id)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
        na_size_t buf_size, unsigned long flags, na_mem_handle_t *mem_handle)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_free(na_class_t NA_UNUSED *na_class,
        na_mem_handle_t mem_handle)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_register(na_class_t NA_UNUSED *na_class, na_mem_handle_t NA_UNUSED mem_handle)
{
    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_deregister(na_class_t NA_UNUSED *na_class, na_mem_handle_t NA_UNUSED mem_handle)
{
    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_size_t
na_sm_mem_handle_get_serialize_size(na_class_t NA_UNUSED *na_class,
        na_mem_handle_t NA_UNUSED mem_handle)
{
    return sizeof(struct na_sm_mem_handle);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_serialize(na_class_t NA_UNUSED *na_class, void *buf,
        na_size_t buf_size, na_mem_handle_t mem_handle)
{
    struct na_sm_mem_handle *na_sm_mem_handle =
            (struct na_sm_mem_handle*) mem_handle;
    na_return_t ret = NA_SUCCESS;

    if (buf_size < sizeof(struct na_sm_mem_handle)) {
        NA_LOG_ERROR("Buffer size too small for serializing parameter");
        ret = NA_SIZE_ERROR;
        goto done;
    }

    /* Copy struct */
    memcpy(buf, na_sm_mem_handle, sizeof(struct na_sm_mem_handle));

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_deserialize(na_class_t NA_UNUSED *na_class,
        na_mem_handle_t *mem_handle, const void *buf, na_size_t buf_size)
{
    struct na_sm_mem_handle *na_sm_mem_handle = NULL;
    na_return_t ret = NA_SUCCESS;

    if (buf_size < sizeof(struct na_sm_mem_handle)) {
        NA_LOG_ERROR("Buffer size too small for deserializing parameter");
        ret = NA_SIZE_ERROR;
        goto done;
    }

    na_sm_mem_handle = (struct na_sm_mem_handle*)
            malloc(sizeof(struct na_sm_mem_handle));
    if (!na_sm_mem_handle) {
          NA_LOG_ERROR("Could not allocate NA SM memory handle");
          ret = NA_NOMEM_ERROR;
          goto done;
    }

    /* Copy struct */
    memcpy(na_sm_mem_handle, buf, sizeof(struct na_sm_mem_handle));

    *mem_handle = (na_mem_handle_t) na_sm_mem_handle;

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_put(na_class_t *na_class, na_context_t *context, na_cb_t callback,
        void *arg, na_mem_handle_t local_mem_handle, na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle, na_offset_t remote_offset,
        na_size_t length, na_addr_t remote_addr, na_op_id_t *op_id)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
        void *arg, na_mem_handle_t local_mem_handle, na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle, na_offset_t remote_offset,
        na_size_t length, na_addr_t remote_addr, na_op_id_t *op_id)
{
    na_return_t ret = NA_SUCCESS;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress(na_class_t *na_class, na_context_t *context,
        unsigned int timeout)
{
    na_return_t ret = NA_SUCCESS;
    /* Convert timeout in ms into seconds. */    
    double remaining = timeout / 1000.0;
			
    fprintf(stderr, "na_sm_progress()\n");
    do {
        hg_time_t	t1, t2;
        int descriptor = -1;
        int size = 1024 * 1024 * 256; // 256 mb
    
        hg_time_get_current(&t1);

        /* Try to make progress here from the SM unexpected queue */
        descriptor = shm_open(SHM_FILE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (descriptor != -1) {
            ftruncate(descriptor, size);
        }
        else {
            NA_LOG_ERROR("shm_open");
        }
        
        if (ret != NA_SUCCESS) {
            if (ret != NA_TIMEOUT) {
                NA_LOG_ERROR("Could not make unexpected progress");
                return ret;
            }
        } else
            break; /* Progressed */
        
        hg_time_get_current(&t2);
        remaining -= hg_time_to_double(hg_time_subtract(t2, t1));
        
    } while (remaining > 0);
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_complete(struct na_sm_op_id *na_sm_op_id)
{

    struct na_cb_info *callback_info = NULL;    
    na_return_t ret = NA_SUCCESS;
    
    /* Mark op id as completed */
    na_sm_op_id->completed = NA_TRUE;
    
    /* Allocate callback info */
    callback_info = (struct na_cb_info *)malloc(sizeof(struct na_cb_info));
    if (!callback_info) {
        NA_LOG_ERROR("Could not allocate callback info");
        return NA_NOMEM_ERROR;
    }
    callback_info->arg = na_sm_op_id->arg;
    callback_info->ret = ret;
    callback_info->type = na_sm_op_id->type;
    
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_release(struct na_cb_info *callback_info, void *arg)
{

 
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t op_id)
{

    na_return_t ret = NA_SUCCESS;
    return ret;
}
