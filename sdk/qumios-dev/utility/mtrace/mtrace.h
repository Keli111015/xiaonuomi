#ifndef _MTRACE_H_
#define _MTRACE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"

#ifndef CONFIG_QM_MTRACE_SUPPORT
#define CONFIG_QM_MTRACE_SUPPORT   0
#endif

#ifndef CONFIG_MAX_FILE_NAME_LENGTH
#define CONFIG_MAX_FILE_NAME_LENGTH  128
#endif


/** Structure holding the details of a memory allocation/deallocation event. */
typedef struct mem_event {
    unsigned int      size;
    unsigned int      malloc_line;
    char              malloc_filename[CONFIG_MAX_FILE_NAME_LENGTH];
    unsigned int      free_line;
    char              free_filename[CONFIG_MAX_FILE_NAME_LENGTH];
    struct mem_event  *pnext;
} memory_event_t;

/** Structure holding the details of a memory node having allocation/deallocation events. */
typedef struct mem_node {
  void              *plocation;
  unsigned int      event_count;
  memory_event_t    *pfirst_event;
  struct mem_node   *pnext;
} memory_node_t;

void* mtrace_calloc(unsigned int num, unsigned int size, unsigned int line, char* filename);
void* mtrace_malloc(unsigned int size, unsigned int line, char* filename);
void* mtrace_realloc(void *ptr, unsigned int size, unsigned int line, const char* filename);
void mtrace_free(void *ptr, unsigned int line, const char* filename);
void mtrace_dump_memory_usage(void);

memory_node_t *allocate_memory(unsigned int size, void *plocation, unsigned int malloc_line, const char *malloc_file);
void deallocate_memory(void* plocation, unsigned int free_line, const char* free_filename);


#ifdef __cplusplus
}
#endif

#endif
