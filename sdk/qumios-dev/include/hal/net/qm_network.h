#ifndef _QM_NETWORK_H_
#define _QM_NETWORK_H_

#ifdef __cplusplus
extern "C" {
#endif



#define QM_NETWORK_ADDR_LEN        (16)        /* IP网络地址的长度 */

typedef struct
{
    char addr[QM_NETWORK_ADDR_LEN];  /* 目标UDP主机地址, 点分十进制IP地址 */
    uint16_t port; /* 目标UDP端口, 范围是0-65535 */
}qm_netaddr_t;

typedef struct{
    uint32_t addr; 
}qm_ip4_addr_t;

#define QM_IPSTR "%d.%d.%d.%d"
#define qm_ip4_addr_get_byte(ipaddr, idx) (((const uint8_t*)(&(ipaddr)->addr))[idx])
#define qm_ip4_addr1(ipaddr) qm_ip4_addr_get_byte(ipaddr, 0)
#define qm_ip4_addr2(ipaddr) qm_ip4_addr_get_byte(ipaddr, 1)
#define qm_ip4_addr3(ipaddr) qm_ip4_addr_get_byte(ipaddr, 2)
#define qm_ip4_addr4(ipaddr) qm_ip4_addr_get_byte(ipaddr, 3)


#define qm_ip4_addr1_16(ipaddr) ((uint16_t)qm_ip4_addr1(ipaddr))
#define qm_ip4_addr2_16(ipaddr) ((uint16_t)qm_ip4_addr2(ipaddr))
#define qm_ip4_addr3_16(ipaddr) ((uint16_t)qm_ip4_addr3(ipaddr))
#define qm_ip4_addr4_16(ipaddr) ((uint16_t)qm_ip4_addr4(ipaddr))

#define QM_IP2STR(ipaddr) qm_ip4_addr1_16(ipaddr), \
    qm_ip4_addr2_16(ipaddr), \
    qm_ip4_addr3_16(ipaddr), \
    qm_ip4_addr4_16(ipaddr)



#ifdef __cplusplus
}
#endif
#endif 