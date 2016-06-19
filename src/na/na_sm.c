/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                         UChicago Argonne, LLC and The HDF Group.
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
#include <fcntl.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <linux/limits.h>
#include <sys/epoll.h>

/****************/
/* Local Macros */
/****************/

/* Msg sizes */
#define NA_SM_UNEXPECTED_SIZE 4096
#define NA_SM_EXPECTED_SIZE  NA_SM_UNEXPECTED_SIZE 
#define NA_SM_EPOLL_MAX_EVENTS 64

/* Max tag */
#define NA_SM_MAX_TAG (NA_TAG_UB >> 2)
#define SHM_FILE "/mercury.shm"

#define NA_SM_PRIVATE_DATA(na_class) \
    ((struct na_sm_private_data *)(na_class->private_data))

#define LINUX 1
/************************************/
/* Local Type and Struct Definition */
/************************************/
typedef struct na_sm_op_id na_sm_op_id_t;
typedef struct na_sm_addr na_sm_addr_t;
typedef struct na_sm_mem_handle na_sm_mem_handle_t;

/* na_sm_addr */
struct na_sm_addr {
    na_sm_op_id_t	*na_sm_op_id;	/* For addr_lookup() */    
    pid_t pid;             /* remote process id */
    char* sm_path;         /* Path to shared memory */
    na_bool_t  unexpected; /* Address generated from unexpected recv */
    na_bool_t  self;       /* Boolean for self */
};

struct na_sm_mem_handle {
    na_ptr_t base;     /* Initial address of memory */
    na_size_t size;    /* Size of memory */
    pid_t pid;         /* remote process id */
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
    na_sm_op_id_t *op_id; /* SM operation id */
    size_t buf_size;
    size_t actual_size;    
};

struct na_sm_info_put {

};

struct na_sm_info_get {
    na_bool_t   internal_progress;
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
  // char *listen_addr;                            /* Listen addr */

  // hg_thread_mutex_t test_unexpected_mutex;      /* Mutex */
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
na_sm_mem_publish(
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
        na_sm_mem_publish,                   /* mem_publish */
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

    int descriptor = -1;
    int size = 1024 * 1024 * 256; // 256 mb
    fprintf(stderr, ">na_sm_initialize()\n");    

    descriptor = shm_open(SHM_FILE, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, size);
    }
    else {
        NA_LOG_ERROR("shm_open() failed.");
    }
    
    int mmap_flags = MAP_SHARED;        
    char *result = mmap(NULL, (size_t) size, PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);
    
    if (result == MAP_FAILED) {
        NA_LOG_ERROR("mmap failed().");        
        return NA_PROTOCOL_ERROR;
    }
    
