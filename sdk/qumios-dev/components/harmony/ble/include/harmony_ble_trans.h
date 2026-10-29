#ifndef __HARMONY_BLE_TRANS_H__
#define __HARMONY_BLE_TRANS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#include "harmony_ble_comm.h"
#include "harmony_ble_cipher.h"


// #define HARMONY_BLE_HEAD_LEN			(8)
#define HARMONY_BLE_HEAD_LEN	    (7) //需新产品验证是否为8
#define HARMONY_BLE_ENCRYPT_LEN	    (32) //IV + TAG + SESSID + HMAC

#define HARMONY_BLE_TRANS_PACKET_NUM 	(4)
#define HARMONY_BLE_TRANS_MAX_SIZE		(HARMONY_BLE_TRANS_PACKET_NUM * HARMONY_BLE_DEFAULT_MTU_SIZE)

#define HARMONY_BLE_DATA_VERSION		(0)
#define HARMONY_BLE_DATA_FORMAT			(1)
#define HARMONY_BLE_DATA_REV			(0)

#define HARMONY_BLE_PAYLOAD_DATA_FORMAT_GET(DATA)	((DATA[0] & 0xF0) >> 4)
#define HARMONY_BLE_PAYLOAD_DATA_OPT_GET(DATA)		((DATA[0] & 0x0F ))
#define HARMONY_BLE_PAYLOAD_DATA_SERVICE_LEN_GET(DATA)		(DATA[1])

typedef enum {
	HARMONY_BLE_ENCY_NONE,			//不加密,BLE需要SMP安全配对，配对建议采用Just works方式
	HARMONY_BLE_ENCY_CONFIG_SPEKE,	//配网SPEKE加密
	HARMONY_BLE_ENCY_SET_SPEKE,		//控制SPEKE加密
	HARMONY_BLE_ENCY_SESSION,		//Session Key加密	
	HARMONY_BLE_ENCY_AUTHCODE,		//基于AuthCode派生秘钥加密	
	HARMONY_BLE_ENCY_NAX,	//
}harmony_ble_ency_type_t;

typedef enum {
	HARMONY_BLE_CMD_REQUEST,	//请求消息
	HARMONY_BLE_CMD_RESPONSE,	//响应消息
	HARMONY_BLE_CMD_REPORT,		//主动上报消息	
}harmony_ble_cmd_type_t;

typedef enum {
	HARMONY_BLE_DATA_TYPE_NONE,	
	HARMONY_BLE_DATA_TYPE_JSON,	
	HARMONY_BLE_DATA_TYPE_MAX,	
}harmony_ble_data_type_t;


typedef enum {
	HARMONY_BLE_OPT_TYPE_PUT,	
	HARMONY_BLE_OPT_TYPE_GET,
	HARMONY_BLE_OPT_TYPE_PEPORT,	
	HARMONY_BLE_OPT_TYPE_MAX,	
}harmony_ble_opt_type_t;

typedef struct {
	uint8_t cmd_type: 4;	// 1 & 2
	uint8_t version : 4;
	uint8_t msg_id;
	uint8_t frame_total;
	uint8_t frame_seq;
	uint8_t rev;
	uint8_t encry;
	uint8_t ret;
}harmony_ble_frame_header_t;

typedef struct {
	char *service_name;	// 
	uint8_t service_length;	// 
	char  *body_data;   // 
	uint16_t body_length;	//
	uint8_t	 encrpyt_hmac[32]; //HMAC KEY
}harmony_ble_json_data_t;

typedef struct 
{
	uint8_t msg_id;
	uint8_t encrypted;
	harmony_ble_opt_type_t opt;
	harmony_ble_cmd_type_t cmd;
	harmony_ble_data_type_t data_type;	
	union 
	{
		harmony_ble_json_data_t json_data;
	}data;
	void *arg;
}harmony_ble_trans_info_t;


typedef void (*harmony_ble_trans_rx_cb)(harmony_ble_trans_info_t *rx_info);


int harmony_ble_trans_deinit(void);

int harmony_ble_trans_init(harmony_ble_trans_rx_cb rx_info);

int harmony_ble_trans_flush(void);

int harmony_ble_transport_rx(uint8_t *p_data, uint16_t length);

int harmony_ble_transport_tx(harmony_ble_trans_info_t *tx_info);

int harmony_ble_transport_encrypt_tx(harmony_ble_trans_info_t *tx_info, 
									char *prod_id, int id_len,
                                    uint8_t iv[HARMONY_BLE_CIPHER_IV_LEN],
									uint8_t sesskey[HARMONY_BLE_CIPHER_SESSID_LEN],
                                    uint8_t sessid[HARMONY_BLE_CIPHER_SESSID_LEN], 
                                    uint8_t hmac_key[HARMONY_BLE_CIPHER_HMACKEY_LEN]);

#ifdef __cplusplus
}
#endif

#endif