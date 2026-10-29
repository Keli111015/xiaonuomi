#ifndef _NC_AUDIO_H_
#define _NC_AUDIO_H_

/**
 * @file nc_audio.h
 * @brief GX8006 nc_audio UART protocol wrapper used by the platform audio binding.
 *
 * The module packs speaker/control commands into the GX8006 UART frame format
 * and parses upstream microphone, wakeup, sleep, and playback-state events.
 */

#include "qm.h"

#ifdef __cplusplus
extern "C" {
#endif


#ifndef CONFIG_NC_AUDIO_RECV_MAX_SIZE  
#define CONFIG_NC_AUDIO_RECV_MAX_SIZE        (512)
#endif

/** @brief Receive timeout measured from the frame header. */
#ifndef CONFIG_NC_AUDIO_RECV_TIMEOUT_MS  
#define CONFIG_NC_AUDIO_RECV_TIMEOUT_MS      (2000)
#endif

/** @brief Maximum version string length reported by the module. */
#ifndef CONFIG_NC_AUDIO_RECV_VER_MAX_LEN  
#define CONFIG_NC_AUDIO_RECV_VER_MAX_LEN      (20)
#endif

/** @brief Default command send retry count. */
#ifndef CONFIG_NC_AUDIO_SEND_COUNT 
#define CONFIG_NC_AUDIO_SEND_COUNT            (1)
#endif

/** @brief Command send acknowledgement timeout. */
#ifndef CONFIG_NC_AUDIO_SEND_TIMEOUT_MS  
#define CONFIG_NC_AUDIO_SEND_TIMEOUT_MS      (100)
#endif

/**
 * @brief GX8006 UART frame layout.
 *
 *   ----------------------------------------------------------------
 *  |  FrameHeader |  VER   |  CMD    | LEN    |  DATA  |  ADD8 |
 *   ----------------------------------------------------------------
 *  |     2Bytes   |  1Byte |  1Byte  | 2Bytes | NBytes  | 1Byte |
 */

#define NC_AUDIO_FRAME_HEADER_1  (0x55)
#define NC_AUDIO_FRAME_HEADER_2  (0xAA)
#define NC_AUDIO_FRAME_VER       (0x03)
#define NC_AUDIO_FRAME_VER_1     (0x00)
#define NC_AUDIO_FRAME_CMD       (0x92)

#define NC_AUDIO_FRAM_PORT_LEN   (3)    /**< HEAD(2byte) + VER(1byte). */
#define NC_AUDIO_FRAME_HEAD_LEN  (7)    /**< HEAD + VER + CMD + LEN + SUB_CMD. */
#define NC_AUDIO_FRAME_MIN_LEN   (8)    /**< HEAD + VER + CMD + LEN + SUB_CMD + ADDR8. */


#define NC_AUDIO_FRAME_SUB_CMD_ADDR  (6)    /**< Offset of SUB_CMD in a full frame. */


/** @brief GX8006 protocol sub-command values carried in NC_AUDIO_FRAME_CMD. */
typedef enum
{
    NC_AUDIO_SUB_CMD_NONE                   = 0x00,
    NC_AUDIO_SUB_CMD_GET_MCU_VER            = 0x01, /**< MCU version query. */
    NC_AUDIO_SUB_CMD_MIC_CFG                = 0x02, /**< Offline-voice MIC config. */
    NC_AUDIO_SUB_CMD_SPK_CFG                = 0x03, /**< Offline-voice SPK config. */
    NC_AUDIO_SUB_CMD_WAKE_IO_CFG            = 0x04, /**< Wake GPIO config. */
    NC_AUDIO_SUB_CMD_MIC_DATA               = 0x05, /**< MIC data upload. */
    NC_AUDIO_SUB_CMD_SPK_PLAY               = 0x06, /**< SPK playback data. */
    NC_AUDIO_SUB_CMD_AUDIO_CFG              = 0x07, /**< Offline-voice config command. */
    NC_AUDIO_SUB_CMD_AUDIO_WAKEUP           = 0x08, /**< Wakeup event. */
    NC_AUDIO_SUB_CMD_AUDIO_SLEEP            = 0x09, /**< Sleep event. */
    NC_AUDIO_SUB_CMD_GET_STATUS             = 0x0A, /**< Module status query. */
    NC_AUDIO_SUB_CMD_RECORD_TEST            = 0x0B, /**< Record/playback test. */
    NC_AUDIO_SUB_CMD_SKP_REPLY              = 0x12, /**< SPK playback state report. */
    NC_AUDIO_SUB_CMD_RECORD_FUNC            = 0xF5, /**< Record mode switch. */
    NC_AUDIO_SUB_CMD_PLAY_STATE_NOTIFY      = 0xF6, /**< Playback state notify. */
}nc_audio_frame_sub_cmd_t;


/** @brief UART field identifiers retained for protocol diagnostics. */
typedef enum
{
    NC_AUDIO_PARSE_STATE_HEAD_1 = 1,
    NC_AUDIO_PARSE_STATE_HEAD_2,
    NC_AUDIO_PARSE_STATE_VER,
    NC_AUDIO_PARSE_STATE_CMD,
    NC_AUDIO_PARSE_STATE_LEN,
    NC_AUDIO_PARSE_STATE_DATA,
    NC_AUDIO_PARSE_STATE_MAX,
}nc_audio_parse_state_t;


/** @brief Normalized events produced by the nc_audio protocol parser. */
typedef enum{
    NC_AUDIO_EVENT_TYPE_WAKEUP,
    NC_AUDIO_EVENT_TYPE_SLEEP,
    NC_AUDIO_EVENT_TYPE_GET_MCU_VER,
    NC_AUDIO_EVENT_TYPE_MIC_CFG,
    NC_AUDIO_EVENT_TYPE_SPK_CFG,
    NC_AUDIO_EVENT_TYPE_MIC_DATA_GET,
    NC_AUDIO_EVENT_TYPE_SPK_PLAY_ACK,
    NC_AUDIO_EVENT_TYPE_SPK_REPLY,
    NC_AUDIO_EVENT_MAX,
}nc_audio_event_type_t;

/** @brief Audio codec values used by GX8006 configuration commands. */
typedef enum {
    NC_AUDIO_FMT_TYPE_PCM = 0,
    NC_AUDIO_FMT_TYPE_SPEEX,
    NC_AUDIO_FMT_TYPE_OPUS,
    NC_AUDIO_FMT_TYPE_MP3,
} nc_audio_fmt_type_t;    


#pragma pack(1)

/** @brief Additive checksum byte appended at the end of each frame. */
typedef union 
{
    uint8_t add8;
}nc_audio_fram_check_t;

/** @brief Common GX8006 frame header followed by variable payload and checksum. */
typedef struct 
{   
    uint8_t head_1;
    uint8_t head_2;
    uint8_t ver;
    uint8_t cmd;
    uint16_t len;
    uint8_t sub_cmd;
    uint8_t payload[1]; 
}nc_audio_frame_header_t;

/** @brief Microphone stream configuration payload. */
typedef struct 
{   
    uint32_t rate;
    uint8_t bit; 
    uint8_t channel; 
    uint8_t mic_gain;
    uint8_t type; /**< Data format: 0 PCM, 1 Speex, 2 Opus. */
    uint32_t size;
}nc_audio_mic_cfg_t;

/** @brief Speaker stream configuration payload. */
typedef struct 
{   
    uint32_t rate;
    uint8_t bit; 
    uint8_t spk_gain;
    uint8_t type; /**< Data format: 0 PCM, 1 Speex, 2 Opus. */
    uint32_t size;
}nc_audio_spk_cfg_t;

/** @brief Speaker playback command header without inline payload. */
typedef struct 
{   
   uint8_t type; /**< 0 start, 1 data, 2 stop. */
   uint32_t msg_id; /**< Monotonic packet id. */
}nc_audio_spk_play_header_t;

/** @brief Speaker playback command payload used for framed SPK data. */
typedef struct 
{   
   uint8_t type; /**< 0 start, 1 data, 2 stop. */
   uint32_t msg_id; /**< Monotonic packet id. */
   uint8_t playload[1];
}nc_audio_spk_play_t;

/** @brief Small one-byte audio configuration payload. */
typedef struct 
{   
   uint8_t type; /**< Configuration type. */
   uint8_t value; /**< One-byte configuration value. */
}nc_audio_cfg_t;

/** @brief Extended audio configuration payload for values wider than one byte. */
typedef struct 
{   
   uint8_t type; /**< Configuration type. */
   int value; /**< Extended configuration value. */
}nc_audio_ext_cfg_t;

/** @brief Record-control payload sent to the voice module. */
typedef struct 
{   
   uint8_t on_off; /**< Record enable switch. */
   uint8_t ns_level; /**< Noise-suppression level. */
}nc_audio_record_func_t;


/** @brief MCU version and flow-control information reported by GX8006. */
typedef struct 
{
   uint8_t type; /**< 0 first power-on report, 1 query response. */
   uint8_t status; /**< SPK flow-control IO, 0xFF means unused. */
   uint8_t level; /**< Active level: 0 low active, 1 high active. */
   char    str_ver[CONFIG_NC_AUDIO_RECV_VER_MAX_LEN]; /**< Version string. */
}nc_audio_get_mcu_ver_t;

#pragma pack()

/** @brief Speaker playback packet type. */
typedef enum {
    NC_AUDIO_SPK_PLAY_TYPE_START = 0,
    NC_AUDIO_SPK_PLAY_TYPE_DATA,
    NC_AUDIO_SPK_PLAY_TYPE_STOP,
} nc_audio_spk_play_type_t;  

/** @brief Speaker playback state reported by GX8006. */
typedef enum {
    NC_AUDIO_SPK_REPLY_TYPE_START = 1,
    NC_AUDIO_SPK_REPLY_TYPE_EMPTY,
    NC_AUDIO_SPK_REPLY_TYPE_STOP,
} nc_audio_spk_reply_type_t;  

/** @brief VAD state carried with microphone data. */
typedef enum 
{
    NC_AUDIO_VAD_START = 0,
    NC_AUDIO_VAD_DATA,
    NC_AUDIO_VAD_STOP,
}nc_audio_vad_status_t;

/** @brief Audio configuration command type. */
typedef enum {
    NC_AUDIO_CFG_TYPE_CHANNEL = 0,
    NC_AUDIO_CFG_TYPE_MIC_ONOFF,
    NC_AUDIO_CFG_TYPE_SPK_ONOFF,
    NC_AUDIO_CFG_TYPE_WAKEUP_ONOFF,
    NC_AUDIO_CFG_TYPE_VAD_ONOFF,
    NC_AUDIO_CFG_TYPE_WAKEUP_ONTIFY_ONOFF,
    NC_AUDIO_CFG_TYPE_TIMEOUT_ONTIFY_ONOFF,

    NC_AUDIO_CFG_TYPE_SLIENT_TIMEOUT = 7,
    NC_AUDIO_CFG_TYPE_MAX_RECORD_TIME = 8,
    NC_AUDIO_CFG_TYPE_VAD_SENSITIVITY = 9,
    NC_AUDIO_CFG_TYPE_NOISE_REDUCTION = 10,


} nc_audio_cfg_type_t;  

/** @brief Generic command result payload with optional message id. */
typedef struct 
{
   uint32_t msg_id;
   uint8_t code; /**< Result code: 0x00 success, 0x01 failure. */
}nc_audio_code_t;

/** @brief Parsed microphone data event payload. */
typedef struct 
{
   nc_audio_vad_status_t status; /**< 0 VAD start, 1 VAD data, 2 VAD end. */
   uint16_t payload_len;
   uint8_t *payload;
}nc_audio_mic_data_t;

/** @brief Parsed speaker state event payload. */ 
typedef struct 
{
   nc_audio_spk_reply_type_t status; /**< 1 SPK start, 2 empty, 3 stop. */
}nc_audio_spk_state_t;

/** @brief Event payload union passed to nc_audio_event_t callbacks. */
typedef union 
{
    nc_audio_get_mcu_ver_t mcu_ver;
    nc_audio_code_t mic_cfg_result;
    nc_audio_code_t spk_cfg_result;
    nc_audio_code_t spk_play_ack;
    nc_audio_mic_data_t mic_data;
    nc_audio_spk_state_t spk_state;
}nc_audio_event_param_t;

/**
 * @brief Low-level byte sender used by nc_audio to write framed UART data.
 *
 * @param data Frame bytes.
 * @param len Frame byte length.
 * @param timeout_ms Total transmission budget; zero performs no waiting.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
typedef int (*nc_audio_send_t)(uint8_t *data, int len, uint32_t timeout_ms);

/**
 * @brief Event callback invoked after parsing a complete GX8006 message.
 *
 * @param event_type Event identifier.
 * @param p_event Event payload, valid only during the callback.
 * @return QM_EOK on success, negative qm_errno value on failure.
 */
typedef int (*nc_audio_event_t)(nc_audio_event_type_t event_type, nc_audio_event_param_t *p_event);

/** @brief One direction's fixed format, expressed in host byte order. */
typedef struct {
    nc_audio_fmt_type_t codec;
    uint32_t sample_rate_hz;
    uint32_t max_payload_bytes; /**< Protocol data capacity, not Opus packet size. */
    uint8_t sample_bits;
    uint8_t channels;
} nc_audio_format_t;

/**
 * @brief Runtime configuration passed to nc_audio_init().
 *
 * One owner initializes/deinitializes the singleton. recv_push must have one
 * caller; notification runs synchronously in that caller and may issue send
 * commands, but must not call recv_push or deinit. Commands are serialized.
 * Stop the receive caller and command producers before deinit.
 */
typedef struct
{
    nc_audio_send_t send;                  /**< Low-level frame sender. */
    nc_audio_event_t notify;               /**< Event callback. */
    nc_audio_format_t mic;                 /**< Independent microphone format. */
    nc_audio_format_t spk;                 /**< Independent speaker format. */
    uint8_t                       mic_gain;          /**< MIC gain. */
    uint8_t                       spk_gain;          /**< SPK gain. */
    uint8_t                       vad_timeout;       /**< Silent timeout in seconds. */
}nc_audio_param_t;

/**
 * @brief 初始化模块并设置默认参数
 *
 * @param[in] param 初始化参数，按值复制。
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_init(nc_audio_param_t *param);

/**
 * @brief Deinitialize module resources created by nc_audio_init().
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_deinit(void);


/**
 * @brief 事件重新注册
 *
 * @param[in] param 初始化参数，详见 @ref aupu_old_comm_param_t
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_event_register(nc_audio_event_t event_notify);

/**
 * @brief Synchronously parse arbitrary UART chunks; no payload is retained.
 *
 * @param[in] data 数据句柄
 * @param[in] len 数据长度
 * 
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_recv_push(uint8_t *data, int len);

/**
 * @brief 音频播报
 *
 * @param[in] data 数据句柄
 * @param[in] len 数据长度
 * 
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_spk_play(nc_audio_spk_play_header_t *play, uint8_t *payload,
                         int payload_len, uint32_t timeout_ms);


/**
 * @brief 音频配置
 *
 * @param[in] value 音量增益: 0~100
 * 
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_set_spk_volume(uint8_t value);

/**
 * @brief 音频配置
 *
 * @param[in] data 数据句柄
 * @param[in] len 数据长度
 * 
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_cfg(nc_audio_cfg_type_t type, int value);


/**
 * @brief 录音开关
 *
 * @param[in] data 数据句柄
 * @param[in] len 数据长度
 * 
 *
 * @return int32_t
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int32_t nc_audio_record_func(nc_audio_record_func_t func);

/**
 * @brief 设置MIC增益
 *
 * @param[in] gain 数据长度
 * 
 *
 * @return int
 * @retval <QM_EOK 执行失败
 * @retval >=QM_EOK 执行成功
 */
int mcu_set_mic_gain(uint8_t gain);
#ifdef __cplusplus
}
#endif

#endif
