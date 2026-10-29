#include "qm.h"
#include "qm_iot_operation.h"
#include "qm_log.h"
#include "id_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>


map_honor_unit_t *map_honor_node(void)
{   
    map_honor_unit_t *map_head = (map_honor_unit_t *)qm_malloc(sizeof(map_honor_unit_t));
    if (map_head == NULL) {
        return NULL;    
    }
    memset(map_head, 0, sizeof(map_honor_unit_t));
    map_head->p.pid = NULL;

    map_head->next = NULL;

    return map_head;
}


map_honor_unit_t *map_node_assignment(char *pid, int type_id, int type, int attr_id, int propidx, uint8_t *eiid_list, uint8_t eiid_len) 
{
    if (pid == NULL && attr_id < 0) {
        return NULL;    
    }

    map_honor_unit_t *new_node = (map_honor_unit_t *)qm_malloc(sizeof(map_honor_unit_t));
    if (new_node == NULL) {
        return NULL;
    }
    memset(new_node, 0, sizeof(map_honor_unit_t));

    new_node->type_id = type_id;
    new_node->attr_id = attr_id;
    new_node->type    = type;
    new_node->propidx = propidx;
    new_node->pid_len = strlen(pid);

    memset(new_node->eiid_list, 0xFF, HONOR_EID_MAX_LEN);
    if(eiid_list != NULL){
        for(int i = 0; i < eiid_len; i++){
            new_node->eiid_list[i] = eiid_list[i];
            qm_printf("new_node->eiid_list[%d] = %d\r\n", i, new_node->eiid_list[i]);
        }
    }
    
        
    if (new_node->pid_len > HONOR_ID_MAX_LEN) {
        new_node->p.pid = (char *)qm_malloc(new_node->pid_len+1);
        if (new_node->p.pid == NULL) {
            return NULL;
        }
        memset(new_node->p.pid, 0, new_node->pid_len+1);
        strcpy(new_node->p.pid, pid);
    } else {
        strcpy(new_node->p.id, pid);
    }

    
    return new_node;
}

int iot_map_insert(iot_map_honor_t *iot_map, map_honor_unit_t *new_node) 
{   
    if (iot_map == NULL || new_node == NULL) {
        return -QM_ERROR;
    }
    
    map_honor_unit_t *prve = iot_map->map_honor_unit;
    map_honor_unit_t *current = iot_map->map_honor_unit->next;

    while (current != NULL) {
        prve = current;
        current = current->next;
    }

    new_node->next = current;
    prve->next = new_node;

    if (prve->next->pid_len > HONOR_ID_MAX_LEN) {
        // QM_LOGD("map", "[%s|%d]  %s\n", __func__, __LINE__, prve->next->p.pid);
    } else {
        // QM_LOGD("map", "[%s|%d]  %s\n", __func__, __LINE__, prve->next->p.id);
    }
    
    
    return QM_EOK;
}

int foreach_map(map_honor_unit_t *id_map) 
{
    if (id_map == NULL) {
        return QM_EOK;
    }

    map_honor_unit_t *current = id_map->next;

    while (current != NULL) {
        // QM_LOGD("map", "%d %d %d %d", current->pid_len, current->type_id, current->attr_id, current->type);
        if (current->pid_len > HONOR_ID_MAX_LEN) {
            // QM_LOGD("map", "[%s|%d] %s", __func__, __LINE__, current->p.pid);
        } else {
            // QM_LOGD("map", "[%s|%d] %s", __func__, __LINE__, current->p.id);
        }
		
		current = current->next;
	}

    return QM_EOK;
}

int destory_map_node(map_honor_unit_t *id_map) 
{
    if (id_map == NULL) {
        return -QM_ERROR;
    }

    map_honor_unit_t *current = id_map->next;
    while (current != NULL) {
        map_honor_unit_t *next = current->next;
        qm_free(current);
        current = next;
    }

    qm_free(id_map);
    id_map = NULL;

    return QM_EOK;
}


void iot_map_service_insert(iot_map_honor_t *iot_map, char *sid, uint8_t len)
{
    if (iot_map == NULL || sid == NULL) {
        return ;
    }

    iot_map_honor_t *map = iot_map;
    map->sid_len = len;

    if (len > HONOR_ID_MAX_LEN) {
        map->s.sid = qm_malloc(len+1);
        if (map->s.sid == NULL) {
            return;
        }
        memset(map->s.sid, 0, len+1);
        strcpy(map->s.sid, sid);
    } else {
        strcpy(map->s.id, sid);
    }
}

map_honor_unit_t *iot_map_find(map_honor_unit_t *iot_map_find, char *src, uint8_t len)
{
    assert(iot_map_find);
    map_honor_unit_t *map = iot_map_find;

    while(map) {
        if (len > 10) {
            if (strcmp(map->p.pid, src) == 0) {
                return map;
            }
        } else {
            if (strcmp(map->p.id, src) == 0) {
                return map;
            }
        }
        map = map->next;
    }
    return NULL;
}


void idmap_serial_copy(iot_map_honor_t *id_map_data, char *svcId, char **sid) {
    if (id_map_data == NULL || svcId == NULL) {
        
        return;
    }

    id_map_data->sid_len = strlen(svcId);

    if (id_map_data->sid_len > HONOR_ID_MAX_LEN) {
        *sid = (char *)qm_malloc(id_map_data->sid_len+1);
        if (*sid == NULL) {
            return;
        }
        memset(*sid, 0, id_map_data->sid_len+1);
        strcpy(*sid, svcId);

        id_map_data->s.sid = *sid;
    } else {
        strcpy(id_map_data->s.id, svcId);
    }
}

void idmap_prop_copy(map_honor_unit_t *map_honor_unit, char *propId) {
    if (map_honor_unit == NULL || propId == NULL) {
        return;
    }

    map_honor_unit->pid_len = strlen(propId);
    if (map_honor_unit->pid_len > HONOR_ID_MAX_LEN) {
        map_honor_unit->p.pid = (char*)qm_malloc(map_honor_unit->pid_len+1);
        if (map_honor_unit->p.pid == NULL) {
            return;
        }
        memset(map_honor_unit->p.pid, 0, map_honor_unit->pid_len+1);

        strcpy(map_honor_unit->p.pid, propId);
    } else {
        strcpy(map_honor_unit->p.id, propId);
    }
}

#ifdef event_action 
struct MagicLinkDataVal *DataVa_new(void)
{
    struct MagicLinkDataVal * thiz = NULL;

    do
    {
        thiz = (struct MagicLinkDataVal *) qm_malloc(sizeof(struct MagicLinkDataVal));
        if (thiz == NULL)
        {
            break;
        }

        memset(thiz, 0, sizeof(struct MagicLinkDataVal));
        thiz->size = 0;
    } while (false);

    return thiz;
}

void DataVa_delete(struct MagicLinkDataVal *thiz)
{
	if (thiz != NULL)
	{
		qm_free(thiz);
	}	

	return;
}
#endif
