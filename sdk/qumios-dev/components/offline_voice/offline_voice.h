#ifndef OFFLINE_VOICE_H
#define OFFLINE_VOICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_tlv.h"
#include "offline_voice_common.h"

typedef uint32_t offline_voice_platform_id_t;

/*
 *   ----------------------------------------------------------------
 *  |  HEAD  |  VER  |  CMD   |   LENGTH |    PAYLOAD    |  CHKSUM  |
 *   ----------------------------------------------------------------
 *    2Bytes   1Byte    1Byte     2Bytes      n Byte        1Byte
 */
#define OFFLINE_VOICE_FRAME_HEAD                    0xA5A5
#define OFFLINE_VOICE_FRAME_HEAD_PART               0xA5
#define OFFLINE_VOICE_FRAME_HEAD_LEN                6  /*HEAD + VER + CMD + LENGTH*/
#define OFFLINE_VOICE_FRAME_LEN                     9  /*HEAD + VER + CMD + LENGTH + ID + ERROR_CODE + CHKSUM*/
#define OFFLINE_VOICE_PROTOCOL_VER                  0x03
#define OFFLINE_VOICE_DATA_MIN_LEN                  (2)


#define MIN_OFFLINE_VOICE_PACKET_ID  248
#define MAX_OFFLINE_VOICE_PACKET_ID  255

typedef enum
{
    OFFLINE_VOICE_CMD_SET           = 0x01,
    OFFLINE_VOICE_CMD_GET_MODE      = 0x50,
    OFFLINE_VOICE_CMD_ENTER_FACTORY = 0x61,
    OFFLINE_VOICE_CMD_ERROR         = 0xFF,
}offline_voice_frame_cmd_t;

typedef enum
{
    OFFLINE_VOICE_GET_MODE_FWNAME   = 0xC8,
    OFFLINE_VOICE_GET_MODE_INVALID  = 0xFF,
}offline_voice_getmode_attrid_t;

typedef enum{
   OFFLINE_VOICE_RESULT_OK      = 0,
   OFFLINE_VOICE_RESULT_FAIL    = 1,
   OFFLINE_VOICE_RESULT_TIMEOUT = 2,
}offline_voice_result_t;

typedef struct{
    uint8_t mode;
    uint8_t count;
    uint16_t wait_time;
}offline_voice_factory_t;

typedef int32_t (*offline_voice_factory_fn)(void *handle , offline_voice_result_t result);
typedef int32_t (*offline_voice_get_mode_fn)(uint8_t error_code, qm_tlv_block_t *qm_tlv_block);
typedef int32_t (*offline_voicel_set_fn)(offline_voice_platform_id_t id, uint8_t *data, uint16_t len, qm_tlv_ctx_t *tlv_ctx);

int32_t offline_voice_loop(void);
int32_t offline_voice_deinit(void);
int32_t offline_voice_init(offline_voicel_set_fn *set_cb);
int32_t offline_voice_result_report(offline_voice_result_t result);
int32_t offline_voice_get_fwname(offline_voice_get_mode_fn get_cb);
int32_t offline_voice_enter_factory(offline_voice_factory_t factory_mode, offline_voice_factory_fn result);


#ifdef __cplusplus

}
#endif


#endif /* PRODTST_CLIENT_H */
