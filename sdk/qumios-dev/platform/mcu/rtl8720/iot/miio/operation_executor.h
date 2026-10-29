/**
 * Copyright (C) 2013-2015
 *
 * @file   operation_executor.h
 *
 * @remark
 *
 */

#ifndef __OPERATION_EXECUTOR_H__
#define __OPERATION_EXECUTOR_H__

#include "miio_define.h"
#include "typedef/property_operation.h"
#include "typedef/event_operation.h"

int execute_property_operation(const char *str, int length, property_operation_type type, uint32_t id, miio_fp_rpc_delegate_ack_t ack, void *ctx);
int execute_action_invocation(const char *str, int length, uint32_t id, miio_fp_rpc_delegate_ack_t ack, void *ctx);

// int send_property_changed(miio_handle_t handle, uint32_t siid, uint32_t piid, property_value_t *newValue);

int send_property_changed(uint32_t id, miio_handle_t handle, miio_fp_rpc_delegate_ack_t ack, property_operation_t *property_operation, uint16_t num);
int send_event_occurred(miio_handle_t handle, event_operation_t *e);


#endif /* __OPERATION_EXECUTOR_H__ */
