#ifndef LJYD_METHOD_H
#define LJYD_METHOD_H

#ifdef __cplusplus
extern "C" {
#endif

/* CLOUD_CONN 之后注册 device/down。同名 method 再注册是覆盖。 */
int ljyd_method_on_connected(void *iot_handle);

/*
 * device/up。params_json 为 NULL 或空串时用 {"source":"local"}。
 * msg_id 由 SDK 自增。
 */
int ljyd_method_up(const char *method, const char *submethod, const char *params_json);

#ifdef __cplusplus
}
#endif

#endif
