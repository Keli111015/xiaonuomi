#ifndef QM_USB_H
#define QM_USB_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_usb USB
 *  qm usb API.
 *
 *  @{
 */

#include "qm_types.h"
#include "qm_config.h"

#if CONFIG_QM_USB_SUPPORT

typedef enum {
    QM_USB_0 = 0x00
} qm_usb_port_t;

typedef enum{
    QM_USB_CDCACM_0 = 0x0
}qm_usb_cdcacm_port_t;

/**
 * @brief Types of CDC ACM events
 */
typedef enum {
    QM_USB_CDC_EVENT_RX,
} qm_usb_cdcacm_event_type_t;


/**
 * @brief Describes an event passing to the input of a callbacks
 */
typedef struct {
    qm_usb_cdcacm_event_type_t type; /*!< Event type */
    union {
        
    };
} qm_usb_cdcacm_event_t;

/**
 * @brief CDC-ACM callback type
 */
typedef void(*qm_usb_cdcacm_callback_t)(qm_usb_cdcacm_port_t cdcacm_port, qm_usb_cdcacm_event_t *event);

/**
 * @brief Configuration structure for CDC-ACM
 */
typedef struct {
    qm_usb_port_t   usb_port;      
    qm_usb_cdcacm_port_t  cdc_port;         /*!< CDC port */
    int rx_buf_sz;                     /*!< Amount of data that can be passed to the ACM at once */
    qm_usb_cdcacm_callback_t callback_rx;   /*!< Pointer to the function with the `qm_usb_cdcacm_callback_t` type that will be handled as a callback */
} qm_usb_cdcacm_config_t;

/**
 * @brief Initialize CDC ACM.
 *
 * @param cfg - init configuration structure
 * @return qm_err_t
 */
int qm_usb_cdcacm_init(qm_usb_cdcacm_config_t *cfg);

/**
 * @brief Register a callback invoking on CDC event. If the callback had been
 *        already registered, it will be overwritten
 *
 * @param port - port of a CDC object
 * @param event_type - type of registered event for a callback
 * @param callback  - callback function
 * @return qm_err_t 
 */
int qm_usb_cdcacm_register_callback(qm_usb_cdcacm_port_t port, qm_usb_cdcacm_event_type_t event_type, qm_usb_cdcacm_callback_t callback);

/**
 * @brief Write data to write buffer from a byte array
 *
 * @param itf - port of a CDC object
 * @param in_buf - a source array
 * @param in_size - size to write from arr_src
 * @param timeout - waiting until write will be considered as failed
 * @return int - Returns the length of writing
 */
int qm_usb_cdcacm_write(qm_usb_cdcacm_port_t port, const uint8_t *in_buf, int in_size, uint32_t timeout);

/**
 * @brief Read a content to the array, and defines it's size to the sz_store
 *
 * @param itf - number of a CDC object
 * @param out_buf - to this array will be stored the object from a CDC buffer
 * @param out_buf_sz - size of buffer for results
 * @param rx_data_size - to this address will be stored the object's size
 * @return int - Returns the length of reading
 */
int qm_usb_cdcacm_read(qm_usb_cdcacm_port_t port, uint8_t *out_buf, int out_size);

/** @} */

#endif

#ifdef __cplusplus
}
#endif


#endif /* QM_USB_H */
