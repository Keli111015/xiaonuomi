#ifndef QM_CAN_H
#define QM_CAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

/**
 * @brief   CAN Constants
 */
#define QM_CAN_EXTD_ID_MASK               0x1FFFFFFF  /**< Bit mask for 29 bit Extended Frame Format ID */
#define QM_CAN_STD_ID_MASK                0x7FF       /**< Bit mask for 11 bit Standard Frame Format ID */
#define QM_CAN_FRAME_MAX_DLC              8           /**< Max data bytes allowed in TWAI */
#define QM_CAN_FRAME_EXTD_ID_LEN_BYTES    4           /**< EFF ID requires 4 bytes (29bit) */
#define QM_CAN_FRAME_STD_ID_LEN_BYTES     2           /**< SFF ID requires 2 bytes (11bit) */
#define QM_CAN_ERR_PASS_THRESH            128         /**< Error counter threshold for error passive */

/* -------------------- Default initializers and flags ---------------------- */
/**
 * @brief Initializer macro for general configuration structure.
 *
 * This initializer macros allows the TX GPIO, RX GPIO, and operating mode to be
 * configured. The other members of the general configuration structure are
 * assigned default values.
 */
#define QM_CAN_CONFIG_DEFAULT(op_mode, tx_io_num, rx_io_num, clock_hz)  {.mode  = op_mode, \
                                                                .tx_pin = tx_io_num, \
                                                                .rx_pin = rx_io_num,  \
                                                                .clock = clock_hz}

#define QM_CAN_FILTER_CONFIG_ACCEPT_ALL() {.acceptance_code = 0, .acceptance_mask = 0xFFFFFFFF}


/**
 * @brief   CAN Controller operating modes
 */
typedef enum {
    QM_CAN_MODE_NORMAL,               /**< Normal operating mode where TWAI controller can send/receive/acknowledge messages */
    QM_CAN_MODE_NO_ACK,               /**< Transmission does not require acknowledgment. Use this mode for self testing */
    QM_CAN_MODE_LISTEN_ONLY,          /**< The TWAI controller will not influence the bus (No transmissions or acknowledgments) but can receive messages */
} qm_can_mode_t;

/**
 * @brief   CAN Controller CLOCK FREQ
 */
typedef enum {
    QM_CAN_CLOCK_25KHZ         = 25,
    QM_CAN_CLOCK_50KHZ         = 50,
    QM_CAN_CLOCK_100KHZ        = 100,
    QM_CAN_CLOCK_125KHZ        = 125,
    QM_CAN_CLOCK_250KHZ        = 250,
    QM_CAN_CLOCK_500KHZ        = 500,
    QM_CAN_CLOCK_800KHZ        = 800,
    QM_CAN_CLOCK_1MHZ          = 1000,
} qm_can_clock_t;

/**
 * @brief   CAN Controller MSG FLAG
 */
typedef enum {
    QM_CAN_MSG_FLAG_NON         = 0,        /**< No message flags (Standard Frame Format) */
    QM_CAN_MSG_FLAG_EXTD        = 1 << 0,   /**< Extended Frame Format (29bit ID) */
    QM_CAN_MSG_FLAG_RTR         = 1 << 1,   /**< Message is a Remote Frame */
    QM_CAN_MSG_FLAG_SS          = 1 << 2,   /**< Transmit as a Single Shot Transmission. Unused for received. */
    QM_CAN_MSG_FLAG_SELF        = 1 << 3,   /**< Transmit as a Self Reception Request. Unused for received. */
} qm_can_msg_flag_t;

/**
 * @brief   CAN INIT CONFIG
 */
typedef struct {
    qm_can_mode_t mode;           /**< Mode of CAN controller */
    uint8_t tx_pin;               /**< Transmit GPIO number */
    uint8_t rx_pin;               /**< Receive GPIO number */
    qm_can_clock_t clock;
} qm_can_config_t;

/**
 * @brief   Structure for acceptance filter configuration of the TWAI driver (see documentation)
 *
 * @note    Macro initializers are available for this structure
 */
typedef struct {
    uint32_t acceptance_code;       /**< 32-bit acceptance code */
    uint32_t acceptance_mask;       /**< 32-bit acceptance mask */
} qm_can_filter_config_t;

/* Define wdg dev handle */
typedef struct {
    uint8_t       port;   /**< can port */
    qm_can_config_t can_config;
    qm_can_filter_config_t filter_config;
    void         *priv;   /**< priv data */
} qm_can_dev_t;

typedef struct {
    union {
        struct {
            //The order of these bits must match deprecated message flags for compatibility reasons
            uint32_t extd: 1;           /**< Extended Frame Format (29bit ID) */
            uint32_t rtr: 1;            /**< Message is a Remote Frame */
            uint32_t ss: 1;             /**< Transmit as a Single Shot Transmission. Unused for received. */
            uint32_t self: 1;           /**< Transmit as a Self Reception Request. Unused for received. */
            uint32_t reserved: 28;      /**< Reserved bits */
        };
        //Todo: Deprecate flags
        uint32_t flags;                 /**< Deprecated: Alternate way to set bits using message flags */
    };
    uint32_t identifier;                /**< 11 or 29 bit identifier */
    uint8_t data_length;                /**< Data length code */
    uint8_t data[QM_CAN_FRAME_MAX_DLC];    /**< Data bytes (not relevant in RTR frame) */
}qm_can_message_t;

/**
 * @brief Describes an event passing to the input of a callbacks
 */
typedef enum {
    QM_CAN_CTRL_CMD_NONE,
    QM_CAN_CTRL_CMD_RECEIVE_CLEAR,
    QM_UART_CTRL_CMD_TRANSMIT_CLEAR,
    QM_CAN_CTRL_CMD_RECEIVE_CALLBACK,
    QM_CAN_CTRL_CMD_TRANSMIT_CALLBACK,
    QM_CAN_CTRL_CMD_MAX
}qm_can_ctrl_cmd_t;

/**
 * @brief CAN callback type
 */
typedef void(*qm_can_receive_callback_t)(qm_can_dev_t *can_dev);

typedef void(*qm_can_transmit_callback_t)(qm_can_dev_t *can_dev, void *param);

/**
 * 
 *
 * @param[in]  
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_can_init(qm_can_dev_t *can_dev);

/**
 * 
 *
 * @param[in]  
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_can_ctrl(qm_can_dev_t *can_dev, qm_can_ctrl_cmd_t cmd, void *param);

/**
 * .
 *
 * @param[in]  
 */
int32_t qm_can_write(qm_can_dev_t *can_dev, const qm_can_message_t *message, int timeout);

/**
 * .
 *
 * @param[in]  
 */
int32_t qm_can_read(qm_can_dev_t *can_dev, qm_can_message_t *message, int timeout);

/**
 * 
 *
 * @param[in]  
 *
 * @return  
 */
int32_t qm_can_deinit(qm_can_dev_t *can_dev);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_CAN_H */
