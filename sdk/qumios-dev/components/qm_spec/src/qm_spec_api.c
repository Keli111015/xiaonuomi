#include "qm.h"
#include "qm_spec_api.h"
#include "qm_spec_core.h"

#define LOG_TAG "SPEC_API"

#define SPEC_REAL_TYPE_ID(type_id)     ( type_id&0x7F )

static volatile uint32_t g_qm_spec_log_flags = QM_SPEC_KCONFIG_LOG_FLAGS;

uint32_t qm_spec_get_log_flags(void)
{
    return g_qm_spec_log_flags;
}

int qm_spec_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_SPEC_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_spec_log_flags = log_flags;
    return QM_EOK;
}

qm_spec_property_operation_t *qm_spec_property_operation_creat(void)
{
    qm_spec_property_operation_t *operation = NULL;
    operation = (qm_spec_property_operation_t*)qm_malloc(sizeof(qm_spec_property_operation_t));
    if(operation == NULL){
        return NULL;
    }
    memset(operation, 0, sizeof(qm_spec_property_operation_t));
    return operation;
}

int qm_spec_property_operation_delete(qm_spec_property_operation_t *property_operation)
{
    qm_spec_property_t *property_element = NULL;
    qm_spec_property_t *tmp_property_element = NULL;

    if(property_operation == NULL){
        return -QM_EINVAL;
    }
    property_element = property_operation->property;

    while(property_element){

        if(property_element->value.len > CONFIG_QM_SPEC_BUFF_SIZE){
            qm_free(property_element->value.value.pdata);
            property_element->value.value.pdata = NULL;
        }

        tmp_property_element = property_element;
        property_element = property_element->next;
        qm_free(tmp_property_element);
        tmp_property_element = NULL;
    }

    qm_free(property_operation);

    return QM_EOK;   
}

int qm_spec_property_operation_merge(qm_spec_property_operation_t *dst_operation, qm_spec_property_operation_t *src_operation)
{
    qm_err_t ret = QM_EOK;
    qm_spec_property_t *property = NULL;
    qm_spec_property_t *m_property = NULL;
    if(dst_operation == NULL || src_operation == NULL){
        return -QM_EINVAL;
    }

    property = src_operation->property;
    while(property){

        m_property = qm_spec_property_find(dst_operation, property->siid, property->piid);
        if(m_property){
            ret = qm_spec_property_copy(m_property, property);
            if(ret != QM_EOK){
                return ret;
            }
        }else{
            m_property = qm_spec_property_creat();
            if(m_property == NULL){
                return -QM_ENOMEM;
            }

            ret = qm_spec_property_copy(m_property, property);
            if(ret != QM_EOK){
                return ret;
            }

            ret = qm_spec_property_add(dst_operation, m_property);
            if(ret != QM_EOK){
                return ret;
            }
        }
        property = property->next;
    }

    return QM_EOK;
}

qm_spec_property_t *qm_spec_property_creat(void)
{
    qm_spec_property_t *property_element = NULL;
    property_element = (qm_spec_property_t*)qm_malloc(sizeof(qm_spec_property_t));
    if(property_element == NULL){
        return NULL;
    }
    memset(property_element, 0, sizeof(qm_spec_property_t));
    return property_element;
}

int qm_spec_property_delete(qm_spec_property_t *property_element)
{
    if(property_element == NULL){
        return -QM_EINVAL;
    }

    if(property_element->value.len > CONFIG_QM_SPEC_BUFF_SIZE){
        qm_free(property_element->value.value.pdata);
        property_element->value.value.pdata = NULL;
    }

    qm_free(property_element);
    return QM_EOK;
}

static int qm_spec_property_check(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property_element)
{
    qm_spec_property_t *m_property_element = NULL;

    while(1){
        m_property_element = qm_spec_property_next(property_operation, m_property_element);
        if(m_property_element == NULL){
            break;
        }

        if( m_property_element->siid == property_element->siid && 
            m_property_element->piid == property_element->piid){
            return 1;
        }
    }
    return 0;
}

