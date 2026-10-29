#ifndef __QM_IOT_OPERATION_H__
#define __QM_IOT_OPERATION_H__

#ifdef __cplusplus
extern "c" {
#endif

#include "id_map.h"
// #include <magiclink.h>

#define EVENT_NUM_MAX 5

/** 属性相关  **/
typedef struct {
    uint8_t            siid;
    uint8_t            prop_idx;
    uint8_t            type_id;
    uint8_t            attr_id;
    uint8_t            *value;
    uint8_t            value_len;
} pid_operation_t;


/** 属性相关  **/
typedef struct {
    uint8_t            siid;
    uint8_t            eid;
    uint8_t            type_id;
    uint8_t            attr_id;
    uint8_t            *value;
    uint8_t            value_len;
} event_operation_t;


/** 事件、方法相关  **/

// #define event_action 

typedef struct {
    char svcId[25];
    char eventId[25];

    unsigned int dataNum;
    // struct MagicLinkDataVal dataArray[5];
} aid_eid_operation_t;

#define SWAP_2BYTE(type, x)	((((type)x) >> 8) | \
						((((type)x) << 8)))
 
#define SWAP_4BYTE(type, x)	((((type)x) >> 24) | \
						((((type)x) & 0x00FF0000) >> 8) | \
						((((type)x) & 0x0000FF00) << 8) | \
						(((type)x) << 24))
 
#define SWAP_UINT64(x)	((((uint64_t)x) >> 56) | \
						((((uint64_t)x) & 0x00FF000000000000) >> 40) | \
						((((uint64_t)x) & 0x0000FF0000000000) >> 24) | \
						((((uint64_t)x) & 0x000000FF00000000) >> 8) | \
						((((uint64_t)x) & 0x00000000FF000000) << 8) | \
						((((uint64_t)x) & 0x0000000000FF0000) << 24) | \
						((((uint64_t)x) & 0x000000000000FF00) << 40) | \
						(((uint64_t)x) << 56))



map_honor_unit_t *map_honor_node(void);

map_honor_unit_t *map_node_assignment(char *pid, int type_id, int type, int attr_id, int propidx, uint8_t *eiid_list, uint8_t eiid_len) ;

map_honor_unit_t *iot_map_find(map_honor_unit_t *iot_map_find, char *src, uint8_t len);

int iot_map_insert(iot_map_honor_t *iot_map, map_honor_unit_t *new_node);

int foreach_map(map_honor_unit_t *id_map);

int destory_map_node(map_honor_unit_t *id_map);


void idmap_serial_copy(iot_map_honor_t *id_map_data, char *svcId, char **sid);

void idmap_prop_copy(map_honor_unit_t *map_honor_unit, char *propId);

void iot_map_service_insert(iot_map_honor_t *iot_map, char *src, uint8_t len);




#ifdef event_action 
struct MagicLinkDataVal *DataVal_new(void);
void DataVal_delete(struct MagicLinkDataVal *thiz);
#endif


#ifdef __cplusplus
}
#endif

#endif
