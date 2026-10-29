#include "prodtst_special.h"
#include "qm_errno.h"
#include "qm_types.h"
#include "prodtst_hal_uart.h"
#include "qm_utils_string.h"
#include "qm_kernel.h"
#include "qm_log.h"

#define LOG_TAG "nfc"

typedef struct{
    int uart_channel; 
}spcl_handle_t;

static spcl_handle_t g_spcl_handle = {0};
#define PRODTST_NFC_UART_CHANNEL   PRODTST_UART_NUM_7

#pragma pack(1)

typedef struct{
    uint8_t head;
    uint8_t len;
    uint8_t  cmd;
}nfc_frame_header_t;

#pragma pack()


#define HEX_LOG(name, p_data, len) \
    do{\
        uint32_t i; \
        printf("[%s:%d]",__FUNCTION__,__LINE__);\
        printf("%s ",name);  \
        for(i=0;i<len;i++)\
        {\
          printf("%02X ", *(p_data+i));\
        }\
        printf("\n");\
      }while(0)


void *prodtst_nfc_establish(void *param)
{
    static int init = 0;
    qm_err_t ret = QM_EOK;
    g_spcl_handle.uart_channel = PRODTST_NFC_UART_CHANNEL;
    if(!init){
        init = 1;
        ret = prodtst_uart_init(PRODTST_NFC_UART_CHANNEL, 115200);
    }

    return (void*)&g_spcl_handle;
}

int prodtst_nfc_destroy(void *handle)
{
    spcl_handle_t *spcl_handle = NULL;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int prodtst_nfc_send(void *handle, char *buf, int size)
{
    int hex_len = size/2; 
    uint8_t *hex = NULL;
    qm_err_t ret = QM_EOK;
    spcl_handle_t *spcl_handle = NULL;

    if(handle == NULL || buf == NULL || size == 0){
        return -QM_EINVAL;
    }

    spcl_handle = (spcl_handle_t*)handle;

    hex = (uint8_t*)qm_malloc(hex_len);
    if(hex == NULL){
        return -QM_ENOMEM;
    }
    
    hex_str_to_nums(buf, size, hex);
    ret = prodtst_uart_write(spcl_handle->uart_channel, hex, hex_len);
    qm_free(hex);
    return ret;
}

int prodtst_nfc_recv(void *handle, char *buf, int size, int timeout)
{
    int recv_len = 0;
    int hex_len = size/2; 
    uint8_t *hex = NULL;
    qm_err_t ret = QM_EOK;
    spcl_handle_t *spcl_handle = NULL;

    hex = (uint8_t*)qm_malloc(hex_len);
    if(hex == NULL){
        return -QM_ENOMEM;
    }
    spcl_handle = (spcl_handle_t*)handle;

    recv_len = prodtst_uart_read(spcl_handle->uart_channel, hex, hex_len, timeout);
    if(recv_len > 0){
        hex_to_strs(hex, recv_len, buf);
        ret = recv_len*2;
    }
    qm_free(hex);
    return ret;
}


static int nfc_check_send(int channel)
{
    nfc_frame_header_t frame = {0};
    frame.head = 0xAA;
    frame.len = 0x01;
    frame.cmd = 0x02;
    prodtst_uart_write(channel, (uint8_t*)&frame, sizeof(nfc_frame_header_t));
    return QM_EOK;
}


static int nfc_read_send(int channel)
{
    uint8_t buf[5] = {0xAA, 0x03, 0x91, 0x00, 0x10};
    prodtst_uart_write(channel, buf, sizeof(buf));
    return QM_EOK;
}

static int nfc_parse(int channel, uint8_t cmd, uint8_t *data, int *len)
{
    int recv_len = 0;
    nfc_frame_header_t *frame = NULL;
    while(1){
        recv_len = prodtst_uart_read(channel, data, sizeof(nfc_frame_header_t), 1000);
        if(recv_len != sizeof(nfc_frame_header_t)){
            return -QM_ETIMEOUT;
        }

        frame = (nfc_frame_header_t*)data;

        if(frame->head != 0xAA){
            continue;
        }

        HEX_LOG("nfc header", data, sizeof(nfc_frame_header_t));

        if(frame->len-1 > 0){
            recv_len = prodtst_uart_read(channel, data+sizeof(nfc_frame_header_t), frame->len-1, 1000);
            if(recv_len != frame->len-1){
                return -QM_ETIMEOUT;
            }

            HEX_LOG("nfc data", data+sizeof(nfc_frame_header_t), frame->len-1);
        }

        if(frame->cmd != cmd){
            continue;
        }else{
            *len = recv_len+sizeof(nfc_frame_header_t);
            return QM_EOK;
        }
    }
}

int prodtst_nfc_process(void *handle, char *in_buf, int in_size, char *out_buf, int out_size)
{
    qm_err_t ret = QM_EOK;
    spcl_handle_t *spcl_handle = NULL;
    spcl_handle = (spcl_handle_t*)handle;
    int check_count = 10;
    int write_count = 3;
    int read_count = 3;
    int right = 0;
    uint8_t nfc_buf[128] = {0};
    int nfc_recv_size = 128;

    if(handle == NULL || in_buf == NULL || 
       in_size == 0 || out_buf == NULL || out_size == 0){
        return -QM_EINVAL;
    }

    while(check_count--){
        nfc_check_send(spcl_handle->uart_channel);
        ret = nfc_parse(spcl_handle->uart_channel, 0x02, nfc_buf, &nfc_recv_size);
        if(ret == -QM_ETIMEOUT){
            continue;
        }else{
            right = 1;
            break;
        }
    }
    if(right){
        QM_LOGI(LOG_TAG, "nfc is already");
    }else{
        QM_LOGI(LOG_TAG, "nfc is null");
        return -QM_ERROR;
    }

    right = 0;

    while(write_count--){
        prodtst_nfc_send(handle, in_buf, in_size);
        ret = nfc_parse(spcl_handle->uart_channel, 0xfe, nfc_buf, &nfc_recv_size);
        if(ret == -QM_ETIMEOUT){
            QM_LOGI(LOG_TAG, "nfc write again");
            continue;
        }else{
            right = 1;
            break;
        }
    }

    if(right){
        QM_LOGI(LOG_TAG, "nfc write success");
    }else{
         QM_LOGI(LOG_TAG, "nfc write fail");
        return -QM_ERROR; 
    }

    right = 0;

    while(read_count--){
        nfc_read_send(spcl_handle->uart_channel);
        ret = nfc_parse(spcl_handle->uart_channel, 0x91, nfc_buf, &nfc_recv_size);
        if(ret == -QM_ETIMEOUT){
            QM_LOGI(LOG_TAG, "nfc write again");
            continue;
        }else{
            right = 1;
            break;
        }
    }

    if(right){
        QM_LOGI(LOG_TAG, "nfc read success");
        hex_to_strs(nfc_buf, nfc_recv_size, out_buf);
    }else{
        QM_LOGI(LOG_TAG, "nfc read fail");
        return -QM_ERROR; 
    }

    return QM_EOK;
}