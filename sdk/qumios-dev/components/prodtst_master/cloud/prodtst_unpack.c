#include "prodtst_cloud.h"
#include "qm_errno.h"
#include "cJSON.h"
#include "qm_log.h"
#include "prodtst_core.h"


typedef struct{
    int (*handle)(int id, char *data, int len);
}prodtst_unpack_t;

static int config_unpack_and_handle(int id, char *data, int len);
static int set_unpack_and_handle(int id, char *data, int len);
static int get_unpack_and_handle(int id, char *data, int len);
static int spcl_down_unpack_and_handle(int id, char *data, int len);
static int result_unpack_and_handle(int id, char *data, int len);

static prodtst_unpack_t prodtst_unpack[] = {

    {config_unpack_and_handle},
    {set_unpack_and_handle},
    {get_unpack_and_handle},
    {spcl_down_unpack_and_handle},
    {result_unpack_and_handle},

};


//{"module_type":"wifi","platform":"harmony","module_name":"W70","dev_type":"serial"}

static int config_unpack_and_handle(int id, char *data, int len)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *object = NULL;
    char *platform = NULL;
    char *module_type = NULL;
    char *module_name = NULL;
    char *dev_type = NULL;
    int test_time = 0;

    root = cJSON_Parse(data);
    if(root == NULL){
        return -QM_EINVAL;
    }
    
    object = cJSON_GetObjectItem(root, "platform");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    platform = object->valuestring;

    object = cJSON_GetObjectItem(root, "module_name");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    module_name = object->valuestring;

    object = cJSON_GetObjectItem(root, "module_type");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    module_type = object->valuestring;

    object = cJSON_GetObjectItem(root, "dev_type");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    dev_type = object->valuestring;

    object = cJSON_GetObjectItem(root, "test_time");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    test_time = object->valueint;

    printf("test_time: %d, platform: %s, module_name:%s, module_type:%s, dev_type:%s", test_time, platform, module_name, module_type, dev_type);

    prodtst_config_notify(test_time, platform, module_name, module_type);

__exit:
    cJSON_Delete(root);
    return ret;
}

static int generic_unpack_and_handle(int event, int id, char *data, int len)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *object = NULL;
    char *type = NULL;
    char *mdata = NULL;
    char *uuid = NULL;

    root = cJSON_Parse(data);
    if(root == NULL){
        return -QM_EINVAL;
    }
    
    object = cJSON_GetObjectItem(root, "type");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    type = object->valuestring;

    object = cJSON_GetObjectItem(root, "uuid");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    uuid = object->valuestring;

    object = cJSON_GetObjectItem(root, "data");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    mdata = object->valuestring;

    prodtst_data_notify(event, type, uuid, id, (uint8_t*)mdata, strlen(mdata));

__exit:
    cJSON_Delete(root);
    return ret;
}

static int set_unpack_and_handle(int id, char *data, int len)
{
    return generic_unpack_and_handle(PRODTST_EVENT_SET, id, data, len);
}

static int get_unpack_and_handle(int id, char *data, int len)
{
    return generic_unpack_and_handle(PRODTST_EVENT_GET, id, data, len);
}

static int result_unpack_and_handle(int id, char *data, int len)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *object = NULL;
    int result = 0;
    char *uuid = NULL;
    char *step = NULL;

    root = cJSON_Parse(data);
    if(root == NULL){
        return -QM_EINVAL;
    }

    object = cJSON_GetObjectItem(root, "uuid");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    uuid = object->valuestring;

    object = cJSON_GetObjectItem(root, "step");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    step = object->valuestring;

    object = cJSON_GetObjectItem(root, "result");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    result = object->valueint;
    
    prodtst_result_notify(uuid, id, step, result);

__exit:
    cJSON_Delete(root);
    return ret;
}

static int spcl_down_unpack_and_handle(int id, char *data, int len)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *object = NULL;
    char *m_data = NULL;
    char *uuid = NULL;

    root = cJSON_Parse(data);
    if(root == NULL){
        return -QM_EINVAL;
    }

    object = cJSON_GetObjectItem(root, "uuid");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    uuid = object->valuestring;

    object = cJSON_GetObjectItem(root, "data");
    if(object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    m_data = object->valuestring;
    
    prodtst_special_down_notify(uuid, id, m_data, strlen(m_data));

__exit:
    cJSON_Delete(root);
    return ret;
}

int prodtst_generic_unpack_and_handle(int packet_type, int id, char *data, int len)
{
    qm_err_t ret = QM_EOK;

    if(data == NULL || len <= 0){
        return -QM_EINVAL;
    }

    ret = prodtst_unpack[packet_type].handle(id, data, len);

    return ret;
}

int prodtst_config_unpack_and_handle(char *data, int len)
{
    return prodtst_generic_unpack_and_handle(PRODTST_PACKET_TYPE_CONFIG, 0, data, len);
}

int prodtst_spcl_down_unpack_and_handle(int id, char *data, int len)
{
    return prodtst_generic_unpack_and_handle(PRODTST_PACKET_TYPE_SPCL_DOWN, id, data, len);
}

int prodtst_set_unpack_and_handle(int id, char *data, int len)
{
    return prodtst_generic_unpack_and_handle(PRODTST_PACKET_TYPE_SET, id, data, len);
}

int prodtst_get_unpack_and_handle(int id, char *data, int len)
{
    return prodtst_generic_unpack_and_handle(PRODTST_PACKET_TYPE_GET, id, data, len);
}

int prodtst_result_unpack_and_handle(int id, char *data, int len)
{
    return prodtst_generic_unpack_and_handle(PRODTST_PACKET_TYPE_RESULT, id, data, len);
}