int qm_spec_property_add(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property_element)
{
    qm_spec_property_t *m_property_element = NULL;
    qm_spec_property_t *property_element_pre = NULL;

    if(property_operation == NULL || property_element == NULL){
        return -QM_EINVAL;
    }

    if(property_operation->property == NULL){
        property_operation->property = property_element; 
        property_element->next = NULL;
        property_operation->element_num++;
        return QM_EOK;
    }

    if(qm_spec_property_check(property_operation, property_element)){
        return -QM_EINVAL;
    }

    m_property_element = property_operation->property;
    while(m_property_element){
        property_element_pre = m_property_element;
        m_property_element = m_property_element->next;  
    }

    property_element_pre->next = property_element;
    property_element->next = NULL;
    property_operation->element_num++;

    return QM_EOK;
}

int qm_spec_property_remove(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property_element)
{
    qm_spec_property_t *m_property_element = NULL;
    qm_spec_property_t *property_element_pre = NULL;


    if(property_operation == NULL || property_element == NULL){
        return -QM_EINVAL;
    }

    if(property_element == property_operation->property){
        m_property_element = property_operation->property;
        property_operation->property = property_operation->property->next;
        qm_spec_property_delete(m_property_element);
        property_operation->element_num--;
        return QM_EOK;
    }

    m_property_element = property_operation->property;
    while(m_property_element){
        if(property_element == m_property_element){
            property_element_pre->next = property_element->next;
            qm_spec_property_delete(m_property_element);
            property_operation->element_num--;
            break;
        }
        property_element_pre = m_property_element;
        m_property_element = m_property_element->next;
    }
    return QM_EOK;
}

int qm_spec_property_copy(qm_spec_property_t *dst_property, const qm_spec_property_t *src_property)
{
    uint8_t *dest_bytes = NULL;
    int dest_size = 0;

    uint8_t *src_bytes = NULL;
    int src_size = 0;

    if(dst_property == NULL || src_property == NULL){
        return -QM_EINVAL;
    }
    
#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT
    dst_property->did = src_property->did;
#endif
    dst_property->siid = src_property->siid;
    dst_property->piid = src_property->piid;
    dst_property->value.property_format = src_property->value.property_format;

    qm_spec_property_unpack_bytes_direct(dst_property, &dest_bytes, &dest_size);
    qm_spec_property_unpack_bytes_direct((qm_spec_property_t *)src_property, &src_bytes, &src_size);

    if(dest_size == src_size){
        if(0 != memcmp(dest_bytes, src_bytes, dest_size)){
           qm_spec_property_pack_bytes(dst_property, src_bytes, src_size);
        }
    }else{
        qm_spec_property_pack_bytes(dst_property, src_bytes, src_size);
    }
    return QM_EOK;
}

qm_spec_property_t *qm_spec_property_find(qm_spec_property_operation_t *property_operation, uint8_t siid, uint16_t piid)
{
    qm_spec_property_t *m_spec_property = NULL;
    if(property_operation == NULL){
        return NULL;
    }
    m_spec_property = property_operation->property;
    while(m_spec_property){
        if( m_spec_property->siid == siid && 
            m_spec_property->piid == piid){
            break;
        }
        m_spec_property = m_spec_property->next;
    }
    
    return m_spec_property;
}

qm_spec_property_t *qm_spec_property_next(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property_element)
{
    if(property_operation == NULL){
        return NULL;
    }

    if(property_element == NULL){
        return property_operation->property;
    }else{
        return property_element->next;
    }

    return NULL;
}

int qm_spec_property_pack_xiid(qm_spec_property_t *property_element, uint8_t siid, uint16_t piid)
{
    if(property_element == NULL){
        return -QM_EINVAL;
    }

    property_element->siid = siid;
    property_element->piid = piid;

    return QM_EOK;
}

int qm_spec_property_unpack_xiid(qm_spec_property_t *property_element, uint8_t *siid, uint16_t *piid)
{
    if(property_element == NULL){
        return -QM_EINVAL;
    }

    *siid = property_element->siid;
    *piid = property_element->piid;

    return QM_EOK;
}

