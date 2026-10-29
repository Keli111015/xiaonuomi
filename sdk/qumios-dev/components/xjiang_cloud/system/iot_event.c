#include "qm_iot_export.h"
#include "iot_event.h"
#include "qm_types.h"

typedef struct {
    int eventid;
    void *callback;
} iot_event_map_t;


static iot_event_map_t g_iot_event_map[] = {
    {IOT_EVENT_CONNECT_SUCC, NULL},
    {IOT_EVENT_CONNECT_FAIL, NULL},
    {IOT_EVENT_DISCONNECTED, NULL},
    {IOT_EVENT_RAWDATA_ARRIVED, NULL},
    {IOT_EVENT_RAWDATA_REPLY, NULL},
    {IOT_EVENT_PROPERTY_SET, NULL},
    {IOT_EVENT_REPORT_REPLY, NULL},
    {IOT_EVENT_TOKEN_REPLY, NULL},
    {IOT_EVENT_RESET_REPLY, NULL},
    {IOT_EVENT_NOTIFY, NULL},
    {IOT_EVENT_PRODTST_SET, NULL},
    {IOT_EVENT_PRODTST_REPORT_REPLY, NULL},
    {IOT_EVENT_PRODTST_GET, NULL},
    {IOT_EVENT_PRODTST_CONFIG, NULL},
    {IOT_EVENT_PRODTST_TEST_REPLY, NULL},
    {IOT_EVENT_PRODTST_RESULT, NULL},
    {IOT_EVENT_PRODTST_RESULT_REPORT_REPLY, NULL},
    {IOT_EVENT_PRODTST_SPCL_DOWN, NULL},
    {IOT_EVENT_PRODTST_SPCL_UP_REPLY, NULL}
};

void *iot_event_callback(int evt)
{
    if (evt < 0 || evt >= sizeof(g_iot_event_map) / sizeof(iot_event_map_t)) {
        return NULL;
    }
    return g_iot_event_map[evt].callback;
}



static void *g_event_monitor = NULL;

int iot_event_regist_cb(void (*monitor_cb)(int event))
{
    g_event_monitor = monitor_cb;
    return 0;
}


int iot_event_post(int event)
{
    if (g_event_monitor == NULL)
        return -1;
    ((void (*)(int))g_event_monitor)(event);
    return 0;
}


DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_CONNECT_SUCC,         int (*callback)(void))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_CONNECT_FAIL,         int (*callback)(void))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_DISCONNECTED,         int (*callback)(void))

DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_RAWDATA_ARRIVED,      int (*callback)(const int, const unsigned char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_RAWDATA_REPLY,        int (*callback)(const int, const int, const int, const unsigned char *, const int))

DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PROPERTY_SET,         int (*callback)(const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_REPORT_REPLY,         int (*callback)(const int, const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_NOTIFY,               int (*callback)(const int, const int, const char *, const int))

DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_SET,                    int (*callback)(const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_REPORT_REPLY,           int (*callback)(const int, const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_CONFIG,                 int (*callback)(const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_TEST_REPLY,             int (*callback)(const int, const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_GET,                    int (*callback)(const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_RESULT,                 int (*callback)(const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_RESULT_REPORT_REPLY,    int (*callback)(const int, const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_SPCL_DOWN,              int (*callback)(const int, const int, const char *, const int))
DEFINE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_SPCL_UP_REPLY,          int (*callback)(const int, const int, const char *, const int))

