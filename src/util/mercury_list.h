/*
 * Copyright (C) 2013-2015 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#ifndef MERCURY_LIST_H
#define MERCURY_LIST_H

#include "mercury_util_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hg_list hg_list_t;

/**
 * Represents an entry in a doubly-linked list.
 */

typedef struct hg_list_entry hg_list_entry_t;

/**
 * A value stored in a list.
 */

typedef void *hg_list_value_t;

/**
 * A null \ref hg_list_value_t.
 */

#define HG_LIST_NULL ((void *) 0)

/**
 * Callback function used to determine of two values in a list are
 * equal.
 *
 * \param value1      The first value to compare.
 * \param value2      The second value to compare.
 * \return            A non-zero value if value1 and value2 are equal, zero
 *                    if they are not equal.
 */
typedef int (*hg_list_equal_func_t)(hg_list_value_t value1, hg_list_value_t value2);

/**
 * Create a new list.
 *
 * \return           A new list, or NULL if it was not possible to allocate
 *                   the memory.
 */
HG_UTIL_EXPORT hg_list_t *
hg_list_new(void);

/**
 * Free an entire list.
 *
 * \param list         The list to free.
 */
HG_UTIL_EXPORT void
hg_list_free(hg_list_t *list);

/**
 * Append a value to the end of a list.
 *
 * \param list         Pointer to the list to append to.
 * \param data         The value to append.
 * \return             The new entry in the list, or NULL if it was not
 *                     possible to allocate the memory for the new entry.
 */
HG_UTIL_EXPORT hg_list_entry_t *
hg_list_insert_head(hg_list_t *list, hg_list_value_t data);

/**
 * Query if any values are currently in a list.
 *
 * \param queue      The list.
 * \return           False if the list is not empty, true if the queue is empty.
 */
hg_util_bool_t
hg_list_is_empty(hg_list_t *list);

/** 
 * Find the length of a list.
 *
 * \param list       The list.
 * \return           The number of entries in the list.
 */
HG_UTIL_EXPORT hg_util_uint32_t
hg_list_length(hg_list_t *list);

/**
 * Retrieve the first entry in a list.
 *
 * \param list         Pointer to the list.
 * \return             The first entry in the list, or NULL if there was no
 *                     entry in the list.
 */
hg_list_entry_t *
hg_list_first(hg_list_t *list);

/** 
 * Retrieve the next entry in a list.
 *
 * \param entry        Pointer to the list entry.
 * \return             The next entry in the list, or NULL if this was the
 *                     last entry in the list.
 */
HG_UTIL_EXPORT hg_list_entry_t *
hg_list_next(hg_list_entry_t *entry);

/**
 * Retrieve the value at a list entry.
 *
 * \param entry        Pointer to the list entry.
 * \return             The value stored at the list entry.
 */
HG_UTIL_EXPORT hg_list_value_t
hg_list_data(hg_list_entry_t *entry);

/**
 * Remove an entry from a list.
 *
 * \param list       Pointer to the list.
 * \param entry      The list entry to remove .
 * \return           If the entry is not found in the list, returns zero,
 *                   else returns non-zero.
 */
HG_UTIL_EXPORT int
hg_list_remove_entry(hg_list_t *list, hg_list_entry_t *entry);

/**
 * Remove all occurrences of a particular value from a list.
 *
 * \param list       Pointer to the list.
 * \param callback   Function to invoke to compare values in the list
 *                   with the value to be removed.
 * \param data       The value to remove from the list.
 * \return           The number of entries removed from the list.
 */
HG_UTIL_EXPORT unsigned int
hg_list_remove_data(hg_list_t *list, hg_list_equal_func_t callback,
        hg_list_value_t data);

/**
 * Find the entry for a particular value in a list.
 *
 * \param list           The list to search.
 * \param callback       Function to invoke to compare values in the list
 *                       with the value to be searched for.
 * \param data           The value to search for.
 * \return               The list entry of the item being searched for, or
 *                       NULL if not found.
 */
HG_UTIL_EXPORT hg_list_entry_t *
hg_list_find_data(hg_list_t *list, hg_list_equal_func_t callback,
        hg_list_value_t data);

#ifdef __cplusplus
}
#endif

#endif /* MERCURY_LIST_H */

