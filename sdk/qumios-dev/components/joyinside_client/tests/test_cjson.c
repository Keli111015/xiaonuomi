/* cJSON uses size_t allocators; QUMI uses unsigned int on the target MCU. */
#include "qm_kernel.h"
#include <stdlib.h>
#define qm_malloc malloc
#define qm_free free
#include "cJSON.c"
