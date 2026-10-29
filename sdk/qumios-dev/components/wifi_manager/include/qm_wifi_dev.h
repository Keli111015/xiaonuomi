#ifndef _QM_WIFI_DEV_H_
#define _QM_WIFI_DEV_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_errno.h"
#include "qm_network.h"

#ifndef CONFIG_QM_WIFI_SSID_MAX_LEN 
#define CONFIG_QM_WIFI_SSID_MAX_LEN  (32)
#endif

#ifndef CONFIG_QM_WIFI_PASSWD_MAX_LEN 
#define CONFIG_QM_WIFI_PASSWD_MAX_LEN  (64)
#endif


#define QM_ERR_WIFI_NOT_INIT    (QM_ERR_WIFI_BASE + 1)   /*!< WiFi driver was not installed by qm_wifi_init */
#define QM_ERR_WIFI_NOT_STARTED (QM_ERR_WIFI_BASE + 2)   /*!< WiFi driver was not started by qm_wifi_start */
#define QM_ERR_WIFI_NOT_STOPPED (QM_ERR_WIFI_BASE + 3)   /*!< WiFi driver was not stopped by qm_wifi_stop */
#define QM_ERR_WIFI_IF          (QM_ERR_WIFI_BASE + 4)   /*!< WiFi interface error */
#define QM_ERR_WIFI_MODE        (QM_ERR_WIFI_BASE + 5)   /*!< WiFi mode error */
#define QM_ERR_WIFI_MAC         (QM_ERR_WIFI_BASE + 6)   /*!< MAC address is invalid */
#define QM_ERR_WIFI_SSID        (QM_ERR_WIFI_BASE + 7)   /*!< SSID is invalid */
#define QM_ERR_WIFI_PASSWORD    (QM_ERR_WIFI_BASE + 8)  /*!< Password is invalid */

typedef enum {

    QM_WIFI_REASON_UNKNOWN                  = 0,
    QM_WIFI_REASON_NO_AP_FOUND              = 1,
    QM_WIFI_REASON_CONNECTION_FAIL          = 2,
    QM_WIFI_REASON_ASSOC_TIMEOUT            = 3,
    QM_WIFI_REASON_ASSOC_FAIL               = 4,
    QM_WIFI_REASON_AUTH_FAIL                = 5,
    QM_WIFI_REASON_DHCP_TIMEOUT             = 6,
} qm_wifi_reason_t;

typedef enum
{
    QM_WIFI_MODE_NONE,       /**< null mode */
    QM_WIFI_MODE_STA,		 /**< WiFi station mode */
    QM_WIFI_MODE_AP,	     /**< WiFi soft-AP mode */
    QM_WIFI_MODE_APSTA,      /**< WiFi station + soft-AP mode */
    QM_WIFI_MODE_MAX
} qm_wifi_mode_t;

typedef enum {
    QM_WIFI_IF_STA,
    QM_WIFI_IF_AP
} qm_wifi_interface_t;

typedef enum {
    QM_WIFI_PS_NONE,        /**< No power save */
    QM_WIFI_PS_MIN_MODEM,   /**< Minimum modem power saving. In this mode, station wakes up to receive beacon every DTIM period */
    QM_WIFI_PS_MAX_MODEM    /**< Maximum modem power saving. In this mode, interval to receive beacons is determined by the listen_interval parameter in qm_wifi_sta_config_t */
} qm_wifi_ps_type_t;

typedef enum
{
    QM_WIFI_EVENT_READY = 0,			    /**< wifi ready */
    QM_WIFI_EVENT_SCAN_DONE,				/**< wifi finish scanning AP */
    QM_WIFI_EVENT_STA_START,                /*!< wifi station start */
    QM_WIFI_EVENT_STA_STOP,                 /*!< wifi station stop */
    QM_WIFI_EVENT_STA_CONNECTED,            /**< wifi station connected to AP */
    QM_WIFI_EVENT_STA_GOT_IP,               /*!< station got IP from connected AP */
    QM_WIFI_EVENT_STA_LOST_IP,              /*!< station lost IP and the IP is reset to 0 */
    QM_WIFI_EVENT_STA_DISCONNECTED,         /**< wifi station disconnected from AP */
    QM_WIFI_EVENT_AP_START,					/**< soft-AP start */
    QM_WIFI_EVENT_AP_STOP,				    /**< soft-AP stop */
    QM_WIFI_EVENT_AP_STACONNECTED,          /**< a station connected to soft-AP */
    QM_WIFI_EVENT_AP_STAIPASSIGNED,         /*!< soft-AP assign an IP to a connected station */
    QM_WIFI_EVENT_AP_STADISCONNECTED,       /**< a station disconnected from soft-AP */
    QM_WIFI_EVENT_MAX,                      /**< Invalid WiFi event ID */
} qm_wifi_event_t;

