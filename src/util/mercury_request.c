/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include "mercury_request.h"
#include "mercury_thread_mutex.h"
#include "mercury_thread_condition.h"
#include "mercury_time.h"
#include "mercury_util_error.h"

#include "mercury_atomic.h"

#include <stdlib.h>

struct hg_request_class {
    hg_request_progress_func_t progress_func;
    hg_request_trigger_func_t trigger_func;
    void *arg;
    hg_util_bool_t progressing;
    hg_thread_mutex_t progress_mutex;
    hg_thread_cond_t progress_cond;
};

struct hg_request {
    void *data;
    hg_atomic_int32_t completed;
    hg_request_class_t *request_class;
};

/*---------------------------------------------------------------------------*/
static HG_UTIL_INLINE hg_util_bool_t
hg_request_check(hg_request_t *request)
{
    int trigger_ret = HG_UTIL_FAIL;
    unsigned int trigger_flag = 0;
    hg_util_bool_t ret = HG_UTIL_FALSE;

    do {
        trigger_ret = request->request_class->trigger_func(0, &trigger_flag,
                request->request_class->arg);
    } while ((trigger_ret == HG_UTIL_SUCCESS) && trigger_flag);

    if (hg_atomic_cas32(&request->completed, HG_UTIL_TRUE, HG_UTIL_FALSE)) {
        ret = HG_UTIL_TRUE;
    }
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_request_class_t *
hg_request_init(hg_request_progress_func_t progress_func,
        hg_request_trigger_func_t trigger_func, void *arg)
{
    struct hg_request_class *hg_request_class = NULL;

    hg_request_class = (struct hg_request_class *)
            malloc(sizeof(struct hg_request_class));
    if (!hg_request_class) {
        HG_UTIL_ERROR_DEFAULT("Could not allocate hg_request_class");
        goto done;
    }

    hg_request_class->progress_func = progress_func;
    hg_request_class->trigger_func = trigger_func;
    hg_request_class->arg = arg;
    hg_request_class->progressing = HG_UTIL_FALSE;
    hg_thread_mutex_init(&hg_request_class->progress_mutex);
    hg_thread_cond_init(&hg_request_class->progress_cond);

done:
    return hg_request_class;
}

/*---------------------------------------------------------------------------*/
int
hg_request_finalize(hg_request_class_t *request_class, void **arg)
{
    int ret = HG_UTIL_SUCCESS;

    if (!request_class) goto done;

    if (arg) *arg = request_class->arg;
    hg_thread_mutex_destroy(&request_class->progress_mutex);
    hg_thread_cond_destroy(&request_class->progress_cond);
    free(request_class);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
hg_request_t *
hg_request_create(hg_request_class_t *request_class)
{
    struct hg_request *hg_request = NULL;

    hg_request = (struct hg_request *) malloc(sizeof(struct hg_request));
    if (!hg_request) {
        HG_UTIL_ERROR_DEFAULT("Could not allocate hg_request");
        goto done;
    }

    hg_request->data = NULL;
    hg_atomic_set32(&hg_request->completed, HG_UTIL_FALSE);
    hg_request->request_class = request_class;

done:
    return hg_request;
}

/*---------------------------------------------------------------------------*/
int
hg_request_destroy(hg_request_t *request)
{
    int ret = HG_UTIL_SUCCESS;

    free(request);

    return ret;
}

/*---------------------------------------------------------------------------*/
int
hg_request_complete(hg_request_t *request)
{
    int ret = HG_UTIL_SUCCESS;

    hg_atomic_incr32(&request->completed);

    return ret;
}

/*---------------------------------------------------------------------------*/
/*
 * lock(progress_mutex)
 * while (!completed) {
 *   check_request
 *   if (completed) {
 *     unlock(progress_mutex);
 *     return;
 *     }
 *   if (in_progress) {
 *     wait_cond(progress_cond);
 *     continue;
 *   }
 *   in_progress = true;
 *   unlock(progress_mutex);
 *   trigger;
 *   progress;
 *   lock(progress);
 *   in_progress = false;
 *   signal(progress_cond);
 * }
 * unlock(progress_mutex);
 */

/*---------------------------------------------------------------------------*/
int
hg_request_wait(hg_request_t *request, unsigned int timeout, unsigned int *flag)
{
    double remaining = (double) timeout / 1000.0; /* Convert timeout in ms into seconds */
    hg_util_bool_t completed = HG_UTIL_FALSE;
    int ret = HG_UTIL_SUCCESS;

    hg_thread_mutex_lock(&request->request_class->progress_mutex);

    do {
        hg_time_t t3, t4;
        completed = hg_request_check(request);
        if (completed)
            break;

        if (request->request_class->progressing) {
            hg_time_t t1, t2;

            if (remaining <= 0) {
                /* Timeout occurred so leave */
                break;
            }

            hg_time_get_current(&t1);
            if (hg_thread_cond_timedwait(&request->request_class->progress_cond,
                    &request->request_class->progress_mutex,
                    (unsigned int) (remaining * 1000)) != HG_UTIL_SUCCESS) {
                /* Timeout occurred so leave */
                break;
            }
            hg_time_get_current(&t2);
            remaining -= hg_time_to_double(hg_time_subtract(t2, t1));
            if (remaining < 0)
                break;
            /* Continue as request may have completed in the meantime */
            continue;
        }

        request->request_class->progressing = HG_UTIL_TRUE;

        hg_thread_mutex_unlock(&request->request_class->progress_mutex);

        hg_time_get_current(&t3);

        request->request_class->progress_func(
                (unsigned int) (remaining * 1000),
                request->request_class->arg);

        hg_time_get_current(&t4);
        remaining -= hg_time_to_double(hg_time_subtract(t4, t3));

        hg_thread_mutex_lock(&request->request_class->progress_mutex);
        request->request_class->progressing = HG_UTIL_FALSE;
        hg_thread_cond_broadcast(&request->request_class->progress_cond);

    } while (!completed && (remaining > 0));
    hg_thread_mutex_unlock(&request->request_class->progress_mutex);

    if (flag) *flag = completed;
    return ret;
}

/*---------------------------------------------------------------------------*/
int
hg_request_waitall(int count, hg_request_t *request[], unsigned int timeout,
        unsigned int *flag)
{
    /* TODO */
    int i;
    for (i = 0; i < count; i++)
        hg_request_wait(request[i], timeout, flag);
    return HG_UTIL_SUCCESS;
}

/*---------------------------------------------------------------------------*/
hg_return_t
hg_request_complete_cb(const struct hg_cb_info *cb_info)
{
    int ret = hg_request_complete(cb_info->arg);
    return ret == HG_UTIL_SUCCESS ? HG_SUCCESS : HG_OTHER_ERROR;
}

/*---------------------------------------------------------------------------*/
int
hg_request_set_data(hg_request_t *request, void *data)
{
    int ret = HG_UTIL_SUCCESS;

    request->data = data;

    return ret;
}

/*---------------------------------------------------------------------------*/
void *
hg_request_get_data(hg_request_t *request)
{
    void *ret = NULL;

    ret = request->data;

    return ret;
}
