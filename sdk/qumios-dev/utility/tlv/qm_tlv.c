#include "qm_tlv.h"
#include "qm_kernel.h"
#include "qm_errno.h"

#if CONFIG_QM_DTTV_SUPPORT
static int _qm_dttv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len);
static int _qm_dttv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_serialize_type_t serialize_type);
#endif

#if CONFIG_QM_TLV_SUPPORT
static int _qm_tlv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len);
static int _qm_tlv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_serialize_type_t serialize_type);
#endif

#if CONFIG_QM_TTLV_SUPPORT
static int _qm_ttlv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len);
static int _qm_ttlv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_serialize_type_t serialize_type);
#endif

qm_tlv_ctx_t *qm_tlv_ctx_creat(void)
{
    qm_tlv_ctx_t *tlv_ctx = NULL;
    tlv_ctx = (qm_tlv_ctx_t*)tlv_malloc(sizeof(qm_tlv_ctx_t));
    if(tlv_ctx == NULL){
        return NULL;
    }
    memset(tlv_ctx, 0, sizeof(qm_tlv_ctx_t));
    return tlv_ctx;
}

int qm_tlv_ctx_delete(qm_tlv_ctx_t *tlv_ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *tmp_block = NULL;

    if(tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    tlv_block = tlv_ctx->tlv_block;

    while(tlv_block){

        if(tlv_block->len > CONFIG_QM_TLV_BUF_SIZE){
            tlv_free(tlv_block->value.pdata);
            tlv_block->value.pdata = NULL;
        }

        tmp_block = tlv_block;
        tlv_block = tlv_block->next;
        tlv_free(tmp_block);
        tmp_block = NULL;
    }

    tlv_free(tlv_ctx);

    return QM_EOK;
}

int qm_tlv_block_delete(qm_tlv_block_t *tlv_block)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
    if(tlv_block->len > CONFIG_QM_TLV_BUF_SIZE){
        tlv_free(tlv_block->value.pdata);
    }

    tlv_free(tlv_block);
    return QM_EOK;
}

qm_tlv_block_t *qm_tlv_block_creat(void)
{
    qm_tlv_block_t *tlv_block = NULL;
    tlv_block = (qm_tlv_block_t*)tlv_malloc(sizeof(qm_tlv_block_t));
    if(tlv_block == NULL){
        return NULL;
    }
    memset(tlv_block, 0, sizeof(qm_tlv_block_t));
    return tlv_block;
}

int qm_tlv_ctx_count_get(qm_tlv_ctx_t *tlv_ctx)
{
    if(tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    return tlv_ctx->count;
}

int qm_tlv_ctx_arg_set(qm_tlv_ctx_t *tlv_ctx, void *arg)
{
    if(tlv_ctx == NULL){
        return -QM_EINVAL;
    }
    tlv_ctx->arg = arg;
    return QM_EOK;
}

int qm_tlv_ctx_arg_get(qm_tlv_ctx_t *tlv_ctx, void **arg)
{
    if(tlv_ctx == NULL || arg == NULL){
        return -QM_EINVAL;
    }
    *arg = tlv_ctx->arg;
    return QM_EOK;
}

int qm_tlv_ctx_update_count_get(qm_tlv_ctx_t *tlv_ctx)
{
    int count = 0;
    qm_tlv_block_t *m_tlv_block = NULL;
    m_tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(m_tlv_block){
        if(m_tlv_block->flag.update){
            count++;
        }
        m_tlv_block = qm_tlv_block_next(tlv_ctx, m_tlv_block);
    }
    return count;
}

static int qm_tlv_block_check(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block)
{
    qm_tlv_block_t *m_tlv_block = NULL;

    m_tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(m_tlv_block){
        if(tlv_block->tag == m_tlv_block->tag){
            return 1;
        }
        m_tlv_block = qm_tlv_block_next(tlv_ctx, m_tlv_block);
    }
    return 0;
}

int qm_tlv_block_add(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block)
{
    qm_tlv_block_t *m_tlv_block = NULL;
    qm_tlv_block_t *tlv_block_pre = NULL;

    if(tlv_ctx == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    if(tlv_ctx->tlv_block == NULL){
        tlv_ctx->tlv_block = tlv_block; 
        tlv_block->next = NULL;
        tlv_ctx->count++;
        tlv_block->flag.update = 1;
        return QM_EOK;
    }

    if(qm_tlv_block_check(tlv_ctx, tlv_block)){
        return -QM_EINVAL;
    }

    m_tlv_block = tlv_ctx->tlv_block;
    while(m_tlv_block){
        tlv_block_pre = m_tlv_block;
        m_tlv_block = m_tlv_block->next;  
    }

    tlv_block_pre->next = tlv_block;
    tlv_block->next = NULL;
    tlv_ctx->count++;
    tlv_block->flag.update = 1;

    return QM_EOK;
}

int qm_tlv_block_remove(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block)
{
    qm_tlv_block_t *m_tlv_block = NULL;
    qm_tlv_block_t *tlv_block_pre = NULL;

    if(tlv_ctx == NULL || tlv_block == NULL){
        return -QM_EINVAL;
    }

    if(tlv_block == tlv_ctx->tlv_block){
        m_tlv_block = tlv_ctx->tlv_block;
        tlv_ctx->tlv_block = tlv_ctx->tlv_block->next;
        qm_tlv_block_delete(m_tlv_block);
        tlv_ctx->count--;
        return QM_EOK;
    }

    m_tlv_block = tlv_ctx->tlv_block;
    while(m_tlv_block){
        if(tlv_block == m_tlv_block){
            tlv_block_pre->next = tlv_block->next;
            qm_tlv_block_delete(m_tlv_block);
            tlv_ctx->count--;
            break;
        }
        tlv_block_pre = m_tlv_block;
        m_tlv_block = m_tlv_block->next;
    }
    return QM_EOK;
}

int qm_tlv_block_tag_set(qm_tlv_block_t *tlv_block, uint16_t tag)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
#if CONFIG_QM_TLV_TAG_BYTE == 1
    tlv_block->tag = (uint8_t)tag;
#elif CONFIG_QM_TLV_TAG_BYTE == 2
   tlv_block->tag = (uint16_t)tag;
#endif 
    return QM_EOK;
}

int qm_tlv_block_tag_get(qm_tlv_block_t *tlv_block, uint16_t *tag)
{
    if(tlv_block == NULL || tag == NULL){
        return -QM_EINVAL;
    }
#if CONFIG_QM_TLV_TAG_BYTE == 1
   *tag = (uint8_t)tlv_block->tag;
#elif CONFIG_QM_TLV_TAG_BYTE == 2
   *tag = (uint16_t)tlv_block->tag;
#endif 
    return QM_EOK;
}

#if CONFIG_QM_TTLV_ARG
int qm_tlv_block_arg_set(qm_tlv_block_t *tlv_block, void *arg)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
    tlv_block->arg = arg;
    return QM_EOK;
}

