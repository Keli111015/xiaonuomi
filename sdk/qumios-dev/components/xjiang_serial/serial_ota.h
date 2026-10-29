#ifndef _SERIAL_OTA_H_
#define _SERIAL_OTA_H_

#include "serial_common.h"
#include "qm_utils_timer.h"

#define SERIAL_OTA_VERIFY_MD5_TYPE  	(0x01)
#define SERIAL_OTA_INTERVAL_TIMEOUT_MS  (1000)
#define SERIAL_OTA_RETRY_COUNT          (3)

#define SERIAL_OTA_BUFFER_LEN           (2048)
#define SERIAL_OTA_BUFFER_UNIT_LEN      (128)
#define SERIAL_OTA_CRC32_INVALID_COUNT  (2)

typedef enum
{
  SERIAL_OTA_EVENT_START,
  SERIAL_OTA_EVENT_EXIT,
  SERIAL_OTA_EVENT_DATA_GET,
  SERIAL_OTA_EVENT_VERIFY_GET,
}serial_ota_event_t;


typedef enum
{
  SERIAL_OTA_STATUS_START,
  SERIAL_OTA_STATUS_WAIT_START_ACK,
  SERIAL_OTA_STATUS_QUERYCRC,
  SERIAL_OTA_STATUS_WAIT_QUERYCRC_ACK,
  SERIAL_OTA_STATUS_TRANSFER,
  SERIAL_OTA_STATUS_WAIT_TRANSFER_ACK,
  SERIAL_OTA_STATUS_VERIFY,
  SERIAL_OTA_STATUS_WAIT_VERIFY_ACK,

}serial_ota_status_t;


typedef enum{
	SERIAL_OTA_VERIFY_MIN 		= 0,
	SERIAL_OTA_VERIFY_MD5 		= 1,
	SERIAL_OTA_VERIFY_SHA256 	= 2,
	SERIAL_OTA_VERIFY_CRC32		= 3,
	SERIAL_OTA_VERIFY_MAX
}serial_ota_verify_type_t;

typedef struct{

	uint8_t is_restart;

	uint8_t crc32_invalid_count;

	uint8_t verify_type;
	uint8_t verify_data[32];

	serial_ota_event_t event;
	serial_ota_status_t status;
	qm_utils_time_t ota_time;
	uint32_t ota_interval;
	uint8_t retry_count;

	uint32_t his_crc32;
	uint16_t partition_id;
	uint8_t packet_id;
	uint8_t *buffer;
	uint16_t buf_len;
	uint16_t offset;

}serial_ota_context_t;

#pragma pack(1)

typedef struct
{
	uint16_t partition_id;
	uint32_t crc32;
}serial_partition_info_t;

typedef struct
{
	uint8_t err_code;
	uint16_t partition_id;
	uint8_t packet_id;
}serial_transfer_ack_info_t;

typedef struct
{
	uint16_t partition_id;
	uint8_t packet_id;
	uint8_t data[SERIAL_OTA_BUFFER_UNIT_LEN];
}serial_transfer_info_t;

#pragma pack()

typedef void (*serial_ota_event_notify_fn)(serial_ota_event_t event, void *arg);

int32_t qm_serial_ota_start(uint8_t verify_type, uint8_t is_restart);
int32_t qm_serial_ota_end(void);
int32_t qm_serial_ota_parse(uint8_t cmd, uint8_t *data, uint16_t len);
int32_t qm_serial_ota_verify_set(uint8_t verify_type, uint8_t *verify_data, uint8_t len);
int32_t qm_serial_ota_data_set(uint16_t partition_id, uint32_t crc32, uint8_t *data, uint16_t len);
int32_t qm_serial_ota_process(void);

#endif
