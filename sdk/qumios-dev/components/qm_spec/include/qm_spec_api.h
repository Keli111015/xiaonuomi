/* Define to prevent recursive inclusion -------------------------------------*/

#ifndef __QM_SPEC_API_H__
#define __QM_SPEC_API_H__

/* Includes ------------------------------------------------------------------*/
#include "qm.h"
#include "qm_spec_log.h"

#define QM_SPEC_VERSION                     1

#define QM_SPEC_LENGTH_STRING               1024

#define QM_SPEC_MAX_ARGUMENTS               12

#define CONFIG_QM_SPEC_BUFF_SIZE            (8)

typedef enum {
    QM_SPEC_PROPERTY_FORMAT_BOOL = 0,
    QM_SPEC_PROPERTY_FORMAT_INT8,
    QM_SPEC_PROPERTY_FORMAT_UINT8,
    QM_SPEC_PROPERTY_FORMAT_INT16,
    QM_SPEC_PROPERTY_FORMAT_UINT16,
    QM_SPEC_PROPERTY_FORMAT_INT32,
    QM_SPEC_PROPERTY_FORMAT_UINT32,
    QM_SPEC_PROPERTY_FORMAT_INT64,
    QM_SPEC_PROPERTY_FORMAT_UINT64,
    QM_SPEC_PROPERTY_FORMAT_FLOAT32,
    QM_SPEC_PROPERTY_FORMAT_FLOAT64,
    QM_SPEC_PROPERTY_FORMAT_STRING,
    QM_SPEC_PROPERTY_FORMAT_DATE,
    QM_SPEC_PROPERTY_FORMAT_STRUCT,
    QM_SPEC_PROPERTY_FORMAT_ARRAY,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_UINT16,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_UINT16,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_UINT32,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_UINT32,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_INT16,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_INT16,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_ONE_INT32,
    QM_SPEC_PROPERTY_FORMAT_FLOAT_TWO_INT32,
    QM_SPEC_PROPERTY_FORMAT_GROUP ,
    QM_SPEC_PROPERTY_FORMAT_STRING_ARRAY,
    QM_SPEC_PROPERTY_FORMAT_NUMBER,
    QM_SPEC_PROPERTY_FORMAT_NONE = 0xFF,
}qm_spec_property_format_t;

 typedef struct 
{
    uint16_t            len;
    uint8_t            property_format;
    union 
    {
        uint8_t *pdata;
        uint8_t bytes[CONFIG_QM_SPEC_BUFF_SIZE];
    }value;
}qm_spec_property_value_t;

 typedef struct {
    uint16_t piid;
    qm_spec_property_value_t value;
}argument_t;

 typedef struct {
    uint16_t      arguments_num;
    argument_t    arguments[QM_SPEC_MAX_ARGUMENTS];
} arguments_t;

 typedef struct _spec_property_t
{
#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT
    uint32_t                      did;
#endif
    uint8_t                     siid;
    uint16_t                    piid;
    int8_t                      code;
    qm_spec_property_value_t    value;
    struct _spec_property_t     *next;
}qm_spec_property_t;

 typedef struct
{
    uint16_t              element_num;
    struct _spec_property_t *property;
}qm_spec_property_operation_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            aiid;
    int8_t              code;
    arguments_t         *in;
    arguments_t         *out;
}qm_spec_action_operation_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            eiid;
    arguments_t         *arguments;
}qm_spec_event_operation_t;

qm_spec_property_operation_t *qm_spec_property_operation_creat(void);
int qm_spec_property_operation_delete(qm_spec_property_operation_t *property_operation);
int qm_spec_property_operation_merge(qm_spec_property_operation_t *dst_operation, qm_spec_property_operation_t *src_operation);

qm_spec_property_t *qm_spec_property_creat(void);
int qm_spec_property_delete(qm_spec_property_t *property_element);

int qm_spec_property_add(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property);
int qm_spec_property_remove(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property);

qm_spec_property_t *qm_spec_property_find(qm_spec_property_operation_t *property_operation, uint8_t siid, uint16_t piid);
qm_spec_property_t *qm_spec_property_next(qm_spec_property_operation_t *property_operation, qm_spec_property_t *property);

int qm_spec_property_copy(qm_spec_property_t *dst_property, const qm_spec_property_t *src_property);

int qm_spec_property_pack_xiid(qm_spec_property_t *property, uint8_t siid, uint16_t piid);
int qm_spec_property_unpack_xiid(qm_spec_property_t *property, uint8_t *siid, uint16_t *piid);

int qm_spec_property_data_format_len(qm_spec_property_format_t format);

int qm_spec_property_pack_number(qm_spec_property_t *property, int value);

int qm_spec_property_pack_bool(qm_spec_property_t *property, bool_t value);
int qm_spec_property_pack_uint8(qm_spec_property_t *property, uint8_t value);
int qm_spec_property_pack_int8(qm_spec_property_t *property, int8_t value);
int qm_spec_property_pack_uint16(qm_spec_property_t *property, uint16_t value);
int qm_spec_property_pack_int16(qm_spec_property_t *property, int16_t value);
int qm_spec_property_pack_uint32(qm_spec_property_t *property, uint32_t value);
int qm_spec_property_pack_int32(qm_spec_property_t *property, int32_t value);
int qm_spec_property_pack_float32(qm_spec_property_t *property, float value);
int qm_spec_property_pack_bytes(qm_spec_property_t *property, uint8_t *bytes, int size);
int qm_spec_property_pack_string(qm_spec_property_t *property, char *str, int size);

int qm_spec_property_unpack_number(qm_spec_property_t *property, int *value);

int qm_spec_property_unpack_bool(qm_spec_property_t *property, bool_t *value);
int qm_spec_property_unpack_uint8(qm_spec_property_t *property, uint8_t *value);
int qm_spec_property_unpack_int8(qm_spec_property_t *property, int8_t *value);
int qm_spec_property_unpack_uint16(qm_spec_property_t *property, uint16_t *value);
int qm_spec_property_unpack_int16(qm_spec_property_t *property, int16_t *value);
int qm_spec_property_unpack_uint32(qm_spec_property_t *property, uint32_t *value);
int qm_spec_property_unpack_int32(qm_spec_property_t *property, int32_t *value);
int qm_spec_property_unpack_float32(qm_spec_property_t *property, float *value);
int qm_spec_property_unpack_bytes(qm_spec_property_t *property, uint8_t *bytes, int *size);
int qm_spec_property_unpack_string(qm_spec_property_t *property, char *str, int *size);
int qm_spec_property_unpack_bytes_direct(qm_spec_property_t *property, uint8_t **bytes, int *size);
int qm_spec_property_unpack_string_direct(qm_spec_property_t *property, char **str, int *size);

#endif