int qm_spec_property_data_format_len(qm_spec_property_format_t format)
{

    format = (qm_spec_property_format_t)SPEC_REAL_TYPE_ID(format);

    if(format <= QM_SPEC_PROPERTY_FORMAT_UINT8){
        return sizeof(uint8_t);
    }

    if((format >= QM_SPEC_PROPERTY_FORMAT_INT16 && format <= QM_SPEC_PROPERTY_FORMAT_UINT16) || 
        (format >= QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_UINT16 && format <= QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_UINT16) ||
        (format >= QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_INT16 && format <= QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_INT16)){

        return sizeof(uint16_t);
    }


    if( format == QM_SPEC_PROPERTY_FORMAT_NUMBER ||
        (format >= QM_SPEC_PROPERTY_FORMAT_INT32 && format <= QM_SPEC_PROPERTY_FORMAT_UINT32) || 
        (format >= QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_UINT32 && format <= QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_UINT32) ||
        (format >= QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_INT32 && format <= QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_INT32)){

        return sizeof(uint32_t);
    }


    if((format >= QM_SPEC_PROPERTY_FORMAT_INT64 && format <= QM_SPEC_PROPERTY_FORMAT_UINT64)){

        return sizeof(uint64_t);
    }

    if(format == QM_SPEC_PROPERTY_FORMAT_FLOAT32 ){
        return sizeof(uint32_t);
    }

    if(format == QM_SPEC_PROPERTY_FORMAT_FLOAT64 ){
        return sizeof(uint64_t);
    }
    
    if((format >= QM_SPEC_PROPERTY_FORMAT_STRING && format <= QM_SPEC_PROPERTY_FORMAT_ARRAY) || 
        (format >= QM_SPEC_PROPERTY_FORMAT_GROUP && format <= QM_SPEC_PROPERTY_FORMAT_STRING_ARRAY)){
        return 0;
    }

    return -1;
}

int qm_spec_property_pack_number(qm_spec_property_t *property, int value)
{
    qm_spec_property_pack_int32(property, (int32_t)value);
    property->value.property_format = QM_SPEC_PROPERTY_FORMAT_NUMBER;

    return QM_EOK;
}

int qm_spec_property_pack_bool(qm_spec_property_t *property, bool_t value)
{
    qm_spec_property_pack_uint8(property, (uint8_t)value);
    property->value.property_format = QM_SPEC_PROPERTY_FORMAT_BOOL;

    return QM_EOK;
}

int qm_spec_property_pack_uint8(qm_spec_property_t *property, uint8_t value)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL){
        return -QM_EINVAL;
    }
    
    property_value = &property->value;
    property_value->property_format = QM_SPEC_PROPERTY_FORMAT_UINT8;
    
    if(property_value->len > CONFIG_QM_SPEC_BUFF_SIZE){
        if(property_value->value.pdata){
            qm_free(property_value->value.pdata);
            property_value->value.pdata = NULL;
        }
    }
    
    property_value->value.bytes[0] = (uint8_t)value;

    property_value->len = sizeof(uint8_t);

    return QM_EOK;
}

int qm_spec_property_pack_int8(qm_spec_property_t *property, int8_t value)
{
    qm_spec_property_pack_uint8(property, (uint8_t)value);
    property->value.property_format = QM_SPEC_PROPERTY_FORMAT_INT8;

    return QM_EOK;
}

int qm_spec_property_pack_uint16(qm_spec_property_t *property, uint16_t value)
{
    uint16_t tmp = 0;
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL){
        return -QM_EINVAL;
    }
    
    property_value = &property->value;
    property_value->property_format = QM_SPEC_PROPERTY_FORMAT_UINT16;

    if(property_value->len > CONFIG_QM_SPEC_BUFF_SIZE){
        if(property_value->value.pdata){
            qm_free(property_value->value.pdata);
            property_value->value.pdata = NULL;
        }
    }
    
    tmp = cpu_to_be16(value);
    memset(property_value->value.bytes, 0, CONFIG_QM_SPEC_BUFF_SIZE);
    memcpy(property_value->value.bytes, &tmp, sizeof(uint16_t));
    property_value->len = sizeof(uint16_t);

    return QM_EOK;
}