int qm_tlv_block_arg_get(qm_tlv_block_t *tlv_block, void **arg)
{
    if(tlv_block == NULL || arg == NULL){
        return -QM_EINVAL;
    }
    *arg = tlv_block->arg;
    return QM_EOK;
}
#endif

qm_tlv_block_t *qm_tlv_block_find(qm_tlv_ctx_t *tlv_ctx, uint16_t tag)
{
    qm_tlv_block_t *m_tlv_block = NULL;
    if(tlv_ctx == NULL){
        return NULL;
    }
    m_tlv_block = tlv_ctx->tlv_block;
    while(m_tlv_block){
        #if CONFIG_QM_TLV_LENGTH_BYTE == 1
        if(m_tlv_block->tag == (uint8_t)tag){
            break;
        }
    #elif CONFIG_QM_TLV_LENGTH_BYTE == 2
        if(m_tlv_block->tag == (uint16_t)tag){
            break;
        }
    #endif
        m_tlv_block = m_tlv_block->next;
    }
    
    return m_tlv_block;
}

#if CONFIG_QM_DTTV_SUPPORT || CONFIG_QM_TTLV_SUPPORT

int qm_tlv_block_data_type_set(qm_tlv_block_t *tlv_block, qm_tlv_data_type_t type)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
    tlv_block->type = type;
    return QM_EOK;
}

qm_tlv_data_type_t qm_tlv_block_data_type_get(qm_tlv_block_t *tlv_block)
{
    if(tlv_block == NULL){
        return QM_DTTV_TYPE_NONE;
    }
    return (qm_tlv_data_type_t)tlv_block->type;
}

static int qm_tlv_block_data_type_len(qm_tlv_data_type_t type)
{
#if CONFIG_QM_DTTV_SUPPORT
#if CONFIG_QM_DTTV_FORCE_UPDATE_SUPPORT
    type = (qm_tlv_data_type_t)REAL_TYPE_ID(type);
#endif
#endif

    if(type <= QM_DTTV_TYPE_UINT8){
        return sizeof(uint8_t);
    }
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
    if((type >= QM_DTTV_TYPE_INT16 && type <= QM_DTTV_TYPE_UINT16) || 
        (type >= QM_DTTV_TYPE_FLOAT_ONE_UINT16 && type <= QM_DTTV_TYPE_FLOAT_TWO_UINT16) ||
        (type >= QM_DTTV_TYPE_FLOAT_ONE_INT16 && type <= QM_DTTV_TYPE_FLOAT_TWO_INT16)){

        return sizeof(uint16_t);
    }
#endif

#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
    if((type >= QM_DTTV_TYPE_INT32 && type <= QM_DTTV_TYPE_UINT32) || 
        (type >= QM_DTTV_TYPE_FLOAT_ONE_UINT32 && type <= QM_DTTV_TYPE_FLOAT_TWO_UINT32) ||
        (type >= QM_DTTV_TYPE_FLOAT_ONE_INT32 && type <= QM_DTTV_TYPE_FLOAT_TWO_INT32)){

        return sizeof(uint32_t);
    }
#endif

#if CONFIG_QM_DATA_TYPE_INT64_SUPPORT
    if((type >= QM_DTTV_TYPE_INT64 && type <= QM_DTTV_TYPE_UINT64)){

        return sizeof(uint64_t);
    }
#endif
    
#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
    if(type == QM_DTTV_TYPE_FLOAT32 ){
        return sizeof(uint32_t);
    }
#endif
    
#if CONFIG_QM_DATA_TYPE_FLOAT64_SUPPORT
    if(type == QM_DTTV_TYPE_FLOAT64 ){
        return sizeof(uint64_t);
    }
#endif
    
#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
    if((type >= QM_DTTV_TYPE_STRING && type <= QM_DTTV_TYPE_ARRAY) || 
        (type >= QM_DTTV_TYPE_GROUP && type <= QM_DTTV_TYPE_STRING_ARRAY)){
        return 0;
    }
#endif

    return -1;
}

#endif


static int qm_tlv_unpack_direct(qm_tlv_block_t *tlv_block, uint8_t **bytes, int *size)
{
    if(tlv_block == NULL || bytes == NULL || size == NULL){
        return -QM_EINVAL;
    }

    *size = (int)tlv_block->len;

    if(tlv_block->len <= CONFIG_QM_TLV_BUF_SIZE){
        *bytes = tlv_block->value.bytes;
    }else{
        *bytes = tlv_block->value.pdata;
    }

    return QM_EOK;
}

