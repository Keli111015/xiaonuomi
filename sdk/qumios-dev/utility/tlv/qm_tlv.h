#ifndef _QM_TLV_H_
#define _QM_TLV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"


#ifndef CONFIG_QM_DTTV_SUPPORT
#define CONFIG_QM_DTTV_SUPPORT             1
#endif

#ifndef CONFIG_QM_TLV_SUPPORT
#define CONFIG_QM_TLV_SUPPORT              0
#endif

#ifndef CONFIG_QM_TTLV_SUPPORT
#define CONFIG_QM_TTLV_SUPPORT             0
#endif


#ifndef CONFIG_QM_TTLV_ARG
#define CONFIG_QM_TTLV_ARG             1
#endif

#if (CONFIG_QM_DTTV_SUPPORT || CONFIG_QM_TLV_SUPPORT || CONFIG_QM_TTLV_SUPPORT) == 0
#error must specify TLV, DTTV or TTLV support
#endif

#if CONFIG_QM_TLV_SUPPORT
#ifndef CONFIG_QM_TLV_TAG_SIZE
#define CONFIG_QM_TLV_TAG_SIZE           1
#endif

#ifndef CONFIG_QM_TLV_LENGTH_SIZE
#define CONFIG_QM_TLV_LENGTH_SIZE        1
#endif

#ifndef CONFIG_QM_TLV_INT16_SUPPORT
#define CONFIG_QM_TLV_INT16_SUPPORT      1
#endif

#ifndef CONFIG_QM_TLV_INT32_SUPPORT
#define CONFIG_QM_TLV_INT32_SUPPORT      1
#endif

#ifndef CONFIG_QM_TLV_FLOAT32_SUPPORT
#define CONFIG_QM_TLV_FLOAT32_SUPPORT      1
#endif

#ifndef CONFIG_QM_TLV_FLOAT64_SUPPORT
#define CONFIG_QM_TLV_FLOAT64_SUPPORT      0
#endif

#ifndef CONFIG_QM_TLV_INT64_SUPPORT
#define CONFIG_QM_TLV_INT64_SUPPORT      0
#endif

#ifndef CONFIG_QM_TLV_BYTES_SUPPORT
#define CONFIG_QM_TLV_BYTES_SUPPORT      1
#endif
#endif

#if CONFIG_QM_DTTV_SUPPORT

#ifdef CONFIG_QM_DTTV_BYTES_SIZE
#define CONFIG_QM_TLV_BUF_SIZE CONFIG_QM_DTTV_BYTES_SIZE
#endif

#ifdef CONFIG_QM_DTTV_BYTES_SIZE
#define CONFIG_QM_TLV_BUF_SIZEFORCE_UPDATE_SUPPORT  1
#endif

#define REAL_TYPE_ID(type_id)     ( type_id&0x7F )
#define IS_FORCE_UPDATE(type_id)     ( type_id&0x80 )

#ifndef CONFIG_QM_DTTV_TAG_SIZE
#define CONFIG_QM_DTTV_TAG_SIZE            1
#endif

#ifndef CONFIG_QM_DTTV_LENGTH_SIZE
#define CONFIG_QM_DTTV_LENGTH_SIZE         2
#endif

#ifndef CONFIG_QM_DTTV_INT16_SUPPORT
#define CONFIG_QM_DTTV_INT16_SUPPORT       1
#endif

#ifndef CONFIG_QM_DTTV_INT32_SUPPORT
#define CONFIG_QM_DTTV_INT32_SUPPORT       1
#endif

#ifndef CONFIG_QM_DTTV_FLOAT32_SUPPORT
#define CONFIG_QM_DTTV_FLOAT32_SUPPORT     1
#endif

#ifndef CONFIG_QM_DTTV_INT64_SUPPORT
#define CONFIG_QM_DTTV_INT64_SUPPORT       0
#endif

#ifndef CONFIG_QM_DTLV_FLOAT64_SUPPORT
#define CONFIG_QM_DTLV_FLOAT64_SUPPORT      0
#endif

#ifndef CONFIG_QM_DTTV_BYTES_SUPPORT
#define CONFIG_QM_DTTV_BYTES_SUPPORT       1
#endif
#endif

#if CONFIG_QM_TTLV_SUPPORT

#ifndef CONFIG_QM_TTLV_TAG_SIZE
#define CONFIG_QM_TTLV_TAG_SIZE             1
#endif

#ifndef CONFIG_QM_TTLV_LENGTH_SIZE
#define CONFIG_QM_TTLV_LENGTH_SIZE          2
#endif

#ifndef CONFIG_QM_TTLV_INT16_SUPPORT
#define CONFIG_QM_TTLV_INT16_SUPPORT        1
#endif

