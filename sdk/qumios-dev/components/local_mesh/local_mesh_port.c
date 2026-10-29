#include "qm.h"
#include "xiaojiang_mesh.h"
#include "local_mesh_port.h"

int localmesh_post_send_mutal_msg_event(int channel, void *msg, bool emergency)
{
    if (msg == NULL)
    {
        return -1;
    }

    localmesh_mutual_msg_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_SEND_MSG) : MUTUAL_EV_SEND_MSG;
    ev.channel = channel;
    memcpy(&ev.msg, msg, sizeof(xjmesh_message_t));

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_mutual_msg_ev_t));
}

int localmesh_post_start_pair_event(int channel, int count, bool emergency)
{
    localmesh_pair_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_START_PAIR) : MUTUAL_EV_START_PAIR;
    ev.channel = channel;
    ev.count = count;

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_pair_ev_t));
}

int localmesh_post_stop_pair_event(int channel, bool emergency)
{
    localmesh_pair_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_STOP_PAIR) : MUTUAL_EV_STOP_PAIR;
    ev.channel = channel;

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_pair_ev_t));
}

int localmesh_post_finish_pair_event(int channel, bool emergency)
{
    localmesh_pair_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_FINISH_PAIR) : MUTUAL_EV_FINISH_PAIR;
    ev.channel = channel;

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_pair_ev_t));
}

int localmesh_post_unpair_event(int channel, bool emergency)
{
    localmesh_pair_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_UNPAIR) : MUTUAL_EV_UNPAIR;
    ev.channel = channel;

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_pair_ev_t));
}

int localmesh_post_rename_event(int channel, void *net_id, bool emergency)
{
    if (net_id == NULL)
    {
        return -1;
    }

    localmesh_rename_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_RENAME) : MUTUAL_EV_RENAME;
    ev.channel = channel;
    memcpy(&ev.net_id, net_id, sizeof(xjmesh_net_id_t));

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_rename_ev_t));
}

int localmesh_post_recv_mutal_msg_event(int channel, void *msg, bool emergency, bool is_call)
{
    if (msg == NULL)
    {
        return -1;
    }

    localmesh_mutual_msg_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_RECV_MSG) : MUTUAL_EV_RECV_MSG;
    ev.channel = channel;
    memcpy(&ev.msg, msg, sizeof(xjmesh_message_t));

    if (is_call)
    {
        return qm_eventbus_call_event(0, &ev, sizeof(localmesh_mutual_msg_ev_t));
    }
    else
    {
        return qm_eventbus_post_event(0, &ev, sizeof(localmesh_mutual_msg_ev_t));
    }
}

int lcoalmesh_post_send_mutal_msg_event(int channel, void *msg, bool emergency)
{
    if (msg == NULL)
    {
        return -1;
    }

    localmesh_mutual_msg_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_SEND_MSG) : MUTUAL_EV_SEND_MSG;
    ev.channel = channel;
    memcpy(&ev.msg, msg, sizeof(xjmesh_message_t));

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_mutual_msg_ev_t));
}

int localmesh_post_storage_save_id_event(int id, bool emergency)
{
    localmesh_storage_save_ev_t ev;
    ev.base_ev.module_id = LOCALMESH_MODULE_ID;
    ev.base_ev.event_id = emergency ? EMERGENCY_EVENT(MUTUAL_EV_SAVE_ID) : MUTUAL_EV_SAVE_ID;
    ev.storage_id = id;

    return qm_eventbus_post_event(0, &ev, sizeof(localmesh_storage_save_ev_t));
}
