#ifndef PRODTST_PI_H
#define PRODTST_PI_H

#ifdef __cplusplus
extern "C" {
#endif


#define PRODTST_PRODUCT_ID      "158689"
//#define PRODTST_DEVICE_ID       "cE5ww1"//"oybxxd"
#define PRODTST_DEVICE_ID       "cE5001"//"oybxxd"
#define PRODTST_DEVICE_SECRET   "6EE05E79B64E8D90E58BAD9768715C27"


int prodtst_get_product_id(unsigned int *product_id);
int prodtst_set_product_id(unsigned int product_id);
int prodtst_get_device_secret(char *device_secret, int *len);
int prodtst_set_device_secret(char *device_secret, int len);
int prodtst_get_device_id(char *device_id, int *len);
int prodtst_set_device_id(char *device_id, int len);

int prodtst_pi_init(void);

#ifdef __cplusplus
}
#endif

#endif