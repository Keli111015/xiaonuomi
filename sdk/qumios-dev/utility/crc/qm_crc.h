#ifndef __QM_CRC_H__
#define __QM_CRC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

uint8_t qm_crc4_itu(uint8_t *data, uint16_t length);
uint8_t qm_crc5_epc(uint8_t *data, uint16_t length);
uint8_t qm_crc5_itu(uint8_t *data, uint16_t length);
uint8_t qm_crc5_usb(uint8_t *data, uint16_t length);
uint8_t qm_crc6_itu(uint8_t *data, uint16_t length);
uint8_t qm_crc7_mmc(uint8_t *data, uint16_t length);
uint8_t qm_crc8(uint8_t *data, uint16_t length);
uint8_t qm_crc8_itu(uint8_t *data, uint16_t length);
uint8_t qm_crc8_rohc(uint8_t *data, uint16_t length);
uint8_t qm_crc8_maxim(uint8_t *data, uint16_t length);
uint16_t qm_crc16_ibm(uint8_t *data, uint16_t length);
uint16_t qm_crc16_maxim(uint8_t *data, uint16_t length);
uint16_t qm_crc16_usb(uint8_t *data, uint16_t length);
uint16_t qm_crc16_modbus(uint8_t *data, uint16_t length);
uint16_t qm_crc16_ccitt(uint8_t *data, uint16_t length);
uint16_t qm_crc16_ccitt_false(uint8_t *data, uint16_t length);

/**
 * Continue a CRC-16/CCITT-FALSE calculation across one or more data chunks.
 * Use 0xFFFF as crc for the first chunk.
 */
uint16_t qm_crc16_ccitt_false_update(uint16_t crc, const uint8_t *data, uint32_t length);
uint16_t qm_crc16_x25(uint8_t *data, uint16_t length);
uint16_t qm_crc16_xmodem(uint8_t *data, uint16_t length);
uint16_t qm_crc16_dnp(uint8_t *data, uint16_t length);
uint32_t qm_ieee_crc32(uint8_t *data, uint16_t length);
uint32_t qm_crc32_mpeg_2(uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif // __QM_CRC_H__