#ifndef CONFIG_QM_TTLV_INT32_SUPPORT
#define CONFIG_QM_TTLV_INT32_SUPPORT        1
#endif

#ifndef CONFIG_QM_TTLV_FLOAT32_SUPPORT
#define CONFIG_QM_TTLV_FLOAT32_SUPPORT      1
#endif

#ifndef CONFIG_QM_TTLV_FLOAT64_SUPPORT
#define CONFIG_QM_TTLV_FLOAT64_SUPPORT      0
#endif

#ifndef CONFIG_QM_TLV_INT64_SUPPORT
#define CONFIG_QM_TTLV_INT64_SUPPORT        1
#endif

#ifndef CONFIG_QM_TTLV_BYTES_SUPPORT
#define CONFIG_QM_TTLV_BYTES_SUPPORT        1
#endif

#endif

#if CONFIG_QM_TLV_TAG_SIZE == 2 || CONFIG_QM_DTTV_TAG_SIZE == 2 || CONFIG_QM_TTLV_TAG_SIZE == 2
#define CONFIG_QM_TLV_TAG_BYTE  2
#else
#define CONFIG_QM_TLV_TAG_BYTE  1
#endif

#if CONFIG_QM_TLV_LENGTH_SIZE == 2 || CONFIG_QM_DTTV_LENGTH_SIZE == 2 || CONFIG_QM_TTLV_LENGTH_SIZE == 2
#define CONFIG_QM_TLV_LENGTH_BYTE  2
#else
#define CONFIG_QM_TLV_LENGTH_BYTE  1
#endif

#if CONFIG_QM_TLV_INT16_SUPPORT || CONFIG_QM_DTTV_INT16_SUPPORT || CONFIG_QM_TTLV_INT16_SUPPORT
#define CONFIG_QM_DATA_TYPE_INT16_SUPPORT  1
#else
#define CONFIG_QM_DATA_TYPE_INT16_SUPPORT  0
#endif

#if CONFIG_QM_TLV_INT32_SUPPORT || CONFIG_QM_DTTV_INT32_SUPPORT || CONFIG_QM_TTLV_INT32_SUPPORT
#define CONFIG_QM_DATA_TYPE_INT32_SUPPORT  1
#else
#define CONFIG_QM_DATA_TYPE_INT32_SUPPORT  0
#endif

#if CONFIG_QM_TLV_FLOAT32_SUPPORT || CONFIG_QM_DTTV_FLOAT32_SUPPORT || CONFIG_QM_TTLV_FLOAT32_SUPPORT
#define CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT  1
#else
#define CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT  0
#endif

#if CONFIG_QM_TLV_FLOAT64_SUPPORT || CONFIG_QM_DTTV_FLOAT64_SUPPORT || CONFIG_QM_TTLV_FLOAT64_SUPPORT
#define CONFIG_QM_DATA_TYPE_FLOAT64_SUPPORT  1
#else
#define CONFIG_QM_DATA_TYPE_FLOAT64_SUPPORT  0
#endif

#if CONFIG_QM_TLV_INT64_SUPPORT || CONFIG_QM_DTTV_INT64_SUPPORT || CONFIG_QM_TTLV_INT64_SUPPORT
#define CONFIG_QM_DATA_TYPE_INT64_SUPPORT  1
#else
#define CONFIG_QM_DATA_TYPE_INT64_SUPPORT  0
#endif

#if CONFIG_QM_TLV_BYTES_SUPPORT || CONFIG_QM_DTTV_BYTES_SUPPORT || CONFIG_QM_TTLV_BYTES_SUPPORT
#define CONFIG_QM_DATA_TYPE_BYTES_SUPPORT  1
#else
#define CONFIG_QM_DATA_TYPE_BYTES_SUPPORT  0
#endif


#if CONFIG_QM_TLV_INT64_SUPPORT || CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
#ifndef CONFIG_QM_TLV_BUF_SIZE
#define CONFIG_QM_TLV_BUF_SIZE  8
#endif
#else
#ifndef CONFIG_QM_TLV_BUF_SIZE
#define CONFIG_QM_TLV_BUF_SIZE  4
#endif
#endif


#ifndef tlv_malloc
#define tlv_malloc   qm_malloc
#endif

#ifndef tlv_free
#define tlv_free     qm_free
#endif

