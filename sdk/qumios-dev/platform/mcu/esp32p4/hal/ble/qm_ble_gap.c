#include "qm.h"
#include "qm_ble_gap.h"
#include "esp_mac.h"

/**
 * @brief 获取当前设备使用的本地 BLE MAC 地址。
 *
 * @param[out] addr 输出地址缓冲区，长度固定为 6 字节。
 *
 * @return QM_EOK 表示成功；参数无效时返回 -QM_EINVAL。
 */
int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    int ret = QM_EOK;
    uint8_t str_mac[6] = {0};
    ret = esp_base_mac_addr_get(addr);
    if (ret != QM_EOK){
        return -QM_EINVAL;
    }
    
    addr[5] += 2;

    return QM_EOK;
}
