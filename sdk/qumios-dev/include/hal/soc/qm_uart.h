#ifndef QM_UART_H
#define QM_UART_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_uart UART
 *  qm uart API.
 *
 *  @{
 */

#include "qm_types.h"
#include "qm_kernel.h"


typedef enum {
    QM_UART_BAUDRATE_300        = 300,
    QM_UART_BAUDRATE_600        = 600,
    QM_UART_BAUDRATE_1200       = 1200,
    QM_UART_BAUDRATE_2400       = 2400,
    QM_UART_BAUDRATE_4800       = 4800,
    QM_UART_BAUDRATE_9600       = 9600,
    QM_UART_BAUDRATE_19200      = 19200,
    QM_UART_BAUDRATE_38400      = 38400,
    QM_UART_BAUDRATE_57600      = 57600,
    QM_UART_BAUDRATE_74880      = 74880,
    QM_UART_BAUDRATE_115200     = 115200,
    QM_UART_BAUDRATE_230400     = 230400,
    QM_UART_BAUDRATE_460800     = 460800,
    QM_UART_BAUDRATE_921600     = 921600,
    QM_UART_BAUDRATE_1500000    = 1500000,
    QM_UART_BAUDRATE_1843200    = 1843200,
    QM_UART_BAUDRATE_3686400    = 3686400,
} qm_uart_baudrate_t;

/**
 * @brief UART mode selection
 */
typedef enum {
    QM_UART_MODE_UART = 0x00,                    /*!< mode: regular UART mode*/
    QM_UART_MODE_RS485_HALF_DUPLEX = 0x01,       /*!< mode: half duplex RS485 UART mode control by RTS pin */
} qm_uart_mode_t;

/**
 * @brief UART word length constants
 */
typedef enum {
    QM_UART_DATA_5_BITS = 5,    /*!< word length: 5bits*/
    QM_UART_DATA_6_BITS,    /*!< word length: 6bits*/
    QM_UART_DATA_7_BITS,    /*!< word length: 7bits*/
    QM_UART_DATA_8_BITS,    /*!< word length: 8bits*/
    QM_UART_DATA_MAX_BITS,
} qm_uart_data_length_t;

/**
 * @brief UART stop bits number
 */
typedef enum {
    QM_UART_STOP_BITS_1   = 0x1,  /*!< stop bit: 1bit*/
    QM_UART_STOP_BITS_2   = 0x2,  /*!< stop bit: 2bits*/
    QM_UART_STOP_BITS_1_5 = 0x3,  /*!< stop bit: 1.5bits*/
    QM_UART_STOP_BITS_MAX = 0x4,
} qm_uart_stop_bits_t;

/**
 * @brief UART parity constants
 */
typedef enum {
    QM_UART_PARITY_NONE = 0x0,     /*!< Disable UART parity*/
    QM_UART_PARITY_EVEN = 0x1,     /*!< Enable UART even parity*/
    QM_UART_PARITY_ODD  = 0x2      /*!< Enable UART odd parity*/
} qm_uart_parity_t;

/**
 * @brief UART hardware flow control modes
 */
typedef enum {
    QM_UART_FLOW_CTRL_NONE    = 0x0,   /*!< disable hardware flow control*/
    QM_UART_FLOW_CTRL_RTS     = 0x1,   /*!< enable RX hardware flow control (rts)*/
    QM_UART_FLOW_CTRL_CTS     = 0x2,   /*!< enable TX hardware flow control (cts)*/
    QM_UART_FLOW_CTRL_CTS_RTS = 0x3,   /*!< enable hardware flow control*/
    QM_UART_FLOW_CTRL_MAX     = 0x4,
} qm_uart_flow_ctrl_t;

/**
 * @brief UART pins
 */
typedef struct {
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint8_t rtx_pin;
    uint8_t ctx_pin;
} qm_uart_pins_t;

/**
 * @brief UART configuration parameters for uart_param_config function
 */
typedef struct {
    uint32_t baud_rate;                    /*!< UART baud rate*/
    qm_uart_data_length_t data_bits;       /*!< UART byte size*/
    qm_uart_parity_t parity;               /*!< UART parity mode*/
    qm_uart_stop_bits_t stop_bits;         /*!< UART stop bits*/
    qm_uart_flow_ctrl_t flow_ctrl;         /*!< UART HW flow control mode (cts/rts)*/
    qm_uart_pins_t pins;                   /*!< UART pins*/
    qm_uart_mode_t mode;                   /*!< UART mode*/
} qm_uart_config_t;

/*
 * UART dev handle
 */
typedef struct {
    uint8_t        port;         /**< uart port */
    qm_uart_config_t  config;    /**< uart config */
    void          *priv;         /**< priv data */
} qm_uart_dev_t;


/**
 * @brief UART event types used in the ring buffer
 */
typedef enum {
    QM_UART_DATA,              /*!< UART data event*/
    QM_UART_BREAK,             /*!< UART break event*/
    QM_UART_BUFFER_FULL,       /*!< UART RX buffer full event*/
    QM_UART_FIFO_OVF,          /*!< UART FIFO overflow event*/
    QM_UART_FRAME_ERR,         /*!< UART RX frame error event*/
    QM_UART_PARITY_ERR,        /*!< UART RX parity event*/
    QM_UART_DATA_BREAK,        /*!< UART TX data and break event*/
    QM_UART_PATTERN_DET,       /*!< UART pattern detected */
    QM_UART_EVENT_MAX,         /*!< UART event max index*/
} qm_uart_event_type_t;

typedef enum {
    QM_UART_CTRL_CMD_NOEN,
    QM_UART_CTRL_CMD_RX_CLEAR,
    QM_UART_CTRL_CMD_READ_CALLBACK,
    QM_UART_CTRL_CMD_WRITE_CALLBACK,
    QM_UART_CTRL_CMD_MAX
}qm_uart_ctrl_cmd_t;

typedef void (*qm_uart_read_callback_t)(qm_uart_dev_t *uart, uint32_t size);
typedef void (*qm_uart_write_callback_t)(qm_uart_dev_t *uart, uint32_t size);

/**
 * @brief Set UART configuration parameters.
 *
 * @param[in] uart    UART parameter settings which should be initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_uart_init(qm_uart_dev_t *uart);

/**
 * @brief uart control
 * 
 * @param uart      the UART interface
 * @param cmd       the cmd of uart control
 * @param param     the argument of the callback.
 * @return 0 : on success,  otherwise is error
 */
int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param);

/**
 * Transmit data on a UART interface
 *
 * @param[in]  uart     the UART interface
 * @param[in]  data     pointer to the start of data
 * @param[in]  size     number of bytes to transmit
 * @param[in]  timeout  timeout in milisecond, set this value to QM_WAIT_FOREVER
 *                      if you want to wait forever
 *
 * @return
 *      - (-1) Parameter error
 *     - OTHERS (>=0) The number of bytes pushed to the TX FIFO
 */
int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout);


/**
 * Receive data on a UART interface
 *
 * @param[in]   uart         the UART interface
 * @param[out]  data         pointer to the buffer which will store incoming data
 * @param[in]   expect_size  number of bytes to receive
 * @param[in]   timeout      timeout in milisecond, set this value to QM_WAIT_FOREVER
 *                           if you want to wait forever
 *
 * @return
 *     - (-1) Error
 *     - OTHERS (>=0) The number of bytes read from UART FIFO
 */
int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout);

/**
 * Deinitialises a UART interface
 *
 * @param[in]  uart  the interface which should be deinitialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_uart_deinit(qm_uart_dev_t *uart);

/** @} */

#ifdef __cplusplus
}
#endif


#endif /* QM_UART_H */