    ret = na_sm_init(na_class);
    return ret;
    
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_init(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;
    hg_queue_t *unexpected_msg_queue = NULL;
    hg_queue_t *unexpected_op_queue = NULL;
    
    na_class->private_data = malloc(sizeof(struct na_sm_private_data));
    if (!na_class->private_data) {
        NA_LOG_ERROR("Could not allocate NA private data class");
        return NA_NOMEM_ERROR;
    }
    
    unexpected_msg_queue = hg_queue_new();
    if (!unexpected_msg_queue) {
        NA_LOG_ERROR("Could not create unexpected message queue");
        free(na_class->private_data);        
        return NA_NOMEM_ERROR;
    }
    NA_SM_PRIVATE_DATA(na_class)->unexpected_msg_queue = unexpected_msg_queue;

    /* Create queue for making progress on operation IDs */
    unexpected_op_queue = hg_queue_new();
    if (!unexpected_op_queue) {
        NA_LOG_ERROR("Could not create unexpected op queue");
        free(na_class->private_data);
        hg_queue_free(unexpected_msg_queue);
        return NA_NOMEM_ERROR;
    }
    
    NA_SM_PRIVATE_DATA(na_class)->unexpected_op_queue = unexpected_op_queue;
    
    
    hg_thread_mutex_init(
            &NA_SM_PRIVATE_DATA(na_class)->unexpected_msg_queue_mutex);
    hg_thread_mutex_init(
            &NA_SM_PRIVATE_DATA(na_class)->unexpected_op_queue_mutex);
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_finalize(na_class_t *na_class)
{
    na_return_t ret = NA_SUCCESS;
    // munmap(result, size);
    shm_unlink(SHM_FILE);
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

    fprintf(stderr, ">na_sm_addr_lookup(name=%s)\n", name);    

    /* Allocate op_id */
    na_sm_op_id = (na_sm_op_id_t *) calloc(1, sizeof(*na_sm_op_id));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_LOOKUP;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->canceled = 0;
    
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
    na_sm_addr->self = NA_FALSE;
    na_sm_op_id->info.lookup.addr = (na_addr_t) na_sm_addr;
    ret = na_sm_complete(na_sm_op_id);
    if (ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not complete operation");
        free(na_sm_addr);
        free(na_sm_op_id);
        return ret;
    }
    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    // *op_id = (na_op_id_t) na_sm_op_id;
    
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_self(na_class_t NA_UNUSED *na_class, na_addr_t *addr)
{

    struct na_sm_addr *na_sm_addr = NULL;
    na_return_t ret = NA_SUCCESS;

    fprintf(stderr, ">na_sm_addr_self()\n");
    na_sm_addr = (struct na_sm_addr *) malloc(sizeof(struct na_sm_addr));
    if (!na_sm_addr) {
        NA_LOG_ERROR("Could not allocate SM addr");
        return NA_NOMEM_ERROR;
    }
    na_sm_addr->pid = 0;
    na_sm_addr->sm_path = NULL;
    na_sm_addr->unexpected = NA_FALSE;
    na_sm_addr->self = NA_TRUE;
    *addr = (na_addr_t) na_sm_addr;    
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
    if (!na_sm_addr->sm_path) {
        free(na_sm_addr->sm_path);
    }
    // free(na_sm_addr);           /* segmentaiton fault. */
    na_sm_addr = NULL;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_bool_t
na_sm_addr_is_self(na_class_t NA_UNUSED *na_class, na_addr_t addr)
{

    struct na_sm_addr *na_sm_addr = (struct na_sm_addr *) addr;
    return na_sm_addr->self;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_addr_to_string(na_class_t *na_class, char *buf,
        na_size_t *buf_size, na_addr_t addr)
{
    na_sm_addr_t *na_sm_addr = (na_sm_addr_t *)addr;    
    na_return_t ret = NA_SUCCESS;
    na_size_t string_len;

    fprintf(stderr, ">na_sm_addr_to_string()\n");
    string_len = strlen(na_sm_addr->sm_path);
    if (buf) {
        if (string_len >= *buf_size) {
            NA_LOG_ERROR("Buffer size too small to copy addr");
            ret = NA_SIZE_ERROR;
        } else {
            strncpy(buf, na_sm_addr->sm_path, string_len);
        }
    }
    *buf_size = string_len + 1;
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
    struct na_sm_op_id *na_sm_op_id = NULL;    
    struct na_sm_addr *na_sm_addr = (struct na_sm_addr*) dest;
    na_return_t ret = NA_SUCCESS;
    struct iovec local[1];
    struct iovec remote[1];
    int mmap_flags = MAP_SHARED;        
    ssize_t nwrite=0;

    fprintf(stderr, ">na_sm_msg_send_unexpected(na_sm_addr->pid=%d na_sm_addr->sm_path=%s buf=%s buf_size=%d tag=%d)\n", na_sm_addr->pid, na_sm_addr->sm_path, (char*) buf,  buf_size, tag);

    /* Allocate op_id */
    na_sm_op_id = (na_sm_op_id_t *) calloc(1, sizeof(*na_sm_op_id));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_SEND_UNEXPECTED;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->completed = NA_FALSE;
    na_sm_op_id->info.send_unexpected.op_id = 0;
    na_sm_op_id->canceled = NA_FALSE;

    /* Post the SM send request */
    fprintf(stderr, "I will post unexpected send request here.\n");
    int sm_ret = 1;

    pid_t pid = na_sm_addr->pid; 

    int descriptor = -1;    
    descriptor = shm_open("/mercury_recv_unexpected.shm", O_CREAT | O_RDWR,
			  S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, strlen(buf));
    }
    else {
        NA_LOG_ERROR("shm_open() failed.");
    }

    // TO-DO: mmap should be done by server and exposed to client.
    char *result = mmap(NULL, strlen(buf), PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);

    local[0].iov_base = buf;
    local[0].iov_len = strlen(buf);

    remote[0].iov_base = result;
    remote[0].iov_len = strlen(buf);

    nwrite = process_vm_writev(pid, local, 1, remote, 1, 0);

    /* If immediate completion, directly add to completion queue */
    if (sm_ret > 0) {
        ret = na_sm_complete(na_sm_op_id);
        if (ret != NA_SUCCESS) {
            NA_LOG_ERROR("Could not complete operation");
            free(na_sm_op_id);
            return ret;
        }
    }

    /* Assign op_id */
    // *op_id = (na_op_id_t) na_sm_op_id;
    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_recv_unexpected(na_class_t *na_class, na_context_t *context,
        na_cb_t callback, void *arg, void *buf, na_size_t buf_size,
        na_op_id_t *op_id)
{
    int efd = -1;		/* epoll descriptor */
    int ready = -1;
    int s = -1;
    struct epoll_event event;
    struct epoll_event events[NA_SM_EPOLL_MAX_EVENTS];

    int descriptor = -1;    	/* shared memory descriptor */

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

    /* Open a shared memory. */
    descriptor = shm_open("/mercury_recv_unexpected.shm", O_CREAT | O_RDWR,
			  S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, buf_size);
    }
    else {
        NA_LOG_ERROR("shm_open() failed.");
    }

    int mmap_flags = MAP_SHARED;
    char *result = mmap(NULL, (size_t) buf_size,
                        PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);
    /* Publish memory location via pipe. */


    if (result == MAP_FAILED) {
        NA_LOG_ERROR("mmap() failed.");        
        return NA_PROTOCOL_ERROR;
    }
    int not_changed = 1;
    while (not_changed){
      /* Check buffer change. */
      fprintf(stderr, "=na_sm_msg_recv_unexpected():result[0] = %c\n", result[0]);
      if (result[0] != 0) {
	fprintf(stderr, "=na_sm_msg_recv_unexpected:result[0] is now %c\n", result[0]);
	not_changed = 0;
      }
    }
      
#if 0
    /* Use epoll to check if something is received. */
    efd = epoll_create1(0);
    if (efd == -1)
      {
	perror ("epoll_create");
	abort ();
      }
    event.data.fd = descriptor;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(efd, EPOLL_CTL_ADD, descriptor, &event);
    if (s == -1)
      {
	perror ("epoll_ctl");
	abort ();
      }

    ret = epoll_wait(efd, events, NA_SM_EPOLL_MAX_EVENTS, 0);
    if (ret > 0) {
      int count = ret, i, ret2;

      fprintf(stderr, "%s: epoll_wait() found %d events",
	    __func__, count);
      for (i = 0; i < count; i++) {
	if (events[i].events & EPOLLIN)
	  {
	    if  (descriptor == events[i].data.fd){
	      fprintf(stderr, "got events on shared memory.\n");
	    }
	    else {
	      fprintf(stderr, "got events on fd = %d.\n", events[i].data.fd);
	    }
	  }
      }
    }
#endif

    /* Push it into queue. */
    hg_thread_mutex_lock(&NA_SM_PRIVATE_DATA(na_class)->unexpected_op_queue_mutex);

    if (hg_queue_push_head(NA_SM_PRIVATE_DATA(na_class)->unexpected_op_queue,
            (hg_queue_value_t) na_sm_op_id) != HG_UTIL_SUCCESS) {
        NA_LOG_ERROR("Could not push ID to unexpected op queue");
        ret = NA_NOMEM_ERROR;
    }

    hg_thread_mutex_unlock(
            &NA_SM_PRIVATE_DATA(na_class)->unexpected_op_queue_mutex);

    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    // *op_id = (na_op_id_t) na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_send_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
        na_cb_t callback, void *arg, const void *buf, na_size_t buf_size,
        na_addr_t dest, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_sm_op_id *na_sm_op_id = NULL;    
    struct na_sm_addr *na_sm_addr = (struct na_sm_addr*) dest;
    na_return_t ret = NA_SUCCESS;
    struct iovec local[1];
    struct iovec remote[1];
    int mmap_flags = MAP_SHARED;        
    ssize_t nwrite=0;

    /* Allocate op_id */
    na_sm_op_id = (na_sm_op_id_t *) calloc(1, sizeof(*na_sm_op_id));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_SEND_EXPECTED;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->completed = NA_FALSE;
    na_sm_op_id->info.send_expected.op_id = 0;
    na_sm_op_id->canceled = NA_FALSE;

    /* Post the SM send request */
    fprintf(stderr, "I will post expected send request here.\n");
    int sm_ret = 1;

    // Seg faul on server.
    // pid_t pid = na_sm_addr->pid; 
    pid_t pid = getpid();

    int descriptor = -1;    
    descriptor = shm_open("/mercury_recv_expected.shm", O_CREAT | O_RDWR,
			  S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, strlen(buf));
    }
    else {
        NA_LOG_ERROR("shm_open() failed.");
    }

    char *result = mmap(NULL, strlen(buf), PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);

    local[0].iov_base = buf;
    local[0].iov_len = strlen(buf);

    remote[0].iov_base = result;
    remote[0].iov_len = strlen(buf);

    nwrite = process_vm_writev(pid, local, 1, remote, 1, 0);

    /* If immediate completion, directly add to completion queue */
    if (sm_ret > 0) {
        ret = na_sm_complete(na_sm_op_id);
        if (ret != NA_SUCCESS) {
            NA_LOG_ERROR("Could not complete operation");
            free(na_sm_op_id);
            return ret;
        }
    }

    /* Assign op_id */
    // *op_id = (na_op_id_t) na_sm_op_id;
    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_msg_recv_expected(na_class_t NA_UNUSED *na_class, na_context_t *context,
        na_cb_t callback, void *arg, void *buf, na_size_t buf_size,
        na_addr_t source, na_tag_t tag, na_op_id_t *op_id)
{
    struct na_sm_op_id *na_sm_op_id = NULL;        
    struct na_sm_addr *na_sm_addr = (struct na_sm_addr*) source;
    na_return_t ret = NA_SUCCESS;
    int sm_ret = 0;
    int descriptor = -1;    

    // This causes segmentation fault error on server.
    // I think it happens because msg_unexpected_recv_cb did not initialize 
    //     params->source_addr = callback_info->info.recv_unexpected.source;
    // in msg_unexpected_recv_cb().
    // fprintf(stderr, ">na_sm_msg_recv_expected(na_sm_addr->pid=%d na_sm_addr->sm_path=%s buf_size=%d)\n", na_sm_addr->pid, na_sm_addr->sm_path, buf_size);

    /* Allocate na_op_id */
    na_sm_op_id = (struct na_sm_op_id *) malloc(sizeof(struct na_sm_op_id));    
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_RECV_EXPECTED;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->completed = NA_FALSE;
    na_sm_op_id->info.recv_expected.op_id = 0;
    na_sm_op_id->info.recv_expected.buf_size = buf_size;
    na_sm_op_id->info.recv_expected.actual_size = 0;
    na_sm_op_id->canceled = NA_FALSE;

    /* Post the SM recv request. */
    // descriptor = shm_open(na_sm_addr->sm_path, O_CREAT | O_RDWR,
    descriptor = shm_open("/mercury_recv_expected.shm", O_CREAT | O_RDWR,
			  S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, buf_size);
    }
    else {
        NA_LOG_ERROR("shm_open() failed.");
    }

    int mmap_flags = MAP_SHARED;
    char *result = mmap(NULL, (size_t) buf_size,
                        PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);
    if (result == MAP_FAILED) {
        NA_LOG_ERROR("mmap failed().");        
        return NA_PROTOCOL_ERROR;
    }

    sm_ret = 1;
#if 0
    // You cannot busy wait for receive expected. Both server and client will starve. 
    int not_changed = 1;
    while (not_changed){
      /* Check buffer change. */
      fprintf(stderr, "=na_sm_msg_recv_expected():result[0] = %x\n", result[0]);
      if (result[0] != 0) {
	fprintf(stderr, "=na_sm_msg_recv_expected:result[0] is now %c\n", result[0]);
	not_changed = 0;
      }
    }
#endif
    /* If immediate completion, directly add to completion queue */
    if (sm_ret > 0) {
        ret = na_sm_complete(na_sm_op_id);
        if (ret != NA_SUCCESS) {
            NA_LOG_ERROR("Could not complete operation");
            free(na_sm_op_id);
            return ret;
        }
    }


    /* Assign op_id */
    // *op_id = (na_op_id_t) na_sm_op_id;
    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_handle_create(na_class_t NA_UNUSED *na_class, void *buf,
        na_size_t buf_size, unsigned long flags, na_mem_handle_t *mem_handle)
{

    na_return_t ret = NA_SUCCESS;
    na_ptr_t sm_buf_base = (na_ptr_t) buf;
    struct na_sm_mem_handle *na_sm_mem_handle = NULL;
    size_t sm_buf_size = buf_size;

    /* Allocate memory handle (use calloc to avoid uninitialized transfer) */
    na_sm_mem_handle = (struct na_sm_mem_handle*)
            calloc(1, sizeof(struct na_sm_mem_handle));
    if (!na_sm_mem_handle) {
          NA_LOG_ERROR("Could not allocate NA SM memory handle");
          return NA_NOMEM_ERROR;
    }
    na_sm_mem_handle->base = sm_buf_base;
    na_sm_mem_handle->size = sm_buf_size;
    na_sm_mem_handle->attr = flags;
    *mem_handle = (na_mem_handle_t) na_sm_mem_handle;
    
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
na_sm_mem_register(na_class_t *na_class, na_mem_handle_t mem_handle)
{
    int descriptor = -1;    
    na_sm_mem_handle_t *na_sm_mem_handle = mem_handle;
    descriptor = shm_open("/mercury_bulk.shm", O_CREAT | O_RDWR,
                          S_IRUSR | S_IWUSR);
    if (descriptor != -1) {
        ftruncate(descriptor, na_sm_mem_handle->size);
    }
    int mmap_flags = MAP_SHARED;
    char *result = mmap(NULL, (size_t) na_sm_mem_handle->size,
                        PROT_WRITE | PROT_READ, mmap_flags,
                        descriptor, 0);
    memcpy(result, na_sm_mem_handle->base, na_sm_mem_handle->size);
    int* buf = (int*)na_sm_mem_handle->base;
    int i = 0;
    for (i = 0; i < 3; i++) {
    	printf("%d=%d\n",i, buf[i]);
    }
    if (result == MAP_FAILED) {
        NA_LOG_ERROR("mmap failed().");
        return NA_PROTOCOL_ERROR;
    }
    na_sm_mem_handle->base = result;
    int* buf2 = (int*)na_sm_mem_handle->base;
    for (i = 0; i < 3; i++) {
    	printf("%d=%d\n",i, buf2[i]);
    }

    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_publish(na_class_t *na_class, na_mem_handle_t mem_handle)
{
    na_sm_mem_handle_t *na_sm_mem_handle = mem_handle;    
    fprintf(stderr, ">na_sm_mem_publish():%zd\n", na_sm_mem_handle->base);
    /* Create named pipe. */
    char *myfifo = "/tmp/mercury_fifo";
    char str[BUFSIZ];
    int client_to_server = open(myfifo, O_WRONLY);
    pid_t pid = getpid();
    fprintf(stderr, "my pid = %d\n", pid);
    if (client_to_server == -1) {
        fprintf(stderr, "open failed\n");
        fprintf(stderr, "Error no is : %d\n", errno);
        fprintf(stderr, "Error description is : %s\n",strerror(errno));     
    }
    sprintf(str, "%d,%zd,%d", pid, na_sm_mem_handle->base,
            na_sm_mem_handle->size);
    write(client_to_server, str, sizeof(str));    
    sprintf(str, "exit");
    write(client_to_server, str, sizeof(str));

    close(client_to_server);    
    return NA_SUCCESS;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_mem_deregister(na_class_t NA_UNUSED *na_class, na_mem_handle_t mem_handle)
{
    na_sm_mem_handle_t *na_sm_mem_handle = mem_handle;
    fprintf(stderr, ">na_sm_mem_deregister()\n");
    int ret = munmap(na_sm_mem_handle->base, na_sm_mem_handle->size);
    if (ret == 0) {
        return NA_SUCCESS;
    }
    else {
        NA_LOG_ERROR("munmap failed().");
        return NA_PROTOCOL_ERROR;        
    }
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
    na_return_t ret = NA_SUCCESS;    
    struct na_sm_mem_handle *na_sm_mem_handle =
            (struct na_sm_mem_handle*) mem_handle;


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
    int client_to_server;
    int fret;
    char *myfifo = "/tmp/mercury_fifo";
    char fifobuf[BUFSIZ];
    char handle_info_buf[BUFSIZ];
       
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


    fret = mkfifo(myfifo, 0666);
    if (fret == -1){
        fprintf(stderr, "mkfifo failed\n");
        fprintf(stderr, "Error no is : %d\n", errno);
        fprintf(stderr, "Error description is : %s\n", strerror(errno));     
    }

   client_to_server = open(myfifo, O_RDONLY);    
   while (1)
        {
            read(client_to_server, fifobuf, BUFSIZ);

            if (strcmp("exit",fifobuf)==0)
                {
                    printf("Server OFF.\n");
                    break;
                }

            else if (strcmp("",fifobuf)!=0)
                {
                    printf("Received: %s\n", fifobuf);
                    strncpy(handle_info_buf, fifobuf, strlen(fifobuf));
                }

            /* clean buf from any data */
            memset(fifobuf, 0, sizeof(fifobuf));
        }
   close(client_to_server);
   unlink(myfifo);
   char *brkt = NULL;
   brkt = strtok(handle_info_buf, ",");
   na_sm_mem_handle->pid = atoi(brkt);
   brkt = strtok(NULL, ",");
   na_sm_mem_handle->base = atol(brkt);
   brkt = strtok(NULL, ",");
   na_sm_mem_handle->size = atoi(brkt);
    *mem_handle = (na_mem_handle_t) na_sm_mem_handle;   
  fprintf(stderr, "<na_sm_mem_handle_deserialize()\n");
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
    char buf[100];
    struct iovec remote[1];
    struct iovec local[1];
    ssize_t nwrite=0;
    na_return_t ret = NA_SUCCESS;
    struct na_sm_op_id *na_sm_op_id = NULL;
    na_sm_mem_handle_t *na_sm_mem_handle_local = local_mem_handle;
    na_sm_mem_handle_t *na_sm_mem_handle_remote = remote_mem_handle;

    for(int i = 0; i < 100; i++)
      buf[i] = i;

    /* Allocate op_id */
    na_sm_op_id = (struct na_sm_op_id *) malloc(sizeof(struct na_sm_op_id));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM PUT operation ID");
        return NA_NOMEM_ERROR;
    }
    
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_PUT;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->completed = NA_FALSE;
    na_sm_op_id->canceled = 0;
    
    pid_t pid = na_sm_mem_handle_remote->pid; 
    // local[0].iov_base = na_sm_mem_handle_local->base; 
    local[0].iov_base = buf;
    // local[0].iov_len = na_sm_mem_handle_local->size;
    local[0].iov_len = 100;

    remote[0].iov_base = na_sm_mem_handle_remote->base;
    remote[0].iov_len = 100;
    // remote[0].iov_len = na_sm_mem_handle_remote->size;
    // #ifdef LINUX    
    nwrite = process_vm_writev(pid, local, 1, remote, 1, 0);
    // #endif    
    if (nwrite < 1){
        perror("process_vm_writev()");
    }
    else {
      fprintf(stderr, "pid=%d, local->size=%d, remote->base=0x%llx, remote->size=%d, nwrite=%d\n",
	      pid,
	      na_sm_mem_handle_local->size,            
	      na_sm_mem_handle_remote->base,
	      na_sm_mem_handle_remote->size,
	      nwrite);
      for(int i = 0; i < nwrite; i++)
	fprintf(stderr, "%x\n", buf[i]);

    }


    ret = na_sm_complete(na_sm_op_id);
    
    /* Assign op_id */
    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    // *op_id = (na_op_id_t) na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_get(na_class_t *na_class, na_context_t *context, na_cb_t callback,
        void *arg, na_mem_handle_t local_mem_handle, na_offset_t local_offset,
        na_mem_handle_t remote_mem_handle, na_offset_t remote_offset,
        na_size_t length, na_addr_t remote_addr, na_op_id_t *op_id)
{
    char buf[100];
    struct iovec remote[1];
    struct iovec local[1];
    ssize_t nread=0;
    na_return_t ret = NA_SUCCESS;
    struct na_sm_op_id *na_sm_op_id = NULL;
    na_sm_mem_handle_t *na_sm_mem_handle_local = local_mem_handle;
    na_sm_mem_handle_t *na_sm_mem_handle_remote = remote_mem_handle;
    /* Allocate op_id */
    na_sm_op_id = (struct na_sm_op_id *) malloc(sizeof(struct na_sm_op_id));
    if (!na_sm_op_id) {
        NA_LOG_ERROR("Could not allocate NA SM GET operation ID");
        return NA_NOMEM_ERROR;
    }
    na_sm_op_id->context = context;
    na_sm_op_id->type = NA_CB_GET;
    na_sm_op_id->callback = callback;
    na_sm_op_id->arg = arg;
    na_sm_op_id->completed = NA_FALSE;
    na_sm_op_id->canceled = 0;

    
    pid_t pid = na_sm_mem_handle_remote->pid; 
    // pid_t pid = getpid(); 
    // local[0].iov_base = na_sm_mem_handle_local->base; 
    local[0].iov_base = buf;
    // local[0].iov_len = na_sm_mem_handle_local->size;
    local[0].iov_len = 100;
    
    // remote[0].iov_base = (void *) 0x00400000;
    remote[0].iov_base = na_sm_mem_handle_remote->base;
    remote[0].iov_len = 100;
    // remote[0].iov_len = na_sm_mem_handle_remote->size;
    // #ifdef LINUX
    nread = process_vm_readv(pid, local, 1, remote, 1, 0);
    // #endif    
    if (nread == 0){
        perror("process_vm_readv()");
    }
    else {
      fprintf(stderr, "pid=%d, local->size=%d, remote->base=0x%llx, remote->size=%d, nread=%d\n",
	      pid,
	      na_sm_mem_handle_local->size,            
	      na_sm_mem_handle_remote->base,
	      na_sm_mem_handle_remote->size,
	      nread);
      for(int i = 0; i < nread; i++)
	fprintf(stderr, "%x\n", buf[i]);

    }

    ret = na_sm_complete(na_sm_op_id);
    if (ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not complete operation");
        free(na_sm_op_id);
    }
    /* Assign op_id */
    if (op_id && op_id != NA_OP_ID_IGNORE) *op_id = na_sm_op_id;
    // *op_id = (na_op_id_t) na_sm_op_id;
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_progress(na_class_t *na_class, na_context_t *context,
               unsigned int timeout)
{
    struct na_sm_op_id *na_sm_op_id = NULL;
    
    na_return_t ret = NA_SUCCESS;
    /* Convert timeout in ms into seconds. */    
    double remaining = timeout / 1000.0;
			
    // fprintf(stderr, "na_sm_progress()\n");
    do {
        hg_time_t	t1, t2;
        hg_queue_value_t queue_value;
        hg_time_get_current(&t1);
        queue_value =
            hg_queue_pop_tail(
                              NA_SM_PRIVATE_DATA(na_class)
                              ->unexpected_op_queue);

        /* Try to make progress here from the SM unexpected queue */
        na_sm_op_id = (queue_value != HG_QUEUE_NULL) ?
            (struct na_sm_op_id *) queue_value : NULL;

        if (na_sm_op_id) {
            switch (na_sm_op_id->type) {
            case NA_CB_LOOKUP:
                NA_LOG_ERROR("Should not complete lookup here");
                break;
            case NA_CB_RECV_UNEXPECTED:
                ret = na_sm_complete(na_sm_op_id);
                break;
            default:
                NA_LOG_ERROR("Unknown type of operation ID");
                ret = NA_PROTOCOL_ERROR;
            
            }
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
        
    } while (remaining > 0 && ret != NA_SUCCESS);
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_complete(struct na_sm_op_id *na_sm_op_id)
{

    struct na_cb_info *callback_info = NULL;    
    na_return_t ret = NA_SUCCESS;

    if (na_sm_op_id == NULL){
        NA_LOG_ERROR("na_sm_op_id is NULL.");        
	return NA_INVALID_PARAM;
    }
        
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

    switch (na_sm_op_id->type) {
    case NA_CB_LOOKUP:
        callback_info->info.lookup.addr = na_sm_op_id->info.lookup.addr;
        NA_LOG_ERROR("Got NA_CB_LOOKUP.");
        break;
    case NA_CB_RECV_EXPECTED:
        NA_LOG_ERROR("Got NA_CB_RECV_EXPECTED.");
        break;
    case NA_CB_SEND_EXPECTED:
        NA_LOG_ERROR("Got NA_CB_SEND_EXPECTED.");
        break;
    case NA_CB_RECV_UNEXPECTED:
        callback_info->info.recv_unexpected.source = 22222;
	callback_info->info.recv_unexpected.actual_buf_size = 0;
	callback_info->info.recv_unexpected.tag = 3;

        NA_LOG_ERROR("Got NA_CB_RECV_UNEXPECTED.");
        break;        
    case NA_CB_SEND_UNEXPECTED:
        NA_LOG_ERROR("Got NA_CB_SEND_UNEXPECTED.");
        break;
    case NA_CB_PUT:
        NA_LOG_ERROR("Got NA_CB_PUT.");
        /* Transfer is now done so free RMA info */
        // free(na_sm_op_id->info.put.rma_info);
        // na_sm_op_id->info.put.rma_info = NULL;
        break;
    case NA_CB_GET:
        NA_LOG_ERROR("Got NA_CB_GET.");        
        /* Transfer is now done so free RMA info */
        // free(na_sm_op_id->info.get);
	// na_sm_op_id->info.get = NULL;
        // na_sm_op_id->info.get.rma_info = NULL;
        break;
    default:
        NA_LOG_ERROR("Operation not supported");
        ret = NA_INVALID_PARAM;
        break;
    }
    ret = na_cb_completion_add(na_sm_op_id->context, na_sm_op_id->callback,
                               callback_info, &na_sm_release, na_sm_op_id);
    if (ret != NA_SUCCESS) {
        NA_LOG_ERROR("Could not add callback to completion queue");
        free(callback_info);
    }
    
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_sm_release(struct na_cb_info *callback_info, void *arg)
{
    fprintf(stderr, ">na_sm_release()\n");
    struct na_sm_op_id *na_sm_op_id = (struct na_sm_op_id *) arg;

    if (na_sm_op_id && !na_sm_op_id->completed) {
        NA_LOG_ERROR("Releasing resources from an uncompleted operation");
    }
    free(callback_info);
    free(na_sm_op_id);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_sm_cancel(na_class_t *na_class, na_context_t *context, na_op_id_t op_id)
{

    na_return_t ret = NA_CANCELED;
    return ret;
}