int qm_spec_property_pack_int16(qm_spec_property_t *property, int16_t value)
{
    qm_spec_property_pack_uint16(property, (uint16_t)value);
    property->value.property_format = QM_SPEC_PROPERTY_FORMAT_INT16;

    return QM_EOK;
}

int qm_spec_property_pack_uint32(qm_spec_property_t *property, uint32_t value)
{
    uint32_t tmp = 0;
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL){
        return -QM_EINVAL;
    }
    
    property_value = &property->value;
    property_value->property_format = QM_SPEC_PROPERTY_FORMAT_UINT32;
    if(property_value->len > CONFIG_QM_SPEC_BUFF_SIZE){
        if(property_value->value.pdata){
            qm_free(property_value->value.pdata);
            property_value->value.pdata = NULL;
        }
    }
    
    tmp = cpu_to_be32(value);
    memset(property_value->value.bytes, 0, CONFIG_QM_SPEC_BUFF_SIZE);
    memcpy(property_value->value.bytes, &tmp, sizeof(uint32_t));
    property_value->len = sizeof(uint32_t);

    return QM_EOK;
}

int qm_spec_property_pack_int32(qm_spec_property_t *property, int32_t value)
{
    qm_spec_property_pack_uint32(property, (uint32_t)value);
    property->value.property_format = QM_SPEC_PROPERTY_FORMAT_INT32;

    return QM_EOK;
}

int qm_spec_property_pack_float32(qm_spec_property_t *property, float value)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL){
        return -QM_EINVAL;
    }
    
    property_value = &property->value;
    property_value->property_format = QM_SPEC_PROPERTY_FORMAT_FLOAT32;
    if(property_value->len > CONFIG_QM_SPEC_BUFF_SIZE){
        if(property_value->value.pdata){
            qm_free(property_value->value.pdata);
            property_value->value.pdata = NULL;
        }
    }

    memset(property_value->value.bytes, 0, CONFIG_QM_SPEC_BUFF_SIZE);
    memcpy(property_value->value.bytes, &value, sizeof(float));
    property_value->len = sizeof(float);

    return QM_EOK;
}

int qm_spec_property_pack_bytes(qm_spec_property_t *property, uint8_t *bytes, int size)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || bytes == NULL || size == 0){
        return -QM_EINVAL;
    }

    property_value = &property->value;
    if(property_value->len > CONFIG_QM_SPEC_BUFF_SIZE){
        if(property_value->value.pdata){
            qm_free(property_value->value.pdata);
            property_value->value.pdata = NULL;
        }
    }

    if(size < CONFIG_QM_SPEC_BUFF_SIZE){
        memset(property_value->value.bytes, 0, CONFIG_QM_SPEC_BUFF_SIZE);
        memcpy(property_value->value.bytes, bytes, size);
    }else{
        property_value->value.pdata = (uint8_t*)qm_malloc(size + 1);
        if(property_value->value.pdata == NULL){
            return -QM_ENOMEM;   
        }
        memset(property_value->value.pdata, 0, size + 1);
        memcpy(property_value->value.pdata, bytes, size);
    }

    property_value->len = size;

    return QM_EOK;
}

int qm_spec_property_pack_string(qm_spec_property_t *property, char *str, int size)
{
    qm_spec_property_pack_bytes(property, (uint8_t*)str, size);
    property->value.property_format = QM_SPEC_PROPERTY_FORMAT_STRING;

    return QM_EOK;
}

int qm_spec_property_unpack_number(qm_spec_property_t *property, int *value)
{
    return qm_spec_property_unpack_int32(property, (int32_t *)value);
}

int qm_spec_property_value_unpack_bool(qm_spec_property_t *property, bool_t *value)
{
    return qm_spec_property_unpack_uint8(property, (uint8_t *)value);
}

