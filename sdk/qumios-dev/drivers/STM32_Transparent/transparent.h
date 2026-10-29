#ifndef TRANSPARENT_H
#define TRANSPARENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_utils_list.h"

#define TRANSPARENT_HEADER        0xFFFF
#define TRANSPARENT_HEADER_PART   0xFF
#define TRANSPARENT_VER           0x01
#define TRANSPARENT_MAX_DEEPTH    5

#define TRANSPARENT_INVALUE_PORT           (7)
#define TRANSPARENT_DEVICE_ITSELF_PORT     (0)

#pragma pack(1)

typedef struct {
    uint16_t head;
    uint8_t ver;
    uint16_t port;
    uint16_t len;
}transparent_header_t;

#pragma pack()

typedef struct{
    transparent_header_t header;
    uint8_t payload[1];
}transparent_data_info_t;

/*
 *  级联设备名的序号表示当前设备的深度， 如 级联设备3 表示该设备在第三层
 *   ----------------------------------------------------------------
 *  |  保留位  |  级联设备5 |  级联设备4  |  级联设备3  |  级联设备2  |  级联设备1 |
 *   ----------------------------------------------------------------
 *  |  1 Byte  |  3 Bytes  |   3 Bytes  |   3 Bytes   |   3 Bytes  | 3 Bytes   |
 */

typedef union{
	struct{
        uint16_t  port1:3;
        uint16_t  port2:3;
        uint16_t  port3:3;
        uint16_t  port4:3;
        uint16_t  port5:3;
        uint16_t  inval:1; // 保留位
	}channel;
	uint16_t vaule;
}transparent_port_t;

typedef struct{
    int32_t (*write)(void *handle, void *data, uint32_t len);
    int32_t (*readtimeout)(void *handle, void *data, uint32_t len, uint32_t timeout);
    void *handle;
}transparent_param_t;


void *transparent_init(transparent_param_t *param);

transparent_port_t *transparent_port_new(void);
int transparent_port_add(transparent_port_t *port, uint16_t sub_port);
int transparent_port_free(transparent_port_t *port);

int32_t transparent_clear(void *handle, transparent_port_t *port);

int32_t transparent_write_cfg(void *handle, transparent_port_t *port, uint8_t *data, uint32_t len);
int32_t transparent_read_cfg(void *handle,  transparent_port_t *port, uint8_t *data, uint32_t len, uint32_t timeout);

int32_t transparent_write(void *handle, transparent_port_t *port, uint8_t *data, uint32_t len);
int32_t transparent_read(void *handle,  transparent_port_t *port, uint8_t *data, uint32_t len, uint32_t timeout);

#if 0
int32_t transparent_deinit(void *handle);
#endif

#ifdef __cplusplus
}
#endif

#endif