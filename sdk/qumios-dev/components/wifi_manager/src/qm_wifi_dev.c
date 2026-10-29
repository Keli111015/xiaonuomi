#include "qm_wifi_dev.h"
#include "qm_errno.h"
#include "qm_types.h"
#include "qm_kv.h"

#define WIFI_CONFIG_KEY  "wifi_cfg"

static qm_wifi_dev_t g_wifi_dev = {0};

int qm_wifi_dev_init(qm_wifi_dev_t *wifi)
{
    int len = sizeof(g_wifi_dev.wifi_cfg);

    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_init == NULL){
        return -QM_EINVAL;
    }

    memset(&wifi->wifi_cfg, 0, sizeof(wifi->wifi_cfg));
    qm_kv_get(WIFI_CONFIG_KEY, &g_wifi_dev.wifi_cfg, &len);
    if (len != sizeof(g_wifi_dev.wifi_cfg)) {
        memset(&wifi->wifi_cfg, 0, sizeof(wifi->wifi_cfg));
    }

    return wifi->ops->wifi_init(wifi);
}

int qm_wifi_dev_deinit(qm_wifi_dev_t *wifi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_deinit == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_deinit(wifi);
}

int qm_wifi_dev_set_mode(qm_wifi_dev_t *wifi, qm_wifi_mode_t mode)
{
    if(wifi == NULL || mode < QM_WIFI_MODE_NONE || mode >= QM_WIFI_MODE_MAX){
        return -QM_EINVAL;
    }
    wifi->wifi_cfg.mode = mode;
    return QM_EOK;
}

int qm_wifi_dev_get_mode(qm_wifi_dev_t *wifi, qm_wifi_mode_t *mode)
{
    if(wifi == NULL || mode == NULL){
        return -QM_EINVAL;
    }
    *mode = wifi->wifi_cfg.mode;
    return QM_EOK;
}

int qm_wifi_dev_set_config(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, qm_wifi_config_t *config)
{
    if(wifi == NULL || config == NULL){
        return -QM_EINVAL;
    }

    if(ifx == QM_WIFI_IF_STA){
        memset(&wifi->wifi_cfg.config.sta, 0, sizeof(wifi->wifi_cfg.config.sta));
        memcpy(&wifi->wifi_cfg.config.sta, &config->sta, sizeof(wifi->wifi_cfg.config.sta));
    }else if(ifx == QM_WIFI_IF_AP){
        memset(&wifi->wifi_cfg.config.ap, 0, sizeof(wifi->wifi_cfg.config.ap));
        memcpy(&wifi->wifi_cfg.config.ap, &config->ap, sizeof(wifi->wifi_cfg.config.ap));
    } else {
        return -QM_ERR_WIFI_IF;
    }

    return qm_kv_set(WIFI_CONFIG_KEY, &wifi->wifi_cfg,
                     sizeof(wifi->wifi_cfg), 1);
}

int qm_wifi_dev_get_config(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, qm_wifi_config_t *config)
{
    if(wifi == NULL || config == NULL){
        return -QM_EINVAL;
    }

    if(ifx == QM_WIFI_IF_STA){
        memcpy(&config->sta, &wifi->wifi_cfg.config.sta, sizeof(wifi->wifi_cfg.config.sta));
    }else if(ifx == QM_WIFI_IF_AP){
        memcpy(&config->ap, &wifi->wifi_cfg.config.ap, sizeof(wifi->wifi_cfg.config.ap));
    } else {
        return -QM_ERR_WIFI_IF;
    }
    return QM_EOK;
}

int qm_wifi_dev_scan_start(qm_wifi_dev_t *wifi, qm_scan_config_t *config)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_scan_start == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_scan_start(wifi, config);
}

int qm_wifi_dev_scan_stop(qm_wifi_dev_t *wifi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_scan_stop == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_scan_stop(wifi);
}

int qm_wifi_dev_scan_get_ap_records(qm_wifi_dev_t *wifi,
                                    uint16_t *number,
                                    qm_wifi_ap_record_t *ap_records)
{
    if (wifi == NULL || wifi->ops == NULL || number == NULL) {
        return -QM_EINVAL;
    }
    if (wifi->ops->wifi_scan_get_ap_records == NULL) {
        return -QM_ENOSYS;
    }
    if (*number > 0 && ap_records == NULL) {
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_scan_get_ap_records(wifi, number, ap_records);
}

int qm_wifi_dev_connect(qm_wifi_dev_t *wifi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_connect == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_connect(wifi);
}

int qm_wifi_dev_disconnect(qm_wifi_dev_t *wifi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_disconnect == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_disconnect(wifi);
}

int qm_wifi_dev_start(qm_wifi_dev_t *wifi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_start == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_start(wifi);
}

int qm_wifi_dev_stop(qm_wifi_dev_t *wifi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_stop == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_stop(wifi);
}

int qm_wifi_dev_deauth(qm_wifi_dev_t *wifi, const uint8_t mac[6])
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_deauth == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_deauth(wifi, mac);
}

int qm_wifi_dev_set_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, const uint8_t mac[6])
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_set_mac == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_set_mac(wifi, ifx, mac);
}

int qm_wifi_dev_get_mac(qm_wifi_dev_t *wifi, qm_wifi_interface_t ifx, uint8_t mac[6])
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_get_mac == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_get_mac(wifi, ifx, mac);
}

int qm_wifi_dev_sta_get_rssi(qm_wifi_dev_t *wifi, int8_t *rssi)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_get_sta_rssi == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_get_sta_rssi(wifi, rssi);
}


int qm_wifi_dev_sta_get_ap_info(qm_wifi_dev_t *wifi, qm_wifi_ap_record_t *ap_info)
{
    if(wifi == NULL || wifi->ops == NULL || wifi->ops->wifi_get_sta_ap_info == NULL){
        return -QM_EINVAL;
    }

    return wifi->ops->wifi_get_sta_ap_info(wifi, ap_info);
}

int qm_wifi_dev_register_event_handler(qm_wifi_dev_t *wifi, qm_wifi_event_handler_t event_handler, void *arg)
{
    if(wifi == NULL){
        return -QM_EINVAL;
    }
    wifi->event_handler = event_handler;
    wifi->arg = arg;
    return QM_EOK;
}

qm_wifi_dev_t *qm_wifi_dev_get(void)
{
    return &g_wifi_dev;
}

int qm_wifi_dev_register(qm_wifi_dev_ops_t *ops, void *priv)
{
    if(ops == NULL){
        return -QM_EINVAL;
    }
    g_wifi_dev.ops = ops;
    g_wifi_dev.priv = priv;
    return QM_EOK;
}
