/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#ifndef MERCURY_ATOMIC_H
#define MERCURY_ATOMIC_H

#include "mercury_util_config.h"

#if defined(_WIN32)
  #include <windows.h>
  typedef struct { volatile LONG value; } hg_atomic_int32_t;
#elif defined(__APPLE__)
  #include <libkern/OSAtomic.h>
  typedef struct { volatile hg_util_int32_t value; } hg_atomic_int32_t;
#elif defined(HG_UTIL_HAS_OPA_PRIMITIVES_H)
  #include <opa_primitives.h>
  typedef OPA_int_t hg_atomic_int32_t;
#elif defined(HG_UTIL_HAS_STDATOMIC_H)
  #include <stdatomic.h>
  typedef _Atomic hg_util_int32_t hg_atomic_int32_t;
#else
  #error "Mercury atomics require OPA or <stdatomic.h> to be present."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set atomic value (32-bit integer).
 *
 * \param ptr [OUT]             pointer to an atomic32 integer
 * \param value [IN]            value
 */
static HG_UTIL_INLINE void
hg_atomic_set32(hg_atomic_int32_t *ptr, hg_util_int32_t value)
{
#if defined(_WIN32) || defined(__APPLE__)
    ptr->value = value;
#elif defined(HG_UTIL_HAS_OPA_PRIMITIVES_H)
    OPA_store_int(ptr, value);
#elif defined(HG_UTIL_HAS_STDATOMIC_H)
    atomic_store(ptr, value);
#else
  #error "Mercury atomics require OPA or <stdatomic.h> to be present."
#endif
}

/**
 * Get atomic value (32-bit integer).
 *
 * \param ptr [OUT]             pointer to an atomic32 integer
 *
 * \return Value of the atomic integer
 */
static HG_UTIL_INLINE hg_util_int32_t
hg_atomic_get32(hg_atomic_int32_t *ptr)
{
    hg_util_int32_t ret;

#if defined(_WIN32) || defined(__APPLE__)
    ret = ptr->value;
#elif defined(HG_UTIL_HAS_OPA_PRIMITIVES_H)
    ret = OPA_load_int(ptr);
#elif defined(HG_UTIL_HAS_STDATOMIC_H)
    ret = atomic_load(ptr);
#else
  #error "Mercury atomics require OPA or <stdatomic.h> to be present."
#endif

    return ret;
}

/**
 * Increment atomic value (32-bit integer).
 *
 * \param ptr [IN/OUT]          pointer to an atomic32 integer
 *
 * \return Incremented value
 */
static HG_UTIL_INLINE hg_util_int32_t
hg_atomic_incr32(hg_atomic_int32_t *ptr)
{
    hg_util_int32_t ret;

#if defined(_WIN32)
    ret = InterlockedIncrement(&ptr->value);
#elif defined(__APPLE__)
    ret = OSAtomicIncrement32(&ptr->value);
#elif defined(HG_UTIL_HAS_OPA_PRIMITIVES_H)
    ret = OPA_fetch_and_incr_int(ptr) + 1;
#elif defined(HG_UTIL_HAS_STDATOMIC_H)
    ret = atomic_fetch_add(ptr, 1) + 1;
#else
  #error "Mercury atomics require OPA or <stdatomic.h> to be present."
#endif

    return ret;
}

/**
 * Decrement atomic value (32-bit integer).
 *
 * \param ptr [IN/OUT]          pointer to an atomic32 integer
 *
 * \return Decremented value
 */
static HG_UTIL_INLINE hg_util_int32_t
hg_atomic_decr32(hg_atomic_int32_t *ptr)
{
    hg_util_int32_t ret;

#if defined(_WIN32)
    ret = InterlockedDecrement(&ptr->value);
#elif defined(__APPLE__)
    ret = OSAtomicDecrement32(&ptr->value);
#elif defined(HG_UTIL_HAS_OPA_PRIMITIVES_H)
    ret = OPA_fetch_and_decr_int(ptr) - 1;
#elif defined(HG_UTIL_HAS_STDATOMIC_H)
    ret = atomic_fetch_sub(ptr, 1) - 1;
#else
  #error "Mercury atomics require OPA or <stdatomic.h> to be present."
#endif

    return ret;
}

/**
 * Compare and swap values (32-bit integer).
 *
 * \param ptr [IN/OUT]          pointer to an atomic32 integer
 * \param compare_value [IN]    value to compare to
 * \param swap_value [IN]       value to swap with if ptr value is equal to
 *                              compare value
 *
 * \return HG_UTIL_TRUE if swapped or HG_UTIL_FALSE
 */
static HG_UTIL_INLINE hg_util_bool_t
hg_atomic_cas32(hg_atomic_int32_t *ptr, hg_util_int32_t compare_value,
        hg_util_int32_t swap_value)
{
    hg_util_bool_t ret;
    
#if defined(_WIN32)
    ret = (compare_value == InterlockedCompareExchange(&ptr->value, swap_value,
            compare_value));
#elif defined(__APPLE__)
    ret = OSAtomicCompareAndSwap32(compare_value, swap_value, &ptr->value);
#elif defined(HG_UTIL_HAS_OPA_PRIMITIVES_H)
    ret = (hg_util_bool_t) (compare_value == OPA_cas_int(ptr, compare_value, swap_value));
#elif defined(HG_UTIL_HAS_STDATOMIC_H)
    ret = atomic_compare_exchange_strong(ptr, &compare_value, swap_value);
#else
  #error "Mercury atomics require OPA or <stdatomic.h> to be present."
#endif

    return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_ATOMIC_H */
