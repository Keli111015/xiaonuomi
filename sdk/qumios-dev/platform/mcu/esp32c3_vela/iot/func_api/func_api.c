#include "func_api.h"


void get_last_str(char *data, char *recv_data, uint8_t char_n) //自行实现提取空格前的字符串
{   
    if(data == NULL || recv_data == NULL){
        return ;
    }
    
    for(int i = 0; i < strlen((char *)data); i++){
        if(data[i] == char_n){
            memcpy(recv_data,(char *)data, i);
            return;
        }
    }
}
