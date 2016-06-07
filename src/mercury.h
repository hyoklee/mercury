/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#ifndef MERCURY_H
#define MERCURY_H

#include "mercury_core.h"

/*********************/
/* Public Prototypes */
/*********************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Get Mercury version number.
 *
 * \param major [OUT]           pointer to unsigned integer
 * \param minor [OUT]           pointer to unsigned integer
 * \param patch [OUT]           pointer to unsigned integer
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Version_get(
        unsigned int *major,
        unsigned int *minor,
        unsigned int *patch
        );

/**
 * Convert error return code to string (null terminated).
 *
 * \param errnum [IN]           error return code
 *
 * \return String
 */
HG_EXPORT const char *
HG_Error_to_string(
        hg_return_t errnum
        );

/**
 * Initialize the Mercury layer.
 * Must be finalized with HG_Finalize().
 *
 * \param na_info_string [IN]   host address with port number (e.g.,
 *                              "tcp://localhost:3344" or
 *                              "bmi+tcp://localhost:3344")
 * \param na_listen [IN]        listen for incoming connections
 *
 * \return Pointer to HG class or NULL in case of failure
 */
HG_EXPORT hg_class_t *
HG_Init(
        const char *na_info_string,
        hg_bool_t na_listen
        );

/**
 * Initialize the Mercury layer from an existing NA class/context.
 * Must be finalized with HG_Finalize().
 *
 * \param na_class [IN]         pointer to NA class
 * \param na_context [IN]       pointer to NA context
 *
 * \return Pointer to HG class or NULL in case of failure
 */
HG_EXPORT hg_class_t *
HG_Init_na(
        na_class_t *na_class,
        na_context_t *na_context
        );

/**
 * Finalize the Mercury layer.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Finalize(
        hg_class_t *hg_class
        );

/**
 * Obtain the name of the given class.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return the name of the class, or NULL if not a valid class
 */
HG_EXPORT const char *
HG_Class_get_name(
        const hg_class_t *hg_class
        );

/**
 * Obtain the protocol of the given class.
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return the name of the class's transport, or NULL if not a valid class
 */
HG_EXPORT const char *
HG_Class_get_protocol(
        const hg_class_t *hg_class
        );

/**
 * Create a new context. Must be destroyed by calling HG_Context_destroy().
 *
 * \param hg_class [IN]         pointer to HG class
 *
 * \return Pointer to HG context or NULL in case of failure
 */
HG_EXPORT hg_context_t *
HG_Context_create(
        hg_class_t *hg_class
        );

/**
 * Destroy a context created by HG_Context_create().
 *
 * \param context [IN]          pointer to HG context
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Context_destroy(
        hg_context_t *context
        );

/**
 * Retrieve the class used to create the given context
 *
 * \param context [IN]          pointer to HG context
 *
 * \return the associated class
 */
HG_EXPORT hg_class_t *
HG_Context_get_class(
        hg_context_t *context
        );

/**
 * Dynamically register a function func_name as an RPC as well as the
 * RPC callback executed when the RPC request ID associated to func_name is
 * received. Associate input and output proc to function ID, so that they can
 * be used to serialize and deserialize function parameters.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param func_name [IN]        unique name associated to function
 * \param in_proc_cb [IN]       pointer to input proc callback
 * \param out_proc_cb [IN]      pointer to output proc callback
 * \param rpc_cb [IN]           RPC callback
 *
 * \return unique ID associated to the registered function
 */
HG_EXPORT hg_id_t
HG_Register_name(
        hg_class_t *hg_class,
        const char *func_name,
        hg_proc_cb_t in_proc_cb,
        hg_proc_cb_t out_proc_cb,
        hg_rpc_cb_t rpc_cb
        );

