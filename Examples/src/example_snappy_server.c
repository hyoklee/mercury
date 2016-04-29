/*
 * Copyright (C) 2013-2016 Argonne National Laboratory, Department of Energy,
 *                    UChicago Argonne, LLC and The HDF Group.
 * All rights reserved.
 *
 * The full copyright notice, including terms governing use, modification,
 * and redistribution, is contained in the COPYING file that can be
 * found at the root of the source code distribution tree.
 */

#include "example_snappy.h"

#include <mercury.h>

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    const char *na_info_string = NULL;

    na_class_t *na_class;
    na_context_t * na_context;
    char self_addr_string[PATH_MAX];
    na_addr_t self_addr;
    FILE *na_config = NULL;

    hg_class_t *hg_class;
    hg_context_t *hg_context;
    unsigned major;
    unsigned minor;
    unsigned patch;
    hg_return_t hg_ret;
    na_size_t self_addr_string_size = PATH_MAX;

    HG_Version_get(&major, &minor, &patch);

    printf("Server running mercury version %u.%u-%u\n",
        major, minor, patch);

    /* Get info string */
    /* bmi+tcp://localhost:port */
    na_info_string = getenv(HG_PORT_NAME);
    if (!na_info_string) {
        fprintf(stderr, HG_PORT_NAME " environment variable must be set, e.g.:\nMERCURY_PORT_NAME=\"tcp://127.0.0.1:22222\"\n");
        exit(0);
    }

    /* Initialize NA */
    na_class = NA_Initialize(na_info_string, NA_TRUE);

    /* Get self addr to tell client about */
    NA_Addr_self(na_class, &self_addr);
    NA_Addr_to_string(na_class, self_addr_string, &self_addr_string_size, self_addr);
    NA_Addr_free(na_class, self_addr);
    printf("Server address is: %s\n", self_addr_string);

    /* Write addr to a file */
    na_config = fopen(TEMP_DIRECTORY CONFIG_FILE_NAME, "w+");
    if (!na_config) {
        fprintf(stderr, "Could not open config file from: %s\n",
                TEMP_DIRECTORY CONFIG_FILE_NAME);
        exit(0);
    }
    fprintf(na_config, "%s\n", self_addr_string);
    fclose(na_config);

    /* Create NA context */
    na_context = NA_Context_create(na_class);

    /* Initialize Mercury with the desired network abstraction class */
    hg_class = HG_Init_na(na_class, na_context);

    /* Create HG context */
    hg_context = HG_Context_create(hg_class);

    /* Register RPC */
    snappy_compress_register(hg_class);

    /* Poke progress engine and check for events */
    do {
        unsigned int actual_count = 0;
        do {
            hg_ret = HG_Trigger(hg_context, 0 /* timeout */,
                    1 /* max count */, &actual_count);
        } while ((hg_ret == HG_SUCCESS) && actual_count);

        /* Do not try to make progress anymore if we're done */
        if (snappy_compress_done_target_g) break;

        hg_ret = HG_Progress(hg_context, HG_MAX_IDLE_TIME);

    } while (hg_ret == HG_SUCCESS);

    /* Finalize */
    HG_Context_destroy(hg_context);
    HG_Finalize(hg_class);

    NA_Context_destroy(na_class, na_context);
    NA_Finalize(na_class);

    return EXIT_SUCCESS;
}