#if CONFIG_QM_DTTV_SUPPORT
typedef enum{
    QM_DTTV_TYPE_MIN = 0,
    QM_DTTV_TYPE_BOOL = QM_DTTV_TYPE_MIN,
    QM_DTTV_TYPE_INT8 = 1,
    QM_DTTV_TYPE_UINT8 = 2,
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
    QM_DTTV_TYPE_INT16 = 3,
    QM_DTTV_TYPE_UINT16 = 4,
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
    QM_DTTV_TYPE_INT32 = 5,
    QM_DTTV_TYPE_UINT32 = 6,
#endif
#if CONFIG_QM_DATA_TYPE_INT64_SUPPORT
    QM_DTTV_TYPE_INT64 = 7,
    QM_DTTV_TYPE_UINT64 = 8,
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
    QM_DTTV_TYPE_FLOAT32 = 9,
#endif

#if CONFIG_QM_DATA_TYPE_FLOAT64_SUPPORT
    QM_DTTV_TYPE_FLOAT64 = 10,
#endif
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
    QM_DTTV_TYPE_STRING = 11,
    QM_DTTV_TYPE_DATE = 12,
    QM_DTTV_TYPE_STRUCT = 13,
    QM_DTTV_TYPE_ARRAY = 14,
#endif
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
    QM_DTTV_TYPE_FLOAT_ONE_UINT16 = 15,
    QM_DTTV_TYPE_FLOAT_TWO_UINT16 = 16,
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
    QM_DTTV_TYPE_FLOAT_ONE_UINT32 = 17,
    QM_DTTV_TYPE_FLOAT_TWO_UINT32 = 18,
#endif
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
    QM_DTTV_TYPE_FLOAT_ONE_INT16 = 19,
    QM_DTTV_TYPE_FLOAT_TWO_INT16 = 20,
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
    QM_DTTV_TYPE_FLOAT_ONE_INT32 = 21,
    QM_DTTV_TYPE_FLOAT_TWO_INT32 = 22,
#endif
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
    QM_DTTV_TYPE_GROUP           = 23,
    QM_DTTV_TYPE_STRING_ARRAY    = 24,
#endif
    QM_DTTV_TYPE_MAX,
    QM_DTTV_TYPE_NONE = 0xFF,
}qm_tlv_data_type_t;
#endif

typedef enum{
   QM_TLV_SERIALIZE_TYPE_ALL = 0,
   QM_TLV_SERIALIZE_TYPE_UPDATE = 1,
   QM_TLV_SERIALIZE_TYPE_PARTIAL = 2,
   QM_TLV_SERIALIZE_TYPE_MAX,
}qm_tlv_serialize_type_t;

typedef enum{
   QM_TLV_DIVIDE_TYPE_GENERIC = 0,
   QM_TLV_DIVIDE_TYPE_UPDATE = 1,
   QM_TLV_DIVIDE_TYPE_MAX,
}qm_tlv_divide_type_t;

typedef enum{
   QM_BLOCK_TYPE_TLV = 0,
   QM_BLOCK_TYPE_DTTV = 1,
   QM_BLOCK_TYPE_TTLV = 2,
   QM_BLOCK_TYPE_MAX,
}qm_tlv_block_type_t;

typedef struct{
    uint8_t update        : 1;
    uint8_t serialization : 1;
    uint8_t divide        : 1;
}qm_tlv_block_flags_t;

typedef struct qm_tlv_block{

    qm_tlv_block_flags_t flag;
#if CONFIG_QM_DTTV_SUPPORT || CONFIG_QM_TTLV_SUPPORT
    uint8_t type;
#endif
#if CONFIG_QM_TLV_TAG_BYTE == 1
        uint8_t tag;
#elif CONFIG_QM_TLV_TAG_BYTE == 2
        uint16_t tag;
#else
    #error tlv tag type is not support.
#endif

#if CONFIG_QM_TLV_LENGTH_BYTE == 1
        uint8_t len;
#elif CONFIG_QM_TLV_LENGTH_BYTE == 2
#  if CONFIG_QM_TLV_TAG_BYTE == 1
        //对齐，确保在CONFIG_QM_TLV_TAG_BYTE为1 && CONFIG_QM_TLV_LENGTH_BYTE为2时, len的offset为偶数
        //否则可能会出现奇怪的问题
        uint8_t pad_tag;
#  endif
        uint16_t len;
#else
    #error tlv length type is not support.
#endif

    union{
        uint8_t bytes[CONFIG_QM_TLV_BUF_SIZE];
        uint8_t *pdata;
    }value;

#if CONFIG_QM_TTLV_ARG
    void *arg;  
#endif

    struct qm_tlv_block *next;

}qm_tlv_block_t;

typedef struct
{
    int count;
    void *arg;
    struct qm_tlv_block *tlv_block;

}qm_tlv_ctx_t;

qm_tlv_ctx_t *qm_tlv_ctx_creat(void);

qm_tlv_block_t *qm_tlv_block_creat(void);

int qm_tlv_ctx_delete(qm_tlv_ctx_t *tlv_ctx);

int qm_tlv_ctx_count_get(qm_tlv_ctx_t *tlv_ctx);

int qm_tlv_ctx_arg_set(qm_tlv_ctx_t *tlv_ctx, void *arg);