/**
 * Enumeration of Wi-Fi auth modes
 */
typedef enum {
    QM_WIFI_AUTH_OPEN = 0,         /**< authenticate mode : open */
    QM_WIFI_AUTH_WEP,              /**< authenticate mode : WEP */
    QM_WIFI_AUTH_WPA_PSK,          /**< authenticate mode : WPA_PSK */
    QM_WIFI_AUTH_WPA2_PSK,         /**< authenticate mode : WPA2_PSK */
    QM_WIFI_AUTH_WPA_WPA2_PSK,     /**< authenticate mode : WPA_WPA2_PSK */
    QM_WIFI_AUTH_WPA2_ENTERPRISE,  /**< authenticate mode : WPA2_ENTERPRISE */
    QM_WIFI_AUTH_WPA3_PSK,         /**< authenticate mode : WPA3_PSK */
    QM_WIFI_AUTH_WPA2_WPA3_PSK,    /**< authenticate mode : WPA2_WPA3_PSK */
    QM_WIFI_AUTH_WAPI_PSK,         /**< authenticate mode : WAPI_PSK */
    QM_WIFI_AUTH_MAX
} qm_wifi_auth_mode_t;

typedef struct qm_sta_info
{
    uint8_t ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];             /**< SSID of target AP. */
    uint8_t ssid_len;
    uint8_t password[CONFIG_QM_WIFI_PASSWD_MAX_LEN + 1];       /**< Password of target AP. */
    uint8_t password_len;
    uint8_t bssid[6];                                      /**< MAC address of target AP*/
    uint8_t channel;
} qm_sta_config_t;

typedef struct qm_ap_info
{
    uint8_t ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];             /**< SSID of target AP. */
    uint8_t ssid_len;
    uint8_t password[CONFIG_QM_WIFI_PASSWD_MAX_LEN + 1];       /**< Password of target AP. */
    uint8_t password_len;
    uint8_t channel;                                       /**< Channel of soft-AP */
    uint8_t ssid_hidden;                                   /**< Broadcast SSID or not, default 0, broadcast the SSID */
    uint8_t max_connection;                                /**< Max number of stations allowed to connect in, default 4, max 10 */
    qm_wifi_auth_mode_t authmode;
} qm_ap_config_t;

typedef struct {
    qm_ap_config_t  ap;  /**< configuration of AP */
    qm_sta_config_t sta; /**< configuration of STA */
} qm_wifi_config_t;

typedef enum {
    QM_WIFI_SCAN_TYPE_ACTIVE = 0,  /**< active scan */
    QM_WIFI_SCAN_TYPE_PASSIVE,     /**< passive scan */
} qm_wifi_scan_type_t;

typedef struct qm_scan_info
{
    uint8_t ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];         /**< SSID of target AP. */
    uint8_t ssid_len;
    uint8_t bssid[6];	   			                    /**< MAC address of target AP*/
    uint8_t channel;       			                    /**< channel, scan the specific channel */
    uint8_t ssid_hidden;   			                    /**< enable to scan AP whose SSID is hidden */
    qm_wifi_scan_type_t scan_type;                      /**< scan type, active or passive */
} qm_scan_config_t;

typedef struct
{
    uint8_t bssid[6];                                  /**< MAC address of AP */
    uint8_t ssid[CONFIG_QM_WIFI_SSID_MAX_LEN + 1];         /**< SSID of AP */
    uint8_t ssid_len;
    int8_t  rssi;                                      /**< signal strength of AP */
    uint8_t channel;                                   /* The RF frequency, 1-13 */
    qm_wifi_auth_mode_t authmode;
} qm_wifi_ap_record_t;