/**
 * Dynamically register an RPC ID as well as the RPC callback executed when the
 * RPC request ID is received. Associate input and output proc to id, so that
 * they can be used to serialize and deserialize function parameters.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               ID to use to register RPC
 * \param in_proc_cb [IN]       pointer to input proc callback
 * \param out_proc_cb [IN]      pointer to output proc callback
 * \param rpc_cb [IN]           RPC callback
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Register(
        hg_class_t *hg_class,
        hg_id_t id,
        hg_proc_cb_t in_proc_cb,
        hg_proc_cb_t out_proc_cb,
        hg_rpc_cb_t rpc_cb
        );

/**
 * Register and associate user data to registered function. When HG_Finalize()
 * is called, free_callback (if defined) is called to free the registered
 * data.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 * \param data [IN]             pointer to data
 * \param free_callback [IN]    pointer to function
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Register_data(
        hg_class_t *hg_class,
        hg_id_t id,
        void *data,
        void (*free_callback)(void *)
        );

/**
 * Indicate whether HG_Register_data() has been called and return associated
 * data.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param id [IN]               registered function ID
 *
 * \return Pointer to data or NULL
 */
HG_EXPORT void *
HG_Registered_data(
        hg_class_t *hg_class,
        hg_id_t id
        );

/**
 * Lookup an addr from a peer address/name. Addresses need to be
 * freed by calling HG_Addr_free(). After completion, user callback is
 * placed into a completion queue and can be triggered using HG_Trigger().
 *
 * \param context [IN]          pointer to context of execution
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param name [IN]             lookup name
 * \param op_id [OUT]           pointer to returned operation ID
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Addr_lookup(
        hg_context_t *context,
        hg_cb_t       callback,
        void         *arg,
        const char   *name,
        hg_op_id_t   *op_id
        );

/**
 * Free the addr from the list of peers.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Addr_free(
        hg_class_t *hg_class,
        hg_addr_t   addr
        );

/**
 * Access self address. Address must be freed with HG_Addr_free().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [OUT]            pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Addr_self(
        hg_class_t *hg_class,
        hg_addr_t  *addr
        );

/**
 * Duplicate an existing HG abstract address. The duplicated address can be
 * stored for later use and the origin address be freed safely. The duplicated
 * address must be freed with HG_Addr_free().
 *
 * \param hg_class [IN]         pointer to HG class
 * \param addr [IN]             abstract address
 * \param new_addr [OUT]        pointer to abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Addr_dup(
        hg_class_t *hg_class,
        hg_addr_t   addr,
        hg_addr_t  *new_addr
        );

/**
 * Convert an addr to a string (returned string includes the terminating
 * null byte '\0'). If buf is NULL, the address is not converted and only
 * the required size of the buffer is returned. If the input value passed
 * through buf_size is too small, HG_SIZE_ERROR is returned and the buf_size
 * output is set to the minimum size required.
 *
 * \param hg_class [IN]         pointer to HG class
 * \param buf [IN/OUT]          pointer to destination buffer
 * \param buf_size [IN/OUT]     pointer to buffer size
 * \param addr [IN]             abstract address
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Addr_to_string(
        hg_class_t *hg_class,
        char       *buf,
        hg_size_t  *buf_size,
        hg_addr_t   addr
        );


/**
 * Initiate a new HG RPC using the specified function ID and the local/remote
 * target defined by addr. The HG handle created can be used to query input
 * and output, as well as issuing the RPC by using HG_Forward().
 * After completion the handle must be freed using HG_Destroy().
 *
 * \param context [IN]          pointer to HG context
 * \param addr [IN]             abstract network address of destination
 * \param id [IN]               registered function ID
 * \param handle [OUT]          pointer to HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Create(
        hg_context_t *context,
        hg_addr_t addr,
        hg_id_t id,
        hg_handle_t *handle
        );

/**
 * Destroy HG handle. Resources associated to the handle are freed when the
 * reference count is null.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Destroy(
        hg_handle_t handle
        );

/**
 * Get info from handle.
 * \remark Users must call HG_Addr_dup() to safely re-use the addr field.
 *
 * \param handle [IN]           HG handle
 *
 * \return Pointer to info or NULL in case of failure
 */
HG_EXPORT struct hg_info *
HG_Get_info(
        hg_handle_t handle
        );