int qm_tlv_block_copy(qm_tlv_block_t *dest_tlv_block, qm_tlv_block_t *src_tlv_block)
{
    int ret = 0;
    uint8_t *dest_bytes = NULL;
    int dest_size = 0;

    uint8_t *src_bytes = NULL;
    int src_size = 0;

    if(dest_tlv_block == NULL || src_tlv_block == NULL){
        return -QM_EINVAL;
    }


    if(dest_tlv_block->tag != src_tlv_block->tag){
       dest_tlv_block->tag = src_tlv_block->tag;
       dest_tlv_block->flag.update = 1;
    }

#if CONFIG_QM_DTTV_SUPPORT 
    if(dest_tlv_block->type != src_tlv_block->type){
       dest_tlv_block->type = src_tlv_block->type;
       dest_tlv_block->flag.update = 1;
    }
#endif

#if CONFIG_QM_TTLV_ARG
    dest_tlv_block->arg = src_tlv_block->arg;
#endif

#if CONFIG_QM_DTTV_SUPPORT && CONFIG_QM_DTTV_FORCE_UPDATE_SUPPORT
    if(IS_FORCE_UPDATE(src_tlv_block->type)){
        dest_tlv_block->type = (src_tlv_block->type & 0x7F);
        dest_tlv_block->flag.update = 1;
    }
#endif

    qm_tlv_unpack_direct(dest_tlv_block, &dest_bytes, &dest_size);
    qm_tlv_unpack_direct(src_tlv_block, &src_bytes, &src_size);

    if(dest_size == src_size){
        if(0 != memcmp(dest_bytes, src_bytes, dest_size)){
            ret = qm_tlv_pack_bytes(dest_tlv_block, src_bytes, src_size);
            dest_tlv_block->flag.update = 1;
        }
    }else{
        ret = qm_tlv_pack_bytes(dest_tlv_block, src_bytes, src_size);
        dest_tlv_block->flag.update = 1;
    }
    return ret;
}

int qm_tlv_block_serialize(qm_tlv_block_t *tlv_block)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
    tlv_block->flag.serialization = 1;
    return QM_EOK;
}

int qm_tlv_block_is_update(qm_tlv_block_t *tlv_block, int *is_update)
{
    if(tlv_block == NULL || is_update == NULL){
        return -QM_EINVAL;
    }

    *is_update = (int)tlv_block->flag.update;

    return QM_EOK;
}

int qm_tlv_block_is_data(qm_tlv_block_t *tlv_block, int *is_data)
{
    if(tlv_block == NULL || is_data == NULL){
        return -QM_EINVAL;
    }

    if(tlv_block->len > 0){
        *is_data = 1;
    }else{
        *is_data = 0;
    }
    return QM_EOK;
}

int qm_tlv_block_update(qm_tlv_block_t *tlv_block, int update)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }

    tlv_block->flag.update = update;

    return QM_EOK;
}

int qm_tlv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len, qm_tlv_block_type_t block_type)
{
#if CONFIG_QM_DTTV_SUPPORT
    if(block_type == QM_BLOCK_TYPE_DTTV){
        return _qm_dttv_deserialize(tlv_ctx, msg, len);
    }
#endif

#if CONFIG_QM_TLV_SUPPORT
    if(block_type == QM_BLOCK_TYPE_TLV){
        return _qm_tlv_deserialize(tlv_ctx, msg, len);
    }
#endif

#if CONFIG_QM_TTLV_SUPPORT
    if(block_type == QM_BLOCK_TYPE_TTLV){
        return _qm_ttlv_deserialize(tlv_ctx, msg, len);
    }
#endif
    return -QM_EINVAL;
}

#if CONFIG_QM_DTTV_SUPPORT
static int _qm_dttv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len)
{
    int index = 0;
    int type_len = 0;
    uint8_t type = 0;
#if (CONFIG_QM_DTTV_TAG_SIZE == 2) || (CONFIG_QM_DTTV_LENGTH_SIZE == 2)
    uint16_t tmp = 0;
#endif
    qm_err_t ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;

    if(tlv_ctx == NULL || msg == NULL || len == 0){
        return -QM_EINVAL;
    }

    while(index < len){

        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            ret = -QM_ENOMEM;
            goto __exit;
        }

        type = *(msg + index);
        type_len = qm_tlv_block_data_type_len((qm_tlv_data_type_t)type);
        if(type_len < 0){
            ret = -QM_EINVAL;
            goto __exit;
        }
        tlv_block->type = type;
        index++;

    #if CONFIG_QM_DTTV_TAG_SIZE == 1
        if(len - index < 1){
            ret = -QM_EINVAL;
            goto __exit;
        }
        tlv_block->tag = *(msg + index);
        index++;
    #elif CONFIG_QM_DTTV_TAG_SIZE == 2
        if(len - index < 2){
            ret = -QM_EINVAL;
            goto __exit;
        }
        memcpy(&tmp, msg + index, sizeof(uint16_t));
        tlv_block->tag = be16_to_cpu(tmp);
        index += 2;
    #endif

        if(type_len == 0){
        #if CONFIG_QM_DTTV_LENGTH_SIZE == 1
            if(len - index < 1){
                ret = -QM_EINVAL;
                goto __exit;
            }
            tlv_block->len = *(msg + index);
            index++;
        #elif CONFIG_QM_DTTV_LENGTH_SIZE == 2
            if(len - index < 2){
                ret = -QM_EINVAL;
                goto __exit;
            }
            memcpy(&tmp, msg + index, sizeof(uint16_t));
            tlv_block->len = be16_to_cpu(tmp);
            index += 2;
        #endif
        }else{
            tlv_block->len = type_len;
        }

        if(len - index < tlv_block->len){
            ret = -QM_EINVAL;
            goto __exit;
        }

        ret = qm_tlv_pack_bytes(tlv_block, msg + index, tlv_block->len);
        if(ret != QM_EOK){
            goto __exit;
        }

        index += tlv_block->len;

        ret = qm_tlv_block_add(tlv_ctx, tlv_block);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

    return QM_EOK;

__exit:
    if(tlv_block){
        tlv_free(tlv_block);
        tlv_block = NULL;
    }
    return ret;
}

#endif