typedef struct{
    qm_ip4_addr_t ip;      /**< IPV4 address */
    qm_ip4_addr_t netmask; /**< IPV4 netmask */
    qm_ip4_addr_t gw;      /**< IPV4 gateway address */
}qm_ip_info_t;

typedef struct{
    qm_ip_info_t ip_info; 
}qm_wifi_event_sta_got_ip_t;

typedef struct {          
    uint8_t ap_num; 			     
    qm_wifi_ap_record_t *ap_record;
} qm_wifi_scan_result_t;

typedef struct {
    int status;                         /**< status of scanning APs: 0 — success, 1 - failure */
    uint8_t ap_num; 			        /* The number of access points found in scanning. */
    qm_wifi_ap_record_t *ap_record;
} qm_wifi_event_scan_done_t;

typedef struct{
    uint8_t reason;           /**< reason of disconnection */
}qm_wifi_event_sta_disconnected_t;
    
typedef struct{
    uint8_t mac[6];             /**< MAC address of the station connected to ESP32 soft-AP */
}qm_wifi_event_ap_staconnected_t;

typedef struct{
    uint8_t mac[6];             /**< MAC address of the station disconnects to soft-AP */
}qm_wifi_event_ap_stadisconnected_t;

typedef struct{
    qm_ip4_addr_t ip;      /*!< IP address which was assigned to the station */
}qm_wifi_event_ap_staipassigned_t;

typedef union{
    qm_wifi_event_sta_disconnected_t sta_disconnected;
    qm_wifi_event_sta_got_ip_t got_ip;
    qm_wifi_event_scan_done_t scan_done;
    qm_wifi_event_ap_staconnected_t ap_staconnected;
    qm_wifi_event_ap_stadisconnected_t ap_stadisconnected;
    qm_wifi_event_ap_staipassigned_t  ap_staipassigned;
}qm_wifi_event_info_t;

/*
 * The event call back function called at specific events occurred.
 */
typedef void (*qm_wifi_event_handler_t)(qm_wifi_event_t event, qm_wifi_event_info_t *event_info, void *arg);

typedef struct qm_wifi_dev qm_wifi_dev_t;

typedef struct qm_wifi_dev_ops {

    qm_err_t (*wifi_init)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_deinit)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_start)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_stop)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_connect)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_disconnect)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_scan_start)(qm_wifi_dev_t *wifi, qm_scan_config_t *config);
    qm_err_t (*wifi_scan_stop)(qm_wifi_dev_t *wifi);
    qm_err_t (*wifi_scan_get_ap_records)(qm_wifi_dev_t *wifi,
                                         uint16_t *number,
                                         qm_wifi_ap_record_t *ap_records);
    qm_err_t (*wifi_deauth)(qm_wifi_dev_t *wifi, const uint8_t mac[6]);
    qm_err_t (*wifi_set_ps)(qm_wifi_dev_t *wifi, qm_wifi_ps_type_t type);
    qm_err_t (*wifi_set_mac)(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6]);
    qm_err_t (*wifi_get_mac)(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6]);
    qm_err_t (*wifi_get_sta_rssi)(qm_wifi_dev_t *wifi, int8_t *rssi);
    qm_err_t (*wifi_get_sta_ap_info)(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info);
}qm_wifi_dev_ops_t;

typedef struct {
    qm_wifi_mode_t mode;
    qm_wifi_config_t config;
}qm_wifi_cfg_t;

struct qm_wifi_dev
{
    qm_wifi_cfg_t wifi_cfg;
    qm_wifi_dev_ops_t *ops;
    qm_wifi_event_handler_t event_handler;
    void *arg;
    void *priv;   			/**< priv data */
};

