/**
 * Copyright (C) 2013-2015
 *
 * @file   operation_executor.h
 *
 * @remark
 *
 */

#ifndef __XJ_OPERATION_EXECUTOR_H__
#define __XJ_OPERATION_EXECUTOR_H__

#include "qm_tlv.h"
#include "miio_define.h"

int xj_send_property_changed(miio_handle_t handle, qm_tlv_ctx_t *tlv_ctx);

#endif /* __OPERATION_EXECUTOR_H__ */
