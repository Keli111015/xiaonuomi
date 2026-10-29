/**
 * Copyright (C) 2013-2015
 *
 * @file   on_action_invoke.c
 *
 * @remark
 *
 */

#include "arch_dbg.h"
#include "on_action_invoke.h"
#include "../typedef/operation_code.h"
#include "../iid/iid.h"
#include "S_2_Curtain_doAction.h"
#include "S_3_DataType_doAction.h"

void on_action_invoke(action_operation_t *o)
{
    LOG_INFO("on_action_invoke\n");
    LOG_INFO("did: %s\n", o->did);
    LOG_INFO("siid: %d\n", o->siid);
    LOG_INFO("aiid: %d\n", o->aiid);

    switch (o->siid)
    {
        case IID_2_Curtain:
            S_2_Curtain_doAction(o);
            break;

        case IID_3_DataType:
            S_3_DataType_doAction(o);
            break;

        default:
            o->code = OPERATION_INVALID;
            break;
    }
}
