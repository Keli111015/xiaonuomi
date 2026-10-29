#include "prodtst_pi.h"
#include "qm_log.h"
#include "mtrace.h"
#include "qm_cli.h"
#include "qm_errno.h"
#include "prodtst_comm.h"
#include "qm_kv.h"
#include "qm_utils_string.h"


static void handle_prodtst_key(char *pwbuf, int blen, int argc, char **argv);

static struct qm_cli_command prodtst_key = {
    .name = "prodtst_key",
    .help = "set/get prodtst keys [<Product Id> <Device Id> <Device Secret>]",
    .function = handle_prodtst_key
};


static void handle_prodtst_key(char *pwbuf, int blen, int argc, char **argv)
{
    char device_id[PRODTST_DEVICE_ID_MAX_LEN + 1] = { 0 };
    char device_secret[PRODTST_DEVICE_SECRET_MAX_LEN + 1] = { 0 };
    int device_id_len = PRODTST_DEVICE_ID_MAX_LEN;
    int device_secret_len = PRODTST_DEVICE_SECRET_MAX_LEN;
    uint32_t product_id = 0;
    if(argc == 2){
        prodtst_get_device_id(device_id, &device_id_len);
        prodtst_get_device_secret(device_secret, &device_secret_len);
        prodtst_get_product_id(&product_id);
        qm_cli_printf("Product Id=%u.\r\n", product_id);
        qm_cli_printf("Device Id=%s.\r\n", device_id);
        qm_cli_printf("Device Secret=%s.\r\n", device_secret);
    }else if (argc == 5) {
        product_id = int_str_to_num(argv[2], strlen(argv[2]));
        prodtst_set_product_id(product_id);
        prodtst_set_device_id(argv[3], strlen(argv[3]));
        prodtst_set_device_secret(argv[4], strlen(argv[4]));
        qm_cli_printf("OK\r\n");
    } else {
        qm_cli_printf("Error\r\n");
        return;
    }
}

int prodtst_get_product_id(unsigned int *product_id)
{
    int len = sizeof(uint32_t);
    unsigned int pid = 0;
    if(product_id == NULL){
        return -1;
    }

    if (0 != qm_kv_get("product_id", &pid, &len)) {
        return -1;
    }
    *product_id = pid;
    return 0;
}

int prodtst_set_product_id(unsigned int product_id)
{
    if(product_id == 0){
        return -1;
    }

    if (0 != qm_kv_set("product_id", &product_id, sizeof(uint32_t), 1)) {
        return -1;
    }
    return 0;
}


int prodtst_get_device_secret(char *device_secret, int *len)
{
    if(!device_secret || !len){
        return -1;
    }
    if(*len < PRODTST_DEVICE_SECRET_MAX_LEN){
        return -1;
    }
    
    int tmp_len = PRODTST_DEVICE_SECRET_MAX_LEN;
    if (0 != qm_kv_get("device_secret", device_secret, &tmp_len)) {
        return -1;
    }

    *len = tmp_len;
    return 0;
}

int prodtst_set_device_secret(char *device_secret, int len)
{
    if(!device_secret || len > PRODTST_DEVICE_SECRET_MAX_LEN){
        return -1;
    }

    if (0 != qm_kv_set("device_secret", device_secret, len, 1)) {
        return -1;
    }
    return 0;
}

int prodtst_get_device_id(char *device_id, int *len)
{
    if(!device_id || !len){
        return -1;
    }
    if(*len < PRODTST_DEVICE_ID_MAX_LEN){
        return -1;
    }
    
    int tmp_len = PRODTST_DEVICE_ID_MAX_LEN;
    if (0 != qm_kv_get("device_id", device_id, &tmp_len)) {
        return -1;
    }

    *len = tmp_len;
    return 0;
}


int prodtst_set_device_id(char *device_id, int len)
{    
    if(!device_id || len > PRODTST_DEVICE_ID_MAX_LEN){
        return -1;
    }
    
    if (0 != qm_kv_set("device_id", device_id, len, 1)) {
        return -1;
    }
    return 0;
}

int prodtst_pi_init(void)
{
    qm_cli_register_command(&prodtst_key);
    return QM_EOK;
}