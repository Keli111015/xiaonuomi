#ifndef __LOCAL_MESH_PORT_H_
#define __LOCAL_MESH_PORT_H_

#include "qm_eventbus_static.h"

typedef enum
{
    LOCALMESH_MODULE_ID = 0x80,
    MUTUAL_EV_START_PAIR = 1,
    MUTUAL_EV_STOP_PAIR,
    MUTUAL_EV_FINISH_PAIR,
    MUTUAL_EV_UNPAIR,
    MUTUAL_EV_RENAME,
    MUTUAL_EV_RECV_MSG,
    MUTUAL_EV_SEND_MSG,
    MUTUAL_EV_TIMER,
    MUTUAL_EV_SAVE_ID, 
    STORAGE_ID_NET_ID = 1,
}localmesh_enum_e;

typedef struct {
    qm_eventbus_event_t base_ev;
    uint8_t channel;
    uint8_t count;
}localmesh_pair_ev_t;

typedef struct {
    qm_eventbus_event_t base_ev;
    uint8_t channel;
    xjmesh_net_id_t net_id;
}localmesh_rename_ev_t;

typedef struct {
    qm_eventbus_event_t base_ev;
    uint8_t channel;
    xjmesh_message_t msg;   
}localmesh_mutual_msg_ev_t;

typedef struct {
    qm_eventbus_event_t base_ev;
    uint16_t storage_id;
}localmesh_storage_save_ev_t;

int localmesh_post_send_mutal_msg_event(int channel, void *msg, bool emergency);
int localmesh_post_start_pair_event(int channel, int count, bool emergency);
int localmesh_post_stop_pair_event(int channel, bool emergency);
int localmesh_post_finish_pair_event(int channel, bool emergency);
int localmesh_post_unpair_event(int channel, bool emergency);
int localmesh_post_rename_event(int channel, void *net_id, bool emergency);
int localmesh_post_recv_mutal_msg_event(int channel, void *msg, bool emergency, bool is_call);
int lcoalmesh_post_send_mutal_msg_event(int channel, void *msg, bool emergency);
int localmesh_post_storage_save_id_event(int id, bool emergency);
#endif