int qm_tlv_ctx_arg_get(qm_tlv_ctx_t *tlv_ctx, void **arg);

int qm_tlv_ctx_update_count_get(qm_tlv_ctx_t *tlv_ctx);

int qm_tlv_block_delete(qm_tlv_block_t *tlv_block);

int qm_tlv_block_tag_get(qm_tlv_block_t *tlv_block, uint16_t *tag);

int qm_tlv_block_tag_set(qm_tlv_block_t *tlv_block, uint16_t tag);

int qm_tlv_block_arg_set(qm_tlv_block_t *tlv_block, void *arg);

int qm_tlv_block_arg_get(qm_tlv_block_t *tlv_block, void **arg);

qm_tlv_block_t *qm_tlv_block_find(qm_tlv_ctx_t *tlv_ctx, uint16_t tag);
#if CONFIG_QM_DTTV_SUPPORT || CONFIG_QM_TTLV_SUPPORT
int qm_tlv_block_data_type_set(qm_tlv_block_t *tlv_block, qm_tlv_data_type_t type);
qm_tlv_data_type_t qm_tlv_block_data_type_get(qm_tlv_block_t *tlv_block);
#endif

int qm_tlv_block_serialize(qm_tlv_block_t *tlv_block);

int qm_tlv_block_is_update(qm_tlv_block_t *tlv_block, int *is_update);

int qm_tlv_block_is_data(qm_tlv_block_t *tlv_block, int *is_data);

int qm_tlv_block_update(qm_tlv_block_t *tlv_block, int update);

int qm_tlv_block_add(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block);

int qm_tlv_block_remove(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block);

int qm_tlv_block_copy(qm_tlv_block_t *dest_tlv_block, qm_tlv_block_t *src_tlv_block);

int qm_tlv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len, qm_tlv_block_type_t block_type);

int qm_tlv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_block_type_t block_type, qm_tlv_serialize_type_t serialize_type);

int qm_tlv_merge(qm_tlv_ctx_t *dest_tlv_ctx, qm_tlv_ctx_t *src_tlv_ctx);

int qm_tlv_block_divide(qm_tlv_block_t *tlv_block);

int qm_tlv_divide(qm_tlv_ctx_t *dest_tlv_ctx, qm_tlv_ctx_t *src_tlv_ctx, qm_tlv_divide_type_t divide_type);

qm_tlv_block_t *qm_tlv_block_next(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block);

int qm_tlv_pack_bool(qm_tlv_block_t *tlv_block, bool_t value);
int qm_tlv_pack_uint8(qm_tlv_block_t *tlv_block, uint8_t value);
int qm_tlv_pack_int8(qm_tlv_block_t *tlv_block, int8_t value);
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
int qm_tlv_pack_uint16(qm_tlv_block_t *tlv_block, uint16_t value);
int qm_tlv_pack_int16(qm_tlv_block_t *tlv_block, int16_t value);
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
int qm_tlv_pack_uint32(qm_tlv_block_t *tlv_block, uint32_t value);
int qm_tlv_pack_int32(qm_tlv_block_t *tlv_block, int32_t value);
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
int qm_tlv_pack_float32(qm_tlv_block_t *tlv_block, float32_t value);
#endif
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
int qm_tlv_pack_bytes(qm_tlv_block_t *tlv_block, uint8_t *bytes, int size);
int qm_tlv_pack_string(qm_tlv_block_t *tlv_block, char *str, int size);
#endif
int qm_tlv_unpack_bool(qm_tlv_block_t *tlv_block, bool_t *value);
int qm_tlv_unpack_uint8(qm_tlv_block_t *tlv_block, uint8_t *value);
int qm_tlv_unpack_int8(qm_tlv_block_t *tlv_block, int8_t *value);
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
int qm_tlv_unpack_uint16(qm_tlv_block_t *tlv_block, uint16_t *value);
int qm_tlv_unpack_int16(qm_tlv_block_t *tlv_block, int16_t *value);
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
int qm_tlv_unpack_uint32(qm_tlv_block_t *tlv_block, uint32_t *value);
int qm_tlv_unpack_int32(qm_tlv_block_t *tlv_block, int32_t *value);
#endif
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
int qm_tlv_unpack_float32(qm_tlv_block_t *tlv_block, float32_t *value);
#endif
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
int qm_tlv_unpack_bytes(qm_tlv_block_t *tlv_block, uint8_t *bytes, int *size);
int qm_tlv_unpack_string(qm_tlv_block_t *tlv_block, char *str, int *size);

int qm_tlv_unpack_bytes_direct(qm_tlv_block_t *tlv_block, uint8_t **bytes, int *size);
int qm_tlv_unpack_string_direct(qm_tlv_block_t *tlv_block, char **str, int *size);
#endif

#ifdef __cplusplus
}
#endif


#endif