#if CONFIG_QM_TLV_SUPPORT
static int _qm_tlv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len)
{
    int index = 0;
#if (CONFIG_QM_TLV_TAG_SIZE == 2) || (CONFIG_QM_TLV_LENGTH_SIZE == 2)
    uint16_t tmp = 0;
#endif
    qm_err_t ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;

    if(tlv_ctx == NULL || msg == NULL || len == 0){
        return -QM_EINVAL;
    }

    while(index < len){

        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            ret = -QM_ENOMEM;
            goto __exit;
        }

    #if CONFIG_QM_TLV_TAG_SIZE == 1
        if(len - index < 1){
            ret = -QM_EINVAL;
            goto __exit;
        }
        tlv_block->tag = *(msg + index);
        index++;
    #elif CONFIG_QM_TLV_TAG_SIZE == 2
        if(len - index < 2){
            ret = -QM_EINVAL;
            goto __exit;
        }
        memcpy(&tmp, msg + index, sizeof(uint16_t));
        tlv_block->tag = be16_to_cpu(tmp);
        index += 2;
    #endif

    #if CONFIG_QM_TLV_LENGTH_SIZE == 1
        if(len - index < 1){
            ret = -QM_EINVAL;
            goto __exit;
        }
        tlv_block->len = *(msg + index);
        index++;
    #elif CONFIG_QM_TLV_LENGTH_SIZE == 2
        if(len - index < 2){
            ret = -QM_EINVAL;
            goto __exit;
        }
        memcpy(&tmp, msg + index, sizeof(uint16_t));
        tlv_block->len = be16_to_cpu(tmp);
        index += 2;
    #endif

        if(len - index < tlv_block->len){
            ret = -QM_EINVAL;
            goto __exit;
        }

        ret = qm_tlv_pack_bytes(tlv_block, msg + index, tlv_block->len);
        if(ret != QM_EOK){
            goto __exit;
        }
        index += tlv_block->len;

        ret = qm_tlv_block_add(tlv_ctx, tlv_block);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

    return QM_EOK;

__exit:
    if(tlv_block){
        tlv_free(tlv_block);
        tlv_block = NULL;
    }
    return ret;
}
#endif

int qm_tlv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_block_type_t block_type, qm_tlv_serialize_type_t serialize_type)
{
#if CONFIG_QM_DTTV_SUPPORT
    if(block_type == QM_BLOCK_TYPE_DTTV){
        return _qm_dttv_serialize(tlv_ctx, msg, len, serialize_type);
    }
#endif

#if CONFIG_QM_TLV_SUPPORT
    if(block_type == QM_BLOCK_TYPE_TLV){
        return _qm_tlv_serialize(tlv_ctx, msg, len, serialize_type);
    }
#endif

#if CONFIG_QM_TTLV_SUPPORT
    if(block_type == QM_BLOCK_TYPE_TTLV){
        return _qm_ttlv_serialize(tlv_ctx, msg, len, serialize_type);
    }
#endif
    return -QM_EINVAL;
}

#if CONFIG_QM_DTTV_SUPPORT

static int dttv_block_len(qm_tlv_block_t *tlv_block)
{
    int type_len = 0;
    int len = 0;
    len += 1;

#if (CONFIG_QM_DTTV_TAG_SIZE == 1)
    len += 1;
#elif(CONFIG_QM_DTTV_TAG_SIZE == 2)
    len += 2;
#endif
    type_len = qm_tlv_block_data_type_len((qm_tlv_data_type_t)tlv_block->type);
    if(type_len > 0){
        len += type_len;
    }else if(type_len == 0){

        #if (CONFIG_QM_DTTV_LENGTH_SIZE == 1)
        len += 1;
        #elif(CONFIG_QM_DTTV_LENGTH_SIZE == 2)
        len += 2;
        #endif

        len += tlv_block->len;
    }
    return len;
}


static int _qm_dttv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_serialize_type_t serialize_type)
{
    int index = 0;
    int offset = *len;
    int block_len = 0;
    int type_len = 0;
    #if (CONFIG_QM_DTTV_TAG_SIZE == 2) || (CONFIG_QM_DTTV_LENGTH_SIZE == 2)
    uint16_t tmp = 0;
    #endif  
    uint8_t *bytes = NULL;
    int size = 0;
    qm_tlv_block_t *tlv_block = NULL;
    if(tlv_ctx == NULL || len == NULL){
        return -QM_EINVAL;
    }

    tlv_block = tlv_ctx->tlv_block;

    while(tlv_block){

        if(serialize_type == QM_TLV_SERIALIZE_TYPE_PARTIAL){
            if(!tlv_block->flag.serialization){
                tlv_block = tlv_block->next;
                continue;
            }else{
                if(msg){
                    block_len = dttv_block_len(tlv_block);
                    if(offset - index < block_len){
                        break;
                    }
                    tlv_block->flag.serialization = 0;
                }
            }
        }else if(serialize_type == QM_TLV_SERIALIZE_TYPE_UPDATE){
            if(!tlv_block->flag.update){
                tlv_block = tlv_block->next;
                continue;
            }else{
                if(msg){
                    block_len = dttv_block_len(tlv_block);
                    if(offset - index < block_len){
                        break;
                    }
                    tlv_block->flag.update = 0;
                }
            }
        }else{
            if(msg){
                block_len = dttv_block_len(tlv_block);
                if(offset - index < block_len){
                    break;
                }
            }
        }

        if(msg){
            *(msg + index) = tlv_block->type;
        }
        index ++;

    #if CONFIG_QM_DTTV_TAG_SIZE == 1
        if(msg){
            *(msg + index) = tlv_block->tag;
        }
        index ++;
    #elif CONFIG_QM_DTTV_TAG_SIZE == 2
        if(msg){
            tmp = cpu_to_be16(tlv_block->tag);
            memcpy(msg+index, &tmp, 2);
        }
        index += 2;
    #endif

        type_len = qm_tlv_block_data_type_len((qm_tlv_data_type_t)tlv_block->type);
        if(type_len == 0){
        #if CONFIG_QM_DTTV_LENGTH_SIZE == 1
            if(msg){
                *(msg + index) = tlv_block->len;
            }
            index ++;
        #elif CONFIG_QM_DTTV_LENGTH_SIZE == 2
            if(msg){
                tmp = cpu_to_be16(tlv_block->len);
                memcpy(msg + index, &tmp, 2);
            }
            index += 2;
        #endif
        }

        if(msg){
            qm_tlv_unpack_bytes_direct(tlv_block, &bytes, &size);
            if(bytes){
                memcpy(msg + index, bytes, size);
            }
        }
        index += tlv_block->len;
        tlv_block = tlv_block->next;
    }

    *len = index;

    return QM_EOK; 
}

