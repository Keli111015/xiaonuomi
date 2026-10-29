/** @file test_cjson.c @brief Match the cJSON allocator ABI to host size_t. */
#include "qm_kernel.h"
#include <stdlib.h>
#define qm_malloc malloc
#define qm_free free
#include "cJSON.c"
