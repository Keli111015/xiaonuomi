#include "prodtst_cloud.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "cloudapi.h"

int prodtst_generic_send(int event, int id, prodtst_data_table_t *prodtst_tbl)
{
    char *data = NULL;
    int len = 0;
    qm_err_t ret = QM_EOK;
    if(prodtst_tbl == NULL){
        return -QM_EINVAL;
    }
    ret = prodtst_generic_pack(prodtst_tbl, &data, &len);
    if(ret != QM_EOK){
        return ret;
    }

    switch(event){

        case  PRODTST_EVENT_GET_REPLY:

            ret = cloud_packet_reply_send(0, MSG_TYPE_PRODTST_GET_REPLY, id, 200, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }

        break;

        case  PRODTST_EVENT_SET_REPLY:
        
            ret = cloud_packet_reply_send(0, MSG_TYPE_PRODTST_SET_REPLY, id, 200, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }

        break;

        case PRODTST_EVENT_REPORT:
            ret = cloud_packet_send(0, MSG_TYPE_PRODTST_REPORT, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }
        break;

        case PRODTST_EVENT_TEST:
            ret = cloud_packet_send(0, MSG_TYPE_PRODTST_TEST, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }

        break;

        case PRODTST_EVENT_RESULT_REPLY:
            ret = cloud_packet_reply_send(0, MSG_TYPE_PRODTST_RESULT_REPLY, id, 200, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }

        break;

        case PRODTST_EVENT_SPCL_UP:

            ret = cloud_packet_reply_send(0, MSG_TYPE_PRODTST_SPCL_UP, id, 200, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }
            
        break;

        case  PRODTST_EVENT_SPCL_DOWN_REPLY:
        
            ret = cloud_packet_reply_send(0, MSG_TYPE_PRODTST_SPCL_DOWN_REPLY, id, 200, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }

        break;

        case PRODTST_EVENT_RESULT_REPORT:

            ret = cloud_packet_send(0, MSG_TYPE_PRODTST_RESULT_REPORT, (uint8_t*)data, len);
            if(ret < 0){
                ret = -QM_EIO;
            }
            
        break;

        default:
        break;
    }

    qm_free(data);
    data = NULL;

    return ret;
}


int prodtst_test_send(prodtst_data_table_t *prodtst_tbl)
{
    return prodtst_generic_send(PRODTST_EVENT_TEST, 0, prodtst_tbl);
}