#endif

#if CONFIG_QM_TLV_SUPPORT
static int tlv_block_len(qm_tlv_block_t *tlv_block)
{
    int len = 0;
#if (CONFIG_QM_TLV_TAG_SIZE == 1)
    len += 1;
#elif(CONFIG_QM_TLV_TAG_SIZE == 2)
    len += 2;
 #endif

#if (CONFIG_QM_TLV_LENGTH_SIZE == 1)
    len += 1;
#elif(CONFIG_QM_TLV_LENGTH_SIZE == 2)
    len += 2;
#endif

    len += tlv_block->len;

    return len;
}

static int _qm_tlv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_serialize_type_t serialize_type)
{
    int index = 0;
    int offset = *len;
    int block_len = 0;
    #if (CONFIG_QM_TLV_TAG_SIZE == 2) || (CONFIG_QM_TLV_LENGTH_SIZE == 2)
    uint16_t tmp = 0;
    #endif  
    uint8_t *bytes = NULL;
    int size = 0;
    qm_tlv_block_t *tlv_block = NULL;
    if(tlv_ctx == NULL || len == NULL){
        return -QM_EINVAL;
    }

    tlv_block = tlv_ctx->tlv_block;

    while(tlv_block){

        if(serialize_type == QM_TLV_SERIALIZE_TYPE_PARTIAL){
            if(!tlv_block->flag.serialization){
                tlv_block = tlv_block->next;
                continue;
            }else{
                if(msg){
                    block_len = tlv_block_len(tlv_block);
                    if(offset - index < block_len){
                        break;
                    }
                    tlv_block->flag.serialization = 0;
                }
            }
        }else if(serialize_type == QM_TLV_SERIALIZE_TYPE_UPDATE){
            if(!tlv_block->flag.update){
                tlv_block = tlv_block->next;
                continue;
            }else{
                if(msg){
                    block_len = tlv_block_len(tlv_block);
                    if(offset - index < block_len){
                        break;
                    }
                    tlv_block->flag.update = 0;
                }
            }
        }else{
            if(msg){
                block_len = tlv_block_len(tlv_block);
                if(offset - index < block_len){
                    break;
                }
            }
        }

    #if CONFIG_QM_TLV_TAG_SIZE == 1
        if(msg){
            *(msg + index) = tlv_block->tag;
        }
        index ++;
    #elif CONFIG_QM_TLV_TAG_SIZE == 2
        if(msg){
            tmp = cpu_to_be16(tlv_block->tag);
            memcpy(msg+index, &tmp, 2);
        }
        index += 2;
    #endif

    #if CONFIG_QM_TLV_LENGTH_SIZE == 1
        if(msg){
            *(msg + index) = tlv_block->len;
        }
        index ++;
    #elif CONFIG_QM_TLV_LENGTH_SIZE == 2
        if(msg){
            tmp = cpu_to_be16(tlv_block->len);
            memcpy(msg + index, &tmp, 2);
        }
        index += 2;
    #endif
        if(msg){
            qm_tlv_unpack_bytes_direct(tlv_block, &bytes, &size);
            if(bytes){
                memcpy(msg + index, bytes, size);
            }
        }
        index += tlv_block->len;
        tlv_block = tlv_block->next;
    }

    *len = index;

    return QM_EOK;
}
#endif

#if CONFIG_QM_TTLV_SUPPORT

static int ttlv_block_len(qm_tlv_block_t *tlv_block)
{
    int type_len = 0;
    int len = 0;
#if (CONFIG_QM_TLV_TAG_BYTE == 1)
    len += 1;
#elif(CONFIG_QM_TLV_TAG_BYTE == 2)
    len += 2;
#endif
    len += 1;

    type_len = qm_tlv_block_data_type_len((qm_tlv_data_type_t)tlv_block->type);
    if(type_len > 0){
        len += type_len;
    }else if(type_len == 0){

        #if (CONFIG_QM_TLV_LENGTH_BYTE == 1)
        len += 1;
        #elif(CONFIG_QM_TLV_LENGTH_BYTE == 2)
        len += 2;
        #endif

        len += tlv_block->len;
    }
    return len;
}

static int _qm_ttlv_deserialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int len)
{
    int index = 0;
    int type_len = 0;
    uint8_t type = 0;
#if (CONFIG_QM_TTLV_TAG_SIZE == 2) || (CONFIG_QM_TTLV_LENGTH_SIZE == 2)
    uint16_t tmp = 0;
#endif
    qm_err_t ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;

    if(tlv_ctx == NULL || msg == NULL || len == 0){
        return -QM_EINVAL;
    }

    while(index < len){

        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            ret = -QM_ENOMEM;
            goto __exit;
        }

    #if CONFIG_QM_TTLV_TAG_SIZE == 1
        if(len - index < 1){
            ret = -QM_EINVAL;
            goto __exit;
        }
        tlv_block->tag = *(msg + index);
        index++;
    #elif CONFIG_QM_TTLV_TAG_SIZE == 2
        if(len - index < 2){
            ret = -QM_EINVAL;
            goto __exit;
        }
        memcpy(&tmp, msg + index, sizeof(uint16_t));
        tlv_block->tag = be16_to_cpu(tmp);
        index += 2;
    #endif

        type = *(msg + index);
        type_len = qm_tlv_block_data_type_len((qm_tlv_data_type_t)type);
        if(type_len < 0){
            ret = -QM_EINVAL;
            goto __exit;
        }
        tlv_block->type = type;
        index++;

        if(type_len == 0){
        #if CONFIG_QM_TTLV_LENGTH_SIZE == 1
            if(len - index < 1){
                ret = -QM_EINVAL;
                goto __exit;
            }
            tlv_block->len = *(msg + index);
            index++;
        #elif CONFIG_QM_TTLV_LENGTH_SIZE == 2
            if(len - index < 2){
                ret = -QM_EINVAL;
                goto __exit;
            }
            memcpy(&tmp, msg + index, sizeof(uint16_t));
            tlv_block->len = be16_to_cpu(tmp);
            index += 2;
        #endif
        }else{
            tlv_block->len = type_len;
        }

        if(len - index < tlv_block->len){
            ret = -QM_EINVAL;
            goto __exit;
        }

        ret = qm_tlv_pack_bytes(tlv_block, msg + index, tlv_block->len);
        if(ret != QM_EOK){
            goto __exit;
        }

        index += tlv_block->len;

        ret = qm_tlv_block_add(tlv_ctx, tlv_block);
        if(ret != QM_EOK){
            goto __exit;
        }
    }

    return QM_EOK;

