#ifndef __PRODTST_SPECIAL_H__
#define __PRODTST_SPECIAL_H__


typedef struct{
    char *action;
    int recv_size;
    void *(*establish)(void *param);
    int (*destroy)(void *handle);
    int (*send)(void *handle, char *buf, int size);
    int (*recv)(void *handle, char *buf, int size, int timeout);
    int (*process)(void *handle, char *in_buf, int in_size, char *out_buf, int out_size);
}prodtst_special_t;


prodtst_special_t *prodtst_special_get(char *action);


extern void *prodtst_nfc_establish(void *param);
extern int prodtst_nfc_destroy(void *handle);
extern int prodtst_nfc_send(void *handle, char *buf, int size);
extern int prodtst_nfc_recv(void *handle, char *buf, int size, int timeout);
extern int prodtst_nfc_process(void *handle, char *in_buf, int in_size, char *out_buf, int out_size);


#endif
