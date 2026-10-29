#include "qm_tlv.h"
#include "qm_log.h"
#include "qm_errno.h"

void qm_tlv_test(void)
{
    qm_err_t ret = 0;
    //test tlv serialize
    qm_tlv_ctx_t *tlv_ctx = NULL; 
    qm_tlv_block_t *tlv_block = NULL;
    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        QM_LOGD("tlv_test", "tlv ctx creat fail");
    }

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x01;
    qm_tlv_pack_bool(tlv_block, 1);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x02;
    qm_tlv_pack_uint8(tlv_block, 2);
    qm_tlv_block_add(tlv_ctx, tlv_block);
    
    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x03;
    qm_tlv_pack_int8(tlv_block, 3);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x04;
    qm_tlv_pack_uint16(tlv_block, 4);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x05;
    qm_tlv_pack_int16(tlv_block, 5);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x06;
    qm_tlv_pack_uint32(tlv_block, 6);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x07;
    qm_tlv_pack_int32(tlv_block, 7);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x06;
    qm_tlv_pack_uint32(tlv_block, 66);
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x08;
    uint8_t bytes_1[4] = {0x01, 0x02, 0x03, 0x04};
    qm_tlv_pack_bytes(tlv_block, bytes_1, sizeof(bytes_1));
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x09;
    uint8_t bytes_2[16] = {0};
    qm_tlv_pack_bytes(tlv_block, bytes_2, sizeof(bytes_2));
    qm_tlv_block_add(tlv_ctx, tlv_block);

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block creat fail");
    }
    tlv_block->tag = 0x0A;
    char *str_1 = "hello";
    qm_tlv_pack_string(tlv_block, str_1, strlen(str_1));
    qm_tlv_block_serialize(tlv_block);
    qm_tlv_block_add(tlv_ctx, tlv_block);
    
    
    uint8_t msg[100] = {0};
    int len = 0;

    qm_tlv_serialize(tlv_ctx, NULL, &len, QM_BLOCK_TYPE_TLV, QM_TLV_SERIALIZE_TYPE_PARTIAL);

    QM_LOGD("tlv_test", "serialize len: %d", len);

    qm_tlv_serialize(tlv_ctx, msg, &len, QM_BLOCK_TYPE_TLV, QM_TLV_SERIALIZE_TYPE_PARTIAL);

    QM_HEX_LOGD("tlv_test", "serialize msg: ", msg, len);

    qm_tlv_ctx_delete(tlv_ctx);

    //test tlv deserialize
    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        QM_LOGD("tlv_test", "tlv ctx creat fail");
    }
    ret = qm_tlv_deserialize(tlv_ctx, msg, len, QM_BLOCK_TYPE_TLV);
    if(ret != QM_EOK){
        QM_LOGD("tlv_test", "tlv ctx creat fail");
    }

    tlv_block = NULL;
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        QM_LOGD("tlv_test", "tag %d", tlv_block->tag);
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x01);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    bool_t bool_value;
    qm_tlv_unpack_bool(tlv_block, &bool_value);
    QM_LOGD("tlv_test", "bool value: %d", bool_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x02);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    uint8_t uint8_value;
    qm_tlv_unpack_uint8(tlv_block, &uint8_value);
    QM_LOGD("tlv_test", "uint8 value: %d", uint8_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x03);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    int8_t int8_value;
    qm_tlv_unpack_int8(tlv_block, &int8_value);
    QM_LOGD("tlv_test", "int8 value: %d", int8_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x04);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    uint16_t uint16_value;
    qm_tlv_unpack_uint16(tlv_block, &uint16_value);
    QM_LOGD("tlv_test", "uint16 value: %d", uint16_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x05);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    int16_t int16_value;
    qm_tlv_unpack_int16(tlv_block, &int16_value);
    QM_LOGD("tlv_test", "int16 value: %d", int16_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x06);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    uint32_t uint32_value;
    qm_tlv_unpack_uint32(tlv_block, &uint32_value);
    QM_LOGD("tlv_test", "uint32_t value: %d", uint32_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x07);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    int32_t int32_value;
    qm_tlv_unpack_int32(tlv_block, &int32_value);
    QM_LOGD("tlv_test", "int32_t value: %d", int32_value);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x08);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    uint8_t bytes_3[16] = {0};
    int size_1 = 0;
    qm_tlv_unpack_bytes(tlv_block, bytes_3, &size_1);
    QM_HEX_LOGD("tlv_test", "bytes value: ", bytes_3, size_1);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x09);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    uint8_t bytes_4[16] = {0};
    int size_2 = 0;
    qm_tlv_unpack_bytes(tlv_block, bytes_4, &size_2);
    QM_HEX_LOGD("tlv_test", "bytes value: ", bytes_4, size_2);

    tlv_block = qm_tlv_block_find(tlv_ctx, 0x0A);
    if(tlv_block == NULL){
        QM_LOGD("tlv_test", "tlv block find fail");
    }
    char str[16] = {0};
    int size_3 = 0;
    qm_tlv_unpack_string(tlv_block, str, &size_3);
    QM_LOGD("tlv_test", "str value: %s", str);

    qm_tlv_ctx_delete(tlv_ctx);
}