/**
 * Get input from handle (requires registration of input proc to deserialize
 * parameters).
 * \remark This is equivalent to:
 *   - HG_Core_get_input()
 *   - Call hg_proc to deserialize parameters
 * Input must be freed using HG_Free_input().
 *
 * \param handle [IN]           HG handle
 * \param in_struct [IN/OUT]    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Get_input(
        hg_handle_t handle,
        void *in_struct
        );

/**
 * Free resources allocated when deserializing the input.
 * User may copy parameters contained in the input structure before calling
 * HG_Free_input().
 *
 * \param handle [IN]           HG handle
 * \param in_struct [IN/OUT]    pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Free_input(
        hg_handle_t handle,
        void *in_struct
        );

/**
 * Get output from handle (requires registration of output proc to deserialize
 * parameters).
 * \remark This is equivalent to:
 *   - HG_Core_get_output()
 *   - Call hg_proc to deserialize parameters
 * Output must be freed using HG_Free_output().
 *
 * \param handle [IN]           HG handle
 * \param out_struct [IN/OUT]   pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Get_output(
        hg_handle_t handle,
        void *out_struct
        );

/**
 * Free resources allocated when deserializing the output.
 * User may copy parameters contained in the output structure before calling
 * HG_Free_output().
 *
 * \param handle [IN]           HG handle
 * \param out_struct [IN/OUT]   pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Free_output(
        hg_handle_t handle,
        void *out_struct
        );

/**
 * Forward a call to a local/remote target using an existing HG handle.
 * Input structure can be passed and parameters serialized using a previously
 * registered input proc. After completion, user callback is placed into a
 * completion queue and can be triggered using HG_Trigger(). RPC output can
 * be queried using HG_Get_output() and freed using HG_Free_output().
 * \remark This routine is internally equivalent to:
 *   - HG_Core_get_input()
 *   - Call hg_proc to serialize parameters
 *   - HG_Core_forward()
 *
 * \param handle [IN]           HG handle
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param in_struct [IN]        pointer to input structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Forward(
        hg_handle_t handle,
        hg_cb_t callback,
        void *arg,
        void *in_struct
        );

/**
 * Respond back to origin using an existing HG handle.
 * Output structure can be passed and parameters serialized using a previously
 * registered output proc. After completion, user callback is placed into a
 * completion queue and can be triggered using HG_Trigger().
 * \remark This routine is internally equivalent to:
 *   - HG_Core_get_output()
 *   - Call hg_proc to serialize parameters
 *   - HG_Core_respond()
 *
 * \param handle [IN]           HG handle
 * \param callback [IN]         pointer to function callback
 * \param arg [IN]              pointer to data passed to callback
 * \param out_struct [IN]       pointer to output structure
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Respond(
        hg_handle_t handle,
        hg_cb_t callback,
        void *arg,
        void *out_struct
        );

/**
 * Try to progress RPC execution for at most timeout until timeout is reached or
 * any completion has occurred.
 * Progress should not be considered as wait, in the sense that it cannot be
 * assumed that completion of a specific operation will occur only when
 * progress is called.
 *
 * \param context [IN]          pointer to HG context
 * \param timeout [IN]          timeout (in milliseconds)
 *
 * \return HG_SUCCESS if any completion has occurred / HG error code otherwise
 */
HG_EXPORT hg_return_t
HG_Progress(
        hg_context_t *context,
        unsigned int timeout
        );

/**
 * Execute at most max_count callbacks. If timeout is non-zero, wait up to
 * timeout before returning. Function can return when at least one or more
 * callbacks are triggered (at most max_count).
 *
 * \param context [IN]          pointer to HG context
 * \param timeout [IN]          timeout (in milliseconds)
 * \param max_count [IN]        maximum number of callbacks triggered
 * \param actual_count [IN]     actual number of callbacks triggered
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Trigger(
        hg_context_t *context,
        unsigned int timeout,
        unsigned int max_count,
        unsigned int *actual_count
        );

/**
 * Cancel an ongoing operation.
 *
 * \param handle [IN]           HG handle
 *
 * \return HG_SUCCESS or corresponding HG error code
 */
HG_EXPORT hg_return_t
HG_Cancel(
        hg_handle_t handle
        );

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_H */
