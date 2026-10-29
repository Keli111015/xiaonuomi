#include "mtrace.h"
#include "qm_log.h"
#include "qm_types.h"

#define LOG_TAG "mtrace"

static memory_node_t *memory_tracker_head = NULL;  /**< Head of double-linked list of memory nodes. */
static unsigned int memory_node = 0;           /**< Counter for memory nodes created. */
/*------------------------------------------------------------------------*/
/** Locate the memory node for the specified memory location (returns NULL if none). */
static memory_node_t *find_memory_node(void* plocation)
{
    memory_node_t *pmemory_node = memory_tracker_head;
    while (NULL != pmemory_node) {
        if (plocation == pmemory_node->plocation) {
            break;
        }
        pmemory_node = pmemory_node->pnext;
    } 

    return pmemory_node;
}
/*------------------------------------------------------------------------*/
/** Create a new memory node for the specified memory location. */
static memory_node_t *create_memory_node(void* plocation)
{
    memory_node_t *ptemp_node = NULL;
    memory_node_t *pmemory_node = find_memory_node(plocation);

    /* a memory node for pLocation should not exist yet */
    if (NULL == pmemory_node) {
        pmemory_node = (memory_node_t*)malloc(sizeof(memory_node_t));
        if(NULL == pmemory_node){
            return NULL;
        }
        pmemory_node->plocation = plocation;
        pmemory_node->event_count = 0;
        pmemory_node->pfirst_event = NULL;
        pmemory_node->pnext = NULL;

        /* add new node to linked list */
        ptemp_node = memory_tracker_head;
        if (NULL == ptemp_node) {
            memory_tracker_head = pmemory_node;
        }
        else {
            while (NULL != ptemp_node->pnext) {
                ptemp_node = ptemp_node->pnext;
            }
            ptemp_node->pnext = pmemory_node;
        }
        ++memory_node;
    }

    return pmemory_node;
}
/*------------------------------------------------------------------------*/
/** Add a new memory event having the specified parameters. */
static memory_event_t *add_memory_event(memory_node_t *pmemory_node, unsigned int size, unsigned int malloc_line, const char* malloc_filename)
{
    memory_event_t *pmemory_event = NULL;
    memory_event_t *ptemp_event = NULL;

    if(pmemory_node == NULL){
        return NULL;
    }
    /* create and set up the new event */
    pmemory_event = (memory_event_t*)malloc(sizeof(memory_event_t));
    if(NULL == pmemory_event){
        return NULL;
    }

    memset(pmemory_event, 0, sizeof(memory_event_t));

    pmemory_event->size = size;
    pmemory_event->malloc_line = malloc_line;
    strncpy(pmemory_event->malloc_filename, malloc_filename, CONFIG_MAX_FILE_NAME_LENGTH-1);

    /* add the new event to the end of the linked list */
    ptemp_event = pmemory_node->pfirst_event;
    if (NULL == ptemp_event) {
        pmemory_node->pfirst_event = pmemory_event;
    }
    else {
        while (NULL != ptemp_event->pnext) {
            ptemp_event = ptemp_event->pnext;
        }
        ptemp_event->pnext = pmemory_event;
    }

    ++pmemory_node->event_count;

    return pmemory_event;
}
/*------------------------------------------------------------------------*/
/** Record memory allocation event. */
memory_node_t *allocate_memory(unsigned int size, void *plocation, unsigned int malloc_line, const char *malloc_file)
{
    memory_node_t *pmemory_node = NULL;

    /* attempt to locate an existing record for this pLocation */
    pmemory_node = find_memory_node(plocation);

    /* pLocation not found - create a new event record */
    if (NULL == pmemory_node) {
        pmemory_node = create_memory_node(plocation);
    }

    /* add the new event record */
    add_memory_event(pmemory_node, size, malloc_line, malloc_file);

    return pmemory_node;
}