int qm_spec_property_unpack_uint8(qm_spec_property_t *property, uint8_t *value)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || value == NULL){
        return -QM_EINVAL;
    }

    property_value = &property->value;
    *value = (uint8_t)property_value->value.bytes[0];
    return QM_EOK;
}

int qm_spec_property_unpack_bool(qm_spec_property_t *property, bool_t *value)
{
    return qm_spec_property_unpack_uint8(property, (uint8_t *)value);
}

int qm_spec_property_unpack_int8(qm_spec_property_t *property, int8_t *value)
{
    return qm_spec_property_unpack_uint8(property, (uint8_t *)value);
}

int qm_spec_property_unpack_uint16(qm_spec_property_t *property, uint16_t *value)
{
    uint16_t tmp = 0;
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || value == NULL){
        return -QM_EINVAL;
    }

    property_value = &property->value;
    memcpy(&tmp, property_value->value.bytes, sizeof(uint16_t));
    *value = be16_to_cpu(tmp);
    return QM_EOK;
}

int qm_spec_property_unpack_int16(qm_spec_property_t *property, int16_t *value)
{
    return qm_spec_property_unpack_uint16(property, (uint16_t*)value);
}

int qm_spec_property_unpack_uint32(qm_spec_property_t *property, uint32_t *value)
{
    uint32_t tmp = 0;
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || value == NULL){
        return -QM_EINVAL;
    }

    property_value = &property->value;
    memcpy(&tmp, property_value->value.bytes, sizeof(uint32_t));
    *value = be32_to_cpu(tmp);
    return QM_EOK;
}

int qm_spec_property_unpack_int32(qm_spec_property_t *property, int32_t *value)
{
    return qm_spec_property_unpack_uint32(property, (uint32_t*)value);
}

int qm_spec_property_unpack_float32(qm_spec_property_t *property, float *value)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || value == NULL){
        return -QM_EINVAL;
    }
    property_value = &property->value;
    memcpy(value, property_value->value.bytes, sizeof(float));
    return QM_EOK;
}

int qm_spec_property_unpack_bytes(qm_spec_property_t *property, uint8_t *bytes, int *size)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || size == NULL){
        return -QM_EINVAL;
    }

    property_value = &property->value;

    *size = (int)property_value->len;

    if(bytes == NULL){
        return QM_EOK;
    }

    if(*size < property_value->len){
        return -QM_EINVAL;
    }

    if(property_value->len <= CONFIG_QM_SPEC_BUFF_SIZE){
        memcpy(bytes, property_value->value.bytes, property_value->len);
    }else{
        memcpy(bytes, property_value->value.pdata, property_value->len);
    }

    return QM_EOK;
}

int qm_spec_property_unpack_string(qm_spec_property_t *property, char *str, int *size)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || size == NULL){
        return -QM_EINVAL;
    }

    property_value = &property->value;

    *size = (int)property_value->len+1;

    if(str == NULL){
        return QM_EOK;
    }

    if(*size < (property_value->len + 1)){
        return -QM_EINVAL;
    }

    if(property_value->len < CONFIG_QM_SPEC_BUFF_SIZE){
        memcpy(str, property_value->value.bytes, property_value->len);
    }else{
        memcpy(str, property_value->value.pdata, property_value->len);
    }
    
    *(str+property_value->len) = '\0';
    return QM_EOK;
}

int qm_spec_property_unpack_bytes_direct(qm_spec_property_t *property, uint8_t **bytes, int *size)
{
    qm_spec_property_value_t *property_value = NULL;
    if(property == NULL || bytes == NULL || size == NULL){
        return -QM_EINVAL;
    }

    property_value = &property->value;

    *size = property_value->len;

    if(property_value->len < CONFIG_QM_SPEC_BUFF_SIZE){
        *bytes = property_value->value.bytes;
    }else{
        *bytes = property_value->value.pdata;
    }

    return QM_EOK;
}

int qm_spec_property_unpack_string_direct(qm_spec_property_t *property, char **str, int *size)
{
    return qm_spec_property_unpack_bytes_direct(property, (uint8_t**)str, size);
}