__exit:
    if(tlv_block){
        tlv_free(tlv_block);
        tlv_block = NULL;
    }
    return ret;
}

static int _qm_ttlv_serialize(qm_tlv_ctx_t *tlv_ctx, uint8_t *msg, int *len, qm_tlv_serialize_type_t serialize_type)
{
    int index = 0;
    int offset = *len;
    int block_len = 0;
    int type_len = 0;
    #if (CONFIG_QM_TTLV_TAG_SIZE == 2) || (CONFIG_QM_TTLV_LENGTH_SIZE == 2)
    uint16_t tmp = 0;
    #endif  
    uint8_t *bytes = NULL;
    int size = 0;
    qm_tlv_block_t *tlv_block = NULL;
    if(tlv_ctx == NULL || len == NULL){
        return -QM_EINVAL;
    }

    tlv_block = tlv_ctx->tlv_block;

    while(tlv_block){

        if(serialize_type == QM_TLV_SERIALIZE_TYPE_PARTIAL){
            if(!tlv_block->flag.serialization){
                tlv_block = tlv_block->next;
                continue;
            }else{
                if(msg){
                    block_len = ttlv_block_len(tlv_block);
                    if(offset - index < block_len){
                        break;
                    }
                    tlv_block->flag.serialization = 0;
                }
            }
        }else if(serialize_type == QM_TLV_SERIALIZE_TYPE_UPDATE){
            if(!tlv_block->flag.update){
                tlv_block = tlv_block->next;
                continue;
            }else{
                if(msg){
                    block_len = ttlv_block_len(tlv_block);
                    if(offset - index < block_len){
                        break;
                    }
                    tlv_block->flag.update = 0;
                }
            }
        }else{
            if(msg){
                block_len = ttlv_block_len(tlv_block);
                if(offset - index < block_len){
                    break;
                }
            }
        }

    #if CONFIG_QM_TTLV_TAG_SIZE == 1
        if(msg){
            *(msg + index) = tlv_block->tag;
        }
        index ++;
    #elif CONFIG_QM_TTLV_TAG_SIZE == 2
        if(msg){
            tmp = cpu_to_be16(tlv_block->tag);
            memcpy(msg+index, &tmp, 2);
        }
        index += 2;
    #endif

        if(msg){
            *(msg + index) = tlv_block->type;
        }
        index ++;

        type_len = qm_tlv_block_data_type_len((qm_tlv_data_type_t)tlv_block->type);
        if(type_len == 0){
        #if CONFIG_QM_TTLV_LENGTH_SIZE == 1
            if(msg){
                *(msg + index) = tlv_block->len;
            }
            index ++;
        #elif CONFIG_QM_TTLV_LENGTH_SIZE == 2
            if(msg){
                tmp = cpu_to_be16(tlv_block->len);
                memcpy(msg + index, &tmp, 2);
            }
            index += 2;
        #endif
        }

        if(msg){
            qm_tlv_unpack_bytes_direct(tlv_block, &bytes, &size);
            if(bytes){
                memcpy(msg + index, bytes, size);
            }
        }
        index += tlv_block->len;
        tlv_block = tlv_block->next;
    }

    *len = index;

    return QM_EOK; 
}

#endif

int qm_tlv_merge(qm_tlv_ctx_t *dest_tlv_ctx, qm_tlv_ctx_t *src_tlv_ctx)
{
    qm_err_t ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    if(dest_tlv_ctx == NULL || src_tlv_ctx == NULL){
        return -QM_EINVAL;
    }

    tlv_block = src_tlv_ctx->tlv_block;
    while(tlv_block){

        m_tlv_block = qm_tlv_block_find(dest_tlv_ctx, (uint16_t)tlv_block->tag);
        if(m_tlv_block){
            ret = qm_tlv_block_copy(m_tlv_block, tlv_block);
            if(ret != QM_EOK){
                return ret;
            }
        }else{
            m_tlv_block = qm_tlv_block_creat();
            if(m_tlv_block == NULL){
                return -QM_ENOMEM;
            }

            ret = qm_tlv_block_copy(m_tlv_block, tlv_block);
            if(ret != QM_EOK){
                return ret;
            }

            ret = qm_tlv_block_add(dest_tlv_ctx, m_tlv_block);
            if(ret != QM_EOK){
                return ret;
            }
        }
        tlv_block = tlv_block->next;
    }

    return QM_EOK;
}

int qm_tlv_block_divide(qm_tlv_block_t *tlv_block)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }

    tlv_block->flag.divide = 1;

    return QM_EOK;
}


