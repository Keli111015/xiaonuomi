/******************************************************************************
 * Main function used here.
 *
 * FileName: miio_main.c
 *
 * Description: entry file of user application
 *
 *
 * Time: 2016.10.
 *
*******************************************************************************/


/*********************************************************************
 * INCLUDES
 */
#include "miio_instance.h"
#include "miio_arch.h"
#include "jsmi.h"
#include "operation_executor.h"

#define TAG "miio_spec"

static int do_get_properties(miio_rpc_delegate_arg_t *req_arg, miio_fp_rpc_delegate_ack_t ack, void* ctx)
{
    LOG_ERROR_TAG(TAG, "--------------------- get_properties -------------------\r\n");

    do
	{
		int ret = 0;
        if (MIIO_DELEGATE_JSON != req_arg->type)
		{
			miio_rpc_delegate_ack_error(req_arg, MIIO_OT_ERR_CODE_PARAM_INVALID, MIIO_OT_ERR_INFO_PARAM_INVALID, ack, ctx);
			break;
		}

		ret = execute_property_operation((const char *)req_arg->pload, req_arg->pload_len, PROPERTY_OPERATION_GET, req_arg->id, ack, ctx);
		if (ret != 0)
		{
			miio_rpc_delegate_ack_error(req_arg, MIIO_OT_ERR_CODE_PARAM_INVALID, MIIO_OT_ERR_INFO_PARAM_INVALID, ack, ctx);
			break;
		}
	} while (0);

	return MIIO_OK;
}
MIIO_RPC_USER(get_properties, do_get_properties, "get_properties");

static int do_set_properties(miio_rpc_delegate_arg_t *req_arg, miio_fp_rpc_delegate_ack_t ack, void* ctx)
{
    LOG_ERROR_TAG(TAG, " ------------------- set_properties -------------------\n");

	do 
	{
		int ret = 0;
        if (MIIO_DELEGATE_JSON != req_arg->type)
		{
			miio_rpc_delegate_ack_error(req_arg, MIIO_OT_ERR_CODE_PARAM_INVALID, MIIO_OT_ERR_INFO_PARAM_INVALID, ack, ctx);
			break;
		}

		ret = execute_property_operation((const char *)req_arg->pload, req_arg->pload_len, PROPERTY_OPERATION_SET, req_arg->id, ack, ctx);
		if (ret != 0)
		{
			miio_rpc_delegate_ack_error(req_arg, MIIO_OT_ERR_CODE_PARAM_INVALID, MIIO_OT_ERR_INFO_PARAM_INVALID, ack, ctx);
			break;
		}
	} while (0);

	return MIIO_OK;
}
MIIO_RPC_USER(set_properties, do_set_properties, "set_properties");


static int do_invoke_action(miio_rpc_delegate_arg_t *req_arg, miio_fp_rpc_delegate_ack_t ack, void* ctx)
{
    LOG_ERROR_TAG(TAG, " ------------------- action -------------------\n");

    do
    {
        int ret = 0;
        if (MIIO_DELEGATE_JSON != req_arg->type)
        {
            miio_rpc_delegate_ack_error(req_arg, MIIO_OT_ERR_CODE_PARAM_INVALID, MIIO_OT_ERR_INFO_PARAM_INVALID, ack, ctx);
            break;
        }

        ret = execute_action_invocation((const char *)req_arg->pload, req_arg->pload_len, req_arg->id, ack, ctx);
        if (ret != 0)
        {
            miio_rpc_delegate_ack_error(req_arg, MIIO_OT_ERR_CODE_PARAM_INVALID, MIIO_OT_ERR_INFO_PARAM_INVALID, ack, ctx);
            break;
        }
    } while (0);

    return MIIO_OK;
}
MIIO_RPC_USER(action, do_invoke_action, "action");