/**
 * Initialize wifi instances.
 *
 * @note  This is supposed to be called during system boot,
 *        not supposed to be called by user module directly.
 * 
 * @param[in]  wifi  the wifi device
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_init(qm_wifi_dev_t *wifi);

/**
 * Deinit WiFi
 * 
 * @param[in]  wifi  the wifi device
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_deinit(qm_wifi_dev_t *wifi);

/**
 * @brief   Set the WiFi operating mode
 *
 * @param[in]  wifi  the wifi device
 * 
 * @param[in]  mode  WiFi operating mode
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_set_mode(qm_wifi_dev_t *wifi, qm_wifi_mode_t mode);

/**
 * @brief  Get current operating mode of WiFi
 *
 * @param[in]  wifi  the wifi device
 * 
 * @param[out]  mode  store current WiFi mode
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_get_mode(qm_wifi_dev_t *wifi, qm_wifi_mode_t *mode);

/**
 * @brief     Set the configuration of STA or AP
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  ifx  interfaces
 * @param[in]  config  station or soft-AP configuration
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_set_config(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, qm_wifi_config_t *config);

/**
 * @brief   Get the configuration of STA or AP
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  ifx  interfaces
 * @param[out] config  station or soft-AP configuration
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_get_config(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, qm_wifi_config_t *config);

/**
 * Start the scan
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  config  configuration of scanning: @ref qm_scan_config_t
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config);

/**
 * Stop the scan
 *
 * @param[in]  wifi  the wifi device
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_scan_stop(qm_wifi_dev_t *wifi);

/**
 * Get the AP records cached by the last completed scan.
 *
 * The input value of number is the capacity of ap_records; on return it is
 * replaced with the number of records copied.
 */
int qm_wifi_dev_scan_get_ap_records(qm_wifi_dev_t *wifi,
                                    uint16_t *number,
                                    qm_wifi_ap_record_t *ap_records);

/**
 * WiFi station connect the AP.
 *
 * @param[in]  wifi  the wifi device
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_connect(qm_wifi_dev_t *wifi);

/**
 * WiFi station disconnect the AP.
 *
 * @param[in]  wifi  the wifi device
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_disconnect(qm_wifi_dev_t *wifi);

/**
 * WiFi ap start.
 *
 * @param[in]  wifi  the wifi device
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_start(qm_wifi_dev_t *wifi);

/**
 * WiFi ap stop.
 *
 * @param[in]  wifi  the wifi device
 * 
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_stop(qm_wifi_dev_t *wifi);

/**
 * deauthenticate station by mac address
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  mac  the MAC address
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6]);

/**
 * Set MAC address
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  ifx  interfaces
 * @param[in]  mac  the MAC address 
 *
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6]);

/**
 * Get MAC address
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  ifx  interfaces
 * @param[out]  mac  the MAC address 
 *
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6]);

/**
  * @brief      Get the rssi information of AP to which the device is associated with
  *
  * @attention 1. This API should be called after station connected to AP.
  * @attention 2. Use this API only in QM_WIFI_MODE_STA or WIFI_MODE_APSTA mode.
  *
  * @param[in]    wifi  the wifi device
  * @param[inout] rssi store the rssi info received from last beacon.
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_dev_sta_get_rssi(qm_wifi_dev_t *wifi, int8_t *rssi);

/**
  * @brief      Get information of AP to which the device is associated with
  *
  * @attention 1. This API should be called after station connected to AP.
  * @attention 2. Use this API only in QM_WIFI_MODE_STA or WIFI_MODE_APSTA mode.
  *
  * @param[in]    wifi  the wifi device
  * @param[inout] ap_info  the wifi_ap_record_t to hold AP information
  *            sta can get the connected ap's phy mode info through the struct member
  *            phy_11b，phy_11g，phy_11n，phy_lr in the wifi_ap_record_t struct.
  *            For example, phy_11b = 1 imply that ap support 802.11b mode
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_dev_sta_get_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info);

/**
 * register wifi event handler
 *
 * @param[in]  wifi  the wifi device
 * @param[in]  event_handler  the wifi event handler
 * @param[in]  arg  the argument of event handler 
 *
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_register_event_handler(qm_wifi_dev_t *wifi, qm_wifi_event_handler_t event_handler, void *arg);

/**
 * register wifi ops
 *
 * @param[in]  ops  the wifi ops
 * @param[in]  priv  priv data 
 *
 * @return  0 on success, otherwise failure.
 */
int qm_wifi_dev_register(qm_wifi_dev_ops_t *ops, void *priv);

/**
 * Get wifi dev
 *
 * @return  wifi dev
 */
qm_wifi_dev_t *qm_wifi_dev_get(void);


#ifdef __cplusplus
}
#endif


#endif /* QM_WIFI_DEV_H */