int qm_tlv_divide(qm_tlv_ctx_t *dest_tlv_ctx, qm_tlv_ctx_t *src_tlv_ctx, qm_tlv_divide_type_t divide_type)
{
    qm_err_t ret = QM_EOK;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;

    if(dest_tlv_ctx == NULL || src_tlv_ctx == NULL || divide_type >= QM_TLV_DIVIDE_TYPE_MAX){
        return -QM_EINVAL;
    }

    tlv_block = src_tlv_ctx->tlv_block;
    while(tlv_block){

        if(divide_type == QM_TLV_DIVIDE_TYPE_UPDATE){
            if(!tlv_block->flag.update){
                tlv_block = tlv_block->next;
                continue;
            }
        }else if(divide_type == QM_TLV_DIVIDE_TYPE_GENERIC){
            if(!tlv_block->flag.divide){
                tlv_block = tlv_block->next;
                continue;
            }else{
                tlv_block->flag.divide = 0;
            }
        }

        m_tlv_block = qm_tlv_block_creat();
        if(m_tlv_block == NULL){
            return -QM_ENOMEM;
        }
        ret = qm_tlv_block_copy(m_tlv_block, tlv_block);
        if(ret != QM_EOK){
            return ret;
        }

        qm_tlv_block_add(dest_tlv_ctx, m_tlv_block);

        tlv_block = tlv_block->next;
    }
    return QM_EOK;
}


qm_tlv_block_t *qm_tlv_block_next(qm_tlv_ctx_t *tlv_ctx, qm_tlv_block_t *tlv_block)
{
    if(tlv_block == NULL){
        if(tlv_ctx == NULL){
            return NULL;
        }
        return tlv_ctx->tlv_block;
    }else{
        return tlv_block->next;
    }
}

int qm_tlv_unpack_bool(qm_tlv_block_t *tlv_block, bool_t *value)
{
    return qm_tlv_unpack_uint8(tlv_block, (uint8_t*)value);
}

int qm_tlv_unpack_uint8(qm_tlv_block_t *tlv_block, uint8_t *value)
{
    if(tlv_block == NULL || value == NULL){
        return -QM_EINVAL;
    }
    *value = (uint8_t)tlv_block->value.bytes[0];
    tlv_block->flag.update = 0;
    return QM_EOK;
}

int qm_tlv_unpack_int8(qm_tlv_block_t *tlv_block, int8_t *value)
{
    return qm_tlv_unpack_uint8(tlv_block, (uint8_t*)value);
}

#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
int qm_tlv_unpack_uint16(qm_tlv_block_t *tlv_block, uint16_t *value)
{
    uint16_t tmp = 0;
    if(tlv_block == NULL || value == NULL){
        return -QM_EINVAL;
    }
    memcpy(&tmp, tlv_block->value.bytes, sizeof(uint16_t));
    *value = be16_to_cpu(tmp);
    tlv_block->flag.update = 0;
    return QM_EOK;
}

int qm_tlv_unpack_int16(qm_tlv_block_t *tlv_block, int16_t *value)
{
    return qm_tlv_unpack_uint16(tlv_block, (uint16_t*)value);
}
#endif

#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
int qm_tlv_unpack_uint32(qm_tlv_block_t *tlv_block, uint32_t *value)
{
    uint32_t tmp = 0;
    if(tlv_block == NULL || value == NULL){
        return -QM_EINVAL;
    }
    memcpy(&tmp, tlv_block->value.bytes, sizeof(uint32_t));
    *value = be32_to_cpu(tmp);
    tlv_block->flag.update = 0;
    return QM_EOK;
}

int qm_tlv_unpack_int32(qm_tlv_block_t *tlv_block, int32_t *value)
{
    return qm_tlv_unpack_uint32(tlv_block, (uint32_t*)value);
}
#endif

#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
int qm_tlv_unpack_float32(qm_tlv_block_t *tlv_block, float32_t *value)
{
    if(tlv_block == NULL || value == NULL){
        return -QM_EINVAL;
    }
    memcpy(value, tlv_block->value.bytes, sizeof(float32_t));
    tlv_block->flag.update = 0;
    return QM_EOK;
}
#endif

#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
int qm_tlv_unpack_bytes(qm_tlv_block_t *tlv_block, uint8_t *bytes, int *size)
{
    if(tlv_block == NULL || size == NULL){
        return -QM_EINVAL;
    }

    *size = (int)tlv_block->len;

    if(bytes == NULL){
        return QM_EOK;
    }

    if(*size < tlv_block->len){
        return -QM_EINVAL;
    }

    if(tlv_block->len <= CONFIG_QM_TLV_BUF_SIZE){
        memcpy(bytes, tlv_block->value.bytes, tlv_block->len);
    }else{
        memcpy(bytes, tlv_block->value.pdata, tlv_block->len);
    }
    tlv_block->flag.update = 0;

    return QM_EOK;
}

int qm_tlv_unpack_string(qm_tlv_block_t *tlv_block, char *str, int *size)
{
    if(tlv_block == NULL || size == NULL){
        return -QM_EINVAL;
    }

    *size = (int)tlv_block->len+1;

    if(str == NULL){
        return QM_EOK;
    }

    if(*size < (tlv_block->len + 1)){
        return -QM_EINVAL;
    }

    if(tlv_block->len <= CONFIG_QM_TLV_BUF_SIZE){
        memcpy(str, tlv_block->value.bytes, tlv_block->len);
    }else{
        memcpy(str, tlv_block->value.pdata, tlv_block->len);
    }
    
    *(str+tlv_block->len) = '\0';
    tlv_block->flag.update = 0;
    return QM_EOK;
}

int qm_tlv_unpack_bytes_direct(qm_tlv_block_t *tlv_block, uint8_t **bytes, int *size)
{
    if(tlv_block == NULL || bytes == NULL || size == NULL){
        return -QM_EINVAL;
    }

    *size = (int)tlv_block->len;

    if(tlv_block->len <= CONFIG_QM_TLV_BUF_SIZE){
        *bytes = tlv_block->value.bytes;
    }else{
        *bytes = tlv_block->value.pdata;
    }
    tlv_block->flag.update = 0;

    return QM_EOK;
}

int qm_tlv_unpack_string_direct(qm_tlv_block_t *tlv_block, char **str, int *size)
{
    return qm_tlv_unpack_bytes_direct(tlv_block, (uint8_t**)str, size);
}
#endif

