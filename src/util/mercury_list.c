/*
 * Copyright (C) 2013-2015 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include "mercury_list.h"

#include <stdlib.h>
#ifdef HG_UTIL_HAS_SYS_QUEUE_H
#include <sys/queue.h>
#else
/* TODO add definitions */
#endif

/* A doubly-linked list */
LIST_HEAD(hg_list_head, hg_list_entry);

struct hg_list_entry {
    hg_list_value_t data;
    LIST_ENTRY(hg_list_entry) entry;
};

struct hg_list {
    struct hg_list_head head;
    hg_util_uint32_t nentry;
};

/*---------------------------------------------------------------------------*/
hg_list_t *
hg_list_new(void)
{
    hg_list_t *list;

    list = (hg_list_t *) malloc(sizeof(hg_list_t));
    if (!list)
        return NULL;

    LIST_INIT(&list->head);
    list->nentry = 0;

    return list;
}

/*---------------------------------------------------------------------------*/
void
hg_list_free(hg_list_t *list)
{
	hg_list_entry_t *current;

	if (!list)
	    return;

    /* Iterate over each entry, freeing each list entry, until the
     * end is reached */
	for (current = LIST_FIRST(&list->head); current; ) {
        hg_list_entry_t *next = LIST_NEXT(current, entry);
        free(current);
        current = next;
	}
	free(list);

	return;
}

/*---------------------------------------------------------------------------*/
hg_list_entry_t *
hg_list_insert_head(hg_list_t *list, hg_list_value_t data)
{
	hg_list_entry_t *new_entry;

	if (!list)
	    return NULL;

	/* Create new list entry */
	new_entry = (hg_list_entry_t *) malloc(sizeof(hg_list_entry_t));
	if (!new_entry)
		return NULL;

	new_entry->data = data;
	LIST_INSERT_HEAD(&list->head, new_entry, entry);
	list->nentry++;

	return new_entry;
}

/*---------------------------------------------------------------------------*/
//hg_list_entry_t *
//hg_list_insert_before(hg_list_t *list, hg_list_entry_t *entry, hg_list_value_t data)
//{
//
//}
//
///*---------------------------------------------------------------------------*/
//hg_list_entry_t *
//hg_list_insert_after(hg_list_t *list, hg_list_entry_t *entry, hg_list_value_t data)
//{
//
//}

/*---------------------------------------------------------------------------*/
hg_util_bool_t
hg_list_is_empty(hg_list_t *list)
{
    if (!list)
        return HG_UTIL_TRUE;

    return LIST_EMPTY(&list->head);
}

/*---------------------------------------------------------------------------*/
hg_util_uint32_t
hg_list_length(hg_list_t *list)
{
    if (!list)
        return 0;

    return list->nentry;
}

/*---------------------------------------------------------------------------*/
hg_list_entry_t *
hg_list_first(hg_list_t *list)
{
    if (!list)
        return NULL;

    return LIST_FIRST(&list->head);
}

/*---------------------------------------------------------------------------*/
hg_list_entry_t *
hg_list_next(hg_list_entry_t *entry)
{
    if (!entry)
        return NULL;

    return LIST_NEXT(entry, entry);
}

/*---------------------------------------------------------------------------*/
hg_list_value_t
hg_list_data(hg_list_entry_t *entry)
{
    if (!entry)
        return NULL;

    return entry->data;
}

/*---------------------------------------------------------------------------*/
int
hg_list_remove_entry(hg_list_t *list, hg_list_entry_t *entry)
{
	if (!list || !entry)
		return HG_UTIL_FAIL;

	LIST_REMOVE(entry, entry);
    list->nentry--;
	free(entry);

	return HG_UTIL_SUCCESS;
}

/*---------------------------------------------------------------------------*/
unsigned int
hg_list_remove_data(hg_list_t *list, hg_list_equal_func_t callback,
        hg_list_value_t data)
{
	unsigned int entries_removed;
	hg_list_entry_t *entry;

	entries_removed = 0;

	/* Iterate over the entries in the list */
    for (entry = LIST_FIRST(&list->head); entry; ) {
        hg_list_entry_t *next = LIST_NEXT(entry, entry);
        if (callback(entry->data, data)) {
            hg_list_remove_entry(list, entry);
            entries_removed++;
        }
        entry = next;
    }

	return entries_removed;
}

/*---------------------------------------------------------------------------*/
hg_list_entry_t *
hg_list_find_data(hg_list_t *list, hg_list_equal_func_t callback,
        hg_list_value_t data)
{
    hg_list_entry_t *entry;

	/* Iterate over entries in the list until the data is found */
    LIST_FOREACH(entry, &list->head, entry)
        if (callback(entry->data, data))
            return entry;
	
	/* Not found */
	return NULL;
}
