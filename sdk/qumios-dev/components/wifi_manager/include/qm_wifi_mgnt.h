#ifndef _QM_WIFI_MGNT_H_
#define _QM_WIFI_MGNT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "qm_wifi_dev.h"

#ifndef CONFIG_QM_WORK_SUPPORT
#error "need open qm work component"
#endif

#include "qm_event.h"

/** Wi-Fi event base used by event-loop APIs. */
QM_EVENT_DECLARE_BASE(QM_EVENT_WIFI);

/**
  * @brief  Initialize WiFi
  *         Allocate resource for WiFi driver, such as WiFi control structure, RX/TX buffer,
  *         WiFi kv structure etc. This WiFi need starts qm_work task
  *
  * @attention 1. This API must be called before all other WiFi API can be called
  * @attention 2. Always use qm_event_handler_register to initialize the QM_EVENT_WIFI event
  *
  * @param  config pointer to WiFi initialized configuration structure; can point to a temporary variable.
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_init(void);

/**
  * @brief  Deinit WiFi
  *         Free all resource allocated in esp_wifi_init and stop WiFi task
  *
  * @attention 1. This API should be called if you want to remove WiFi driver from the system
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_deinit(void);

/**
  * @brief     Set the WiFi operating mode
  *
  *            Set the WiFi operating mode as station, soft-AP, station+soft-AP or NAN.
  *            The default mode is station mode.
  *
  * @param     mode  WiFi operating mode
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_set_mode(qm_wifi_mode_t mode);

/**
  * @brief  Get current operating mode of WiFi
  *
  * @param[out]  mode  store current WiFi mode
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_get_mode(qm_wifi_mode_t *mode);

/**
  * @brief     Set the configuration of the STA, AP or NAN
  *
  * @attention 1. This API can be called only when specified interface is enabled, otherwise, API fail
  * @attention 2. For station configuration, bssid_set needs to be 0; and it needs to be 1 only when users need to check the MAC address of the AP.
  * @attention 3. devices are limited to only one channel, so when in the soft-AP+station mode, the soft-AP will adjust its channel automatically to be the same as
  *               the channel of the station.
  * @attention 4. The configuration will be stored in NVS for station and soft-AP
  *
  * @param     interface  interface
  * @param     conf  station, soft-AP or NAN configuration
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_set_config(qm_wifi_interface_t ifx, qm_wifi_config_t *config);

/**
  * @brief     Get configuration of specified interface
  *
  * @param     interface  interface
  * @param[out]  conf  station or soft-AP configuration
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_get_config(qm_wifi_interface_t ifx, qm_wifi_config_t *config);

/**
  * @brief     Clear persisted WiFi configuration.
  *
  * @details This clears the in-memory WiFi configuration cache and removes the
  *          persisted `wifi_cfg` entry, so the next boot will not reuse the
  *          previous station/AP credentials automatically.
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_clear_config(void);

/**
  * @brief  Start WiFi according to current configuration
  *         If mode is QM_WIFI_MODE_STA, it creates station control block and starts station
  *         If mode is QM_WIFI_MODE_AP, it creates soft-AP control block and starts soft-AP
  *         If mode is QM_WIFI_MODE_APSTA, it creates soft-AP and station control block and starts soft-AP and station
  *         If mode is QM_WIFI_MODE_NAN, it creates NAN control block and starts NAN
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_start(void);

/**
  * @brief  Stop WiFi
  *         If mode is QM_WIFI_MODE_STA, it stops station and frees station control block
  *         If mode is QM_WIFI_MODE_AP, it stops soft-AP and frees soft-AP control block
  *         If mode is QM_WIFI_MODE_APSTA, it stops station/soft-AP and frees station/soft-AP control block
  *         If mode is QM_WIFI_MODE_NAN, it stops NAN and frees NAN control block
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_stop(void);

/**
  * @brief     Connect WiFi station to the AP.
  *
  * @attention 1. This API only impact QM_WIFI_MODE_STA or WIFI_MODE_APSTA mode
  * @attention 2. If station interface is connected to an AP, call qm_wifi_disconnect to disconnect.
  * @attention 3. The scanning triggered by qm_wifi_scan_start() will not be effective until connection between device and the AP is established.
  *               If device is scanning and connecting at the same time, it will abort scanning and return a warning message.
  * 
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_connect(void);

/**
  * @brief     Disconnect WiFi station from the AP.
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_disconnect(void);

/**
  * @brief     deauthenticate all stations or associated id equals to mac
  *
  * @param     mac  when mac is 0, deauthenticate all stations, otherwise deauthenticate station whose associated id is aid
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_deauth(const uint8_t mac[6]);

/**
  * @brief     Scan all available APs.
  *
  * @attention The scan results are owned by the WiFi implementation. The
  *            result-get API copies records into the caller-provided buffer;
  *            callers must not free the returned records or retain event
  *            pointers after the next scan.
  * @attention The values of maximum active scan time and passive scan time per channel are limited to 1500 milliseconds.
  *            Values above 1500ms may cause station to disconnect from AP and are not recommended.
  *
  * @param     config  configuration settings for scanning, if set to NULL default settings will be used
  *                    of which default values are show_hidden:false, scan_type:active, scan_time.active.min:0,
  *                    scan_time.active.max:120 milliseconds, scan_time.passive:360 milliseconds
  *                    home_chan_dwell_time:30ms
  *
  * @param     block if block is true, this API will block the caller until the scan is done, otherwise
  *                         it will return immediately
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_scan_start(qm_scan_config_t *config);

/**
  * @brief     Get AP list found in last scan.
  *
  * @attention  This API copies the last scan result into ap_records. The
  *             caller owns only the provided output buffer; no driver memory
  *             is returned or needs to be freed by the caller.
  *
  * @param[inout]  number As input param, it stores max AP number ap_records can hold.
  *                As output param, it receives the actual AP number this API returns.
  * @param         ap_records  qm_wifi_ap_record_t array to hold the found APs
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_scan_get_ap_records(uint16_t *number, qm_wifi_ap_record_t *ap_records);

/**
  * @brief     Stop the scan in process
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_scan_stop(void);

/**
  * @brief     Set MAC address of WiFi station, soft-AP or NAN interface.
  *
  * @attention 1. This API can only be called when the interface is disabled
  * @attention 2. Above mentioned interfaces have different MAC addresses, do not set them to be the same.
  * @attention 3. The bit 0 of the first byte of MAC address can not be 1. For example, the MAC address
  *      can set to be "1a:XX:XX:XX:XX:XX", but can not be "15:XX:XX:XX:XX:XX".
  *
  * @param     ifx  interface
  * @param     mac  the MAC address
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_set_mac(qm_wifi_interface_t ifx, const uint8_t mac[6]);

/**
  * @brief     Get mac of specified interface
  *
  * @param      ifx  interface
  * @param[out] mac  store mac of the interface ifx
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_get_mac(qm_wifi_interface_t ifx, uint8_t mac[6]);

/**
  * @brief      Get the rssi information of AP to which the device is associated with
  *
  * @attention 1. This API should be called after station connected to AP.
  * @attention 2. Use this API only in QM_WIFI_MODE_STA or WIFI_MODE_APSTA mode.
  *
  * @param      rssi store the rssi info received from last beacon.
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_sta_get_rssi(int8_t *rssi);

/**
  * @brief     Get information of AP to which the device is associated with
  *
  * @attention When the obtained country information is empty, it means that the AP does not carry country information
  *
  * @param     ap_info  the wifi_ap_record_t to hold AP information
  *            sta can get the connected ap's phy mode info through the struct member
  *            phy_11b，phy_11g，phy_11n，phy_lr in the wifi_ap_record_t struct.
  *            For example, phy_11b = 1 imply that ap support 802.11b mode
  *
  * @return  0 on success, otherwise failure.
  */
int qm_wifi_sta_get_ap_info(qm_wifi_ap_record_t *ap_info);

#ifdef __cplusplus
}
#endif

#endif /* QM_WIFI_MGNT_H */