int qm_tlv_pack_bool(qm_tlv_block_t *tlv_block, bool_t value)
{
    return qm_tlv_pack_uint8(tlv_block, (uint8_t)value);
}

int qm_tlv_pack_uint8(qm_tlv_block_t *tlv_block, uint8_t value)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
    
    if(tlv_block->len > CONFIG_QM_TLV_BUF_SIZE){
        if(tlv_block->value.pdata){
            qm_free(tlv_block->value.pdata);
            tlv_block->value.pdata = NULL;
        }
    }
    
    tlv_block->value.bytes[0] = (uint8_t)value;
    #if CONFIG_QM_TLV_LENGTH_BYTE == 1
    tlv_block->len = (uint8_t)sizeof(uint8_t);
    #elif CONFIG_QM_TLV_LENGTH_BYTE == 2
    tlv_block->len = (uint16_t)sizeof(uint8_t);
    #endif

    return QM_EOK;
}

int qm_tlv_pack_int8(qm_tlv_block_t *tlv_block, int8_t value)
{
    return qm_tlv_pack_uint8(tlv_block, (uint8_t)value);
}
#if CONFIG_QM_DATA_TYPE_INT16_SUPPORT
int qm_tlv_pack_uint16(qm_tlv_block_t *tlv_block, uint16_t value)
{
    uint16_t tmp = 0;
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }

    if(tlv_block->len > CONFIG_QM_TLV_BUF_SIZE){
        if(tlv_block->value.pdata){
            qm_free(tlv_block->value.pdata);
            tlv_block->value.pdata = NULL;
        }
    }

    tmp = cpu_to_be16(value);
    memset(tlv_block->value.bytes, 0, CONFIG_QM_TLV_BUF_SIZE);
    memcpy(tlv_block->value.bytes, &tmp, sizeof(uint16_t));
    #if CONFIG_QM_TLV_LENGTH_BYTE == 1
    tlv_block->len = (uint8_t)sizeof(uint16_t);
    #elif CONFIG_QM_TLV_LENGTH_BYTE == 2
    tlv_block->len = (uint16_t)sizeof(uint16_t);
    #endif

    return QM_EOK;
}

int qm_tlv_pack_int16(qm_tlv_block_t *tlv_block, int16_t value)
{
    return qm_tlv_pack_uint16(tlv_block, (uint16_t)value);
}
#endif
#if CONFIG_QM_DATA_TYPE_INT32_SUPPORT
int qm_tlv_pack_uint32(qm_tlv_block_t *tlv_block, uint32_t value)
{
    uint32_t tmp = 0;
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }

    if(tlv_block->len > CONFIG_QM_TLV_BUF_SIZE){
        if(tlv_block->value.pdata){
            qm_free(tlv_block->value.pdata);
            tlv_block->value.pdata = NULL;
        }
    }

    tmp = cpu_to_be32(value);
    memset(tlv_block->value.bytes, 0, CONFIG_QM_TLV_BUF_SIZE);
    memcpy(tlv_block->value.bytes, &tmp, sizeof(uint32_t));
    #if CONFIG_QM_TLV_LENGTH_BYTE == 1
    tlv_block->len = (uint8_t)sizeof(uint32_t);
    #elif CONFIG_QM_TLV_LENGTH_BYTE == 2
    tlv_block->len = (uint16_t)sizeof(uint32_t);
    #endif

    return QM_EOK;
}

int qm_tlv_pack_int32(qm_tlv_block_t *tlv_block, int32_t value)
{
    return qm_tlv_pack_uint32(tlv_block, (uint32_t)value);
}
#endif

#if CONFIG_QM_DATA_TYPE_FLOAT32_SUPPORT
int qm_tlv_pack_float32(qm_tlv_block_t *tlv_block, float32_t value)
{
    if(tlv_block == NULL){
        return -QM_EINVAL;
    }
    memset(tlv_block->value.bytes, 0, CONFIG_QM_TLV_BUF_SIZE);
    memcpy(tlv_block->value.bytes, &value, sizeof(float32_t));
    #if CONFIG_QM_TLV_LENGTH_BYTE == 1
    tlv_block->len = (uint8_t)sizeof(float32_t);
    #elif CONFIG_QM_TLV_LENGTH_BYTE == 2
    tlv_block->len = (uint16_t)sizeof(float32_t);
    #endif
    return QM_EOK;
}
#endif

#if CONFIG_QM_DATA_TYPE_BYTES_SUPPORT
int qm_tlv_pack_bytes(qm_tlv_block_t *tlv_block, uint8_t *bytes, int size)
{
    if(tlv_block == NULL || bytes == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(tlv_block->len > CONFIG_QM_TLV_BUF_SIZE){
        if(tlv_block->value.pdata){
            qm_free(tlv_block->value.pdata);
            tlv_block->value.pdata = NULL;
        }
    }

    if(size <= CONFIG_QM_TLV_BUF_SIZE){
        memset(tlv_block->value.bytes, 0, CONFIG_QM_TLV_BUF_SIZE);
        memcpy(tlv_block->value.bytes, bytes, size);
    }else{
        tlv_block->value.pdata = (uint8_t*)tlv_malloc(size);
        if(tlv_block->value.pdata == NULL){
            return -QM_ENOMEM;   
        }
        memset(tlv_block->value.pdata, 0, size);
        memcpy(tlv_block->value.pdata, bytes, size);
    }

    #if CONFIG_QM_TLV_LENGTH_BYTE == 1
    tlv_block->len = (uint8_t)size;
    #elif CONFIG_QM_TLV_LENGTH_BYTE == 2
    tlv_block->len = (uint16_t)size;
    #endif

    return QM_EOK;
}

int qm_tlv_pack_string(qm_tlv_block_t *tlv_block, char *str, int size)
{
    return qm_tlv_pack_bytes(tlv_block, (uint8_t*)str, size);
}
#endif
