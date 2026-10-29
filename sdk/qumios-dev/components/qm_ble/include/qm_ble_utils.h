#ifndef QB_UTILS_H
#define QB_UTILS_H



#define QM_BLE_MIN(a, b) (a)<(b)? (a) : (b)


#define QM_BLE_SET_U16_LE(data, val) {                              \
    *(uint8_t *)(data) = (uint8_t)(val & 0xFF);              \
    *((uint8_t *)(data) + 1) = (uint8_t)((val >> 8) & 0xFF); \
}

#define QM_BLE_SET_U32_LE(data, val) {                               \
    *(uint8_t *)(data) = (uint8_t)(val & 0xFF);               \
    *((uint8_t *)(data) + 1) = (uint8_t)((val >> 8) & 0xFF);  \
    *((uint8_t *)(data) + 2) = (uint8_t)((val >> 16) & 0xFF); \
    *((uint8_t *)(data) + 3) = (uint8_t)((val >> 24) & 0xFF); \
}

#define QM_BLE_SET_U32_BE(data, val) {                               \
    *(uint8_t *)(data) =  (uint8_t)((val >> 24) & 0xFF);      \
    *((uint8_t *)(data) + 1) = (uint8_t)((val >> 16) & 0xFF); \
    *((uint8_t *)(data) + 2) = (uint8_t)((val >> 8) & 0xFF);  \
    *((uint8_t *)(data) + 3) = (uint8_t)(val & 0xFF);         \
}

#define QM_BLE_SWAP32(x)                                                    \
    ((uint32_t)(                                                            \
    (((uint32_t)(x) & (uint32_t)0x000000ffUL) << 24) |                      \
    (((uint32_t)(x) & (uint32_t)0x0000ff00UL) <<  8)  |                     \
    (((uint32_t)(x) & (uint32_t)0x00ff0000UL) >>  8)  |                     \
    (((uint32_t)(x) & (uint32_t)0xff000000UL) >> 24) ))
    

uint8_t qm_ble_hex2ascii(uint8_t digit);
void qm_ble_hex2string(uint8_t *hex, uint32_t len, uint8_t *str);

void qm_ble_hex_byte_dump_debug(uint8_t *data, int len, int tab_num);
void qm_ble_hex_byte_dump_verbose(uint8_t *data, int len, int tab_num);

#endif  // QB_UTILS_H
