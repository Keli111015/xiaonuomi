#ifndef _IOT_EVENT_H_
#define _IOT_EVENT_H_

#if defined(__cplusplus)
extern "C" {
#endif


typedef enum {
    IOT_EVENT_CONNECT_SUCC,
    IOT_EVENT_CONNECT_FAIL,
    IOT_EVENT_DISCONNECTED,
    IOT_EVENT_RAWDATA_ARRIVED,
    IOT_EVENT_RAWDATA_REPLY,
    IOT_EVENT_PROPERTY_SET,
    IOT_EVENT_REPORT_REPLY,
    IOT_EVENT_PRODTST_SET,
    IOT_EVENT_PRODTST_REPORT_REPLY,
    IOT_EVENT_PRODTST_GET,
    IOT_EVENT_PRODTST_CONFIG,
    IOT_EVENT_PRODTST_TEST_REPLY,
    IOT_EVENT_PRODTST_RESULT,
    IOT_EVENT_PRODTST_RESULT_REPORT_REPLY,
    IOT_EVENT_PRODTST_SPCL_DOWN,
    IOT_EVENT_PRODTST_SPCL_UP_REPLY,
    IOT_EVENT_TOKEN_REPLY,
    IOT_EVENT_RESET_REPLY,
    IOT_EVENT_NOTIFY,

} iot_event_t;

#define iot_event_register(evt, cb)                 iot_event_register_for_##evt(cb);
#define DECLARE_IOT_EVENT_CALLBACK(evt, cb)         int iot_event_register_for_##evt(cb);
#define DEFINE_IOT_EVENT_CALLBACK(evt, cb)          int iot_event_register_for_##evt(cb) { \
        if (evt < 0 || evt >= sizeof(g_iot_event_map)/sizeof(iot_event_map_t)) {return -1;} \
        g_iot_event_map[evt].callback = (void *)callback;return 0;}



DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_CONNECT_SUCC,         int (*cb)(void))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_CONNECT_FAIL,         int (*cb)(void))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_DISCONNECTED,         int (*cb)(void))

DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_RAWDATA_ARRIVED,      int (*cb)(const int, const unsigned char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_RAWDATA_REPLY,        int (*cb)(const int, const int, const int, const unsigned char *, const int))

DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PROPERTY_SET,         int (*cb)(const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_REPORT_REPLY,         int (*cb)(const int, const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_NOTIFY,               int (*cb)(const int, const int, const char *, const int))


DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_SET,                   int (*cb)(const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_REPORT_REPLY,          int (*cb)(const int, const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_CONFIG,                int (*cb)(const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_TEST_REPLY,            int (*cb)(const int, const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_GET,                   int (*cb)(const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_RESULT,                int (*cb)(const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_RESULT_REPORT_REPLY,   int (*cb)(const int, const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_SPCL_DOWN,             int (*cb)(const int, const int, const char *, const int))
DECLARE_IOT_EVENT_CALLBACK(IOT_EVENT_PRODTST_SPCL_UP_REPLY,         int (*cb)(const int, const int, const char *, const int))


void *iot_event_callback(int evt);

int iot_event_regist_cb(void (*monitor_cb)(int event));

int iot_event_post(int event);



#if defined(__cplusplus)
}
#endif


#endif


