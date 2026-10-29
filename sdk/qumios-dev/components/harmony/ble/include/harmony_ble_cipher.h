#ifndef __HARMONY_BLE_CIPHER_H__
#define __HARMONY_BLE_CIPHER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#define HARMONY_BLE_CIPHER_IV_LEN           (12)
#define HARMONY_BLE_CIPHER_TAG_LEN          (16)
#define HARMONY_BLE_CIPHER_SALT_LEN         (16)
#define HARMONY_BLE_CIPHER_AUTHCODE_LEN     (16)
#define HARMONY_BLE_CIPHER_SESSIONKEY_LEN   (16)
#define HARMONY_BLE_CIPHER_HMACKEY_LEN      (32)
#define HARMONY_BLE_CIPHER_SESSID_LEN       (32)
#define HARMONY_BLE_CIPHER_AES128_LENGTH    (128)
typedef struct 
{
    uint8_t *session_key;
    uint8_t *iv;
    uint8_t *tag;
    uint8_t *prod_id;
    uint16_t prod_length;
    uint8_t *in_data;
    uint8_t *out_data;
    uint16_t data_len;
}harmony_ble_cipher_info_t;

int harmony_ble_cipher_decrypt_data(harmony_ble_cipher_info_t *cipher_info);
int harmony_ble_cipher_encrypt_data(harmony_ble_cipher_info_t *cipher_info);
int harmony_ble_cipher_flush(uint8_t *in_salt, uint8_t *in_authcode, uint8_t *out_session, uint8_t *out_hmac);
int harmony_ble_cipher_encrpy_hmac_flush(uint8_t *data, uint16_t len, uint8_t *hmac_key, uint8_t *hmac_value);

#ifdef __cplusplus
}
#endif

#endif