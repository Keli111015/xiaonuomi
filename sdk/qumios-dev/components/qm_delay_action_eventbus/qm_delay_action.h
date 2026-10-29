#ifndef _LIGHT_DELAY_ACTION_H
#define _LIGHT_DELAY_ACTION_H

#include "stdint.h"
#include "qm_eventbus_static.h"

typedef struct {
	void (*callback)(void *arg);
	void *arg;
}qm_void_callback_t;

typedef struct {
	uint8_t type;
	uint8_t repeat;
	uint8_t busid;
	uint8_t id;
	uint32_t time_ms;
	uint32_t current_ms;
	union {
		qm_void_callback_t fn;
	} data;
}qm_eventbus_delayaction_t;


#define DELAYACTION_TYPE_FN 1
#define DELAYACTION_TYPE_EV 2
#define DELAYACTION_INVALID_ID 0

int qm_eventbus_delay_action_init(void); 
int qm_eventbus_delay_action_deinit(void);
int qm_eventbus_delay_fn_start(int busid, uint32_t delay_ms, uint8_t repeat, qm_void_callback_t *fn);
int qm_eventbus_delay_fn_stop(int busid, qm_void_callback_t *fn);
int qm_eventbus_delay_action_handler(int busid, qm_eventbus_event_t *ev);


#endif