/*------------------------------------------------------------------------*/
/** Record memory deallocation event. */
void deallocate_memory(void* plocation, unsigned int free_line, const char* free_filename)
{
    memory_node_t *pmemory_node = NULL;
    memory_event_t *ptemp_event = NULL;

    if(free_line == 0 || free_filename == NULL){
        return;
    }

    /* attempt to locate an existing record for this pLocation */
    pmemory_node = find_memory_node(plocation);

    /* if no entry, then an unallocated pointer was freed */
    if (NULL == pmemory_node) {
        pmemory_node = create_memory_node(plocation);
        ptemp_event = add_memory_event(pmemory_node, 0, 0, "");
    }
    else {
        /* there should always be at least 1 event for an existing memory node */
        if(NULL == pmemory_node->pfirst_event){
            return;
        }

        /* locate last memory event for this pLocation */
        ptemp_event = pmemory_node->pfirst_event;
        while (NULL != ptemp_event->pnext) {
            ptemp_event = ptemp_event->pnext;
        }

        /* if pointer has already been freed, create a new event for double deletion */
        if (0 != ptemp_event->free_line) {
            ptemp_event = add_memory_event(pmemory_node, ptemp_event->size, 0, "");
        }
    }

    ptemp_event->free_line = free_line;
    strncpy(ptemp_event->free_filename, free_filename, CONFIG_MAX_FILE_NAME_LENGTH-1);
    ptemp_event->free_filename[CONFIG_MAX_FILE_NAME_LENGTH-1] = (char)0;
}

/*------------------------------------------------------------------------*/
/** Custom calloc function with memory event recording. */
void* mtrace_calloc(unsigned int num, unsigned int size, unsigned int line, char* filename)
{
    void* pvoid = NULL;
    pvoid = calloc(num, size);
    if (NULL != pvoid) {
        allocate_memory(num * size, pvoid, line, filename);
    }
    return pvoid;
}

/*------------------------------------------------------------------------*/
/** Custom malloc function with memory event recording. */
void* mtrace_malloc(unsigned int size, unsigned int line, char* filename)
{
    void* pvoid = NULL;
    pvoid = malloc(size);
    if (NULL != pvoid) {
        allocate_memory(size, pvoid, line, filename);
    }
    return pvoid;
}

/*------------------------------------------------------------------------*/
/** Custom free function with memory event recording. */
void mtrace_free(void *ptr, unsigned int line, const char* filename)
{
    deallocate_memory(ptr, line, filename);
    free(ptr);
}

/*------------------------------------------------------------------------*/
/** Custom realloc function with memory event recording. */
void* mtrace_realloc(void *ptr, unsigned int size, unsigned int line, const char* filename)
{
  void* pvoid = NULL;

  deallocate_memory(ptr, line, filename);

  pvoid = realloc(ptr, size);

  if (NULL != pvoid) {
      allocate_memory(size, pvoid, line, filename);
  }
  return pvoid;
}

/*------------------------------------------------------------------------*/
/** Print a report of memory events to file. */
void mtrace_dump_memory_usage(void)
{
    unsigned int valid;
    unsigned int invalid;
    memory_node_t *ptemp_node = NULL;
    memory_event_t *ptemp_event = NULL;

    valid = 0;
    invalid = 0;
    ptemp_node = memory_tracker_head;
    while (NULL != ptemp_node) {
        QM_LOGI(LOG_TAG, "point: %p", ptemp_node->plocation);
        QM_LOGI(LOG_TAG, "event count: %u", ptemp_node->event_count);

        ptemp_event = ptemp_node->pfirst_event;
        while (NULL != ptemp_event) {
            QM_LOGI(LOG_TAG, "event size: %u", ptemp_event->size);
            QM_LOGI(LOG_TAG, "malloc filename: %s", ptemp_event->malloc_filename);
            QM_LOGI(LOG_TAG, "malloc line: %u", ptemp_event->malloc_line);
            QM_LOGI(LOG_TAG, "free filename: %s", ptemp_event->free_filename);
            QM_LOGI(LOG_TAG, "free line: %u\r\n", ptemp_event->free_line);

            if ((0 != ptemp_event->malloc_line) && (0 != ptemp_event->free_line)) {
                ++valid;
            }
            else {
                ++invalid;
            }
            ptemp_event = ptemp_event->pnext;
        }
        ptemp_node = ptemp_node->pnext;
    }

    QM_LOGI(LOG_TAG, "summary valid records: %u", valid);
    QM_LOGI(LOG_TAG, "summary invalid records: %u", invalid);
    QM_LOGI(LOG_TAG, "summary total records: %u", valid + invalid);
}