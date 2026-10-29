#ifndef LJYD_POMODORO_H
#define LJYD_POMODORO_H

/* 创建番茄钟。CONFIG_APP_POMODORO_AUTO_START 为 1 时上电就开始。 */
void pomodoro_init(void);

/* QM_IOT_EVENT_CLOUD_CONN 之后调用：单独注册 method=pomodoro，不走 method 表。 */
int pomodoro_on_connected(void *iot_handle);

#endif
