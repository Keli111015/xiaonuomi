#include "qm_utils_string.h"

int32_t hex_to_strs(uint8_t*in_buf, uint8_t len, char* out_buf)
{
    uint8_t i=0;

    uint8_t ddl;
    uint8_t ddh;

  if(!in_buf|| !out_buf || len==0)
       return -1;

  for (i=0; i<len; i++)
    {
        ddh = 48 + in_buf[i] / 16;
        ddl = 48 + in_buf[i] % 16;
        if (ddh > 57) ddh = ddh + 39;
        if (ddl > 57) ddl = ddl + 39;
        out_buf[i*2] = ddh;
        out_buf[i*2+1] = ddl;
    }

  out_buf[len*2] = '\0';

  return 0;
}

int32_t hex_to_strs_print(uint8_t*in_buf, uint8_t len, char* out_buf)
{
    uint8_t i=0;

    uint8_t ddl;
    uint8_t ddh;

  if(!in_buf|| !out_buf || len==0)
       return -1;

  for (i=0; i<len; i++)
    {
        ddh = 48 + in_buf[i] / 16;
        ddl = 48 + in_buf[i] % 16;
        if (ddh > 57) ddh = ddh + 7;
        if (ddl > 57) ddl = ddl + 7;
        out_buf[i*3] = ddh;
        out_buf[i*3+1] = ddl;
        out_buf[i*3+2] = ' ';
    }

  out_buf[len*3] = '\0';

  return 0;
}

int32_t hex_str_to_nums(char* hex_str, int len, uint8_t* num_str )
{
   int i=0, j=0;

   if(hex_str==NULL || num_str==NULL || len==0)
       return -1;

   for(i=0; i<len; i=i+2)
   {
     num_str[j]=hex_str_to_num(hex_str+i, 2);
     j++;
   }

   return 0;
}


uint32_t hex_str_to_num(const char*str, uint8_t len)
{
    uint32_t h=0;
    len=len-1;

    while (len--)
    {
        if (*str>='0' && *str<='9')
            h+=(*str)-'0';
        else if (*str>='A' && *str<='F')
            h+=10+(*str)-'A';
        else if (*str>='a' && *str<='f')
            h+=10+(*str)-'a';
        else return 0;

        h=h<<4;str++;
    }

    if (*str>='0' && *str<='9')
        h+=(*str)-'0';
    else if (*str>='A' && *str<='F')
        h+=10+(*str)-'A';
    else if (*str>='a' && *str<='f')
        h+=10+(*str)-'a';
    else return 0;



    return h;
}

uint32_t int_str_to_num(const char *str, uint8_t len)
{
    uint32_t h=0;
    len=len-1;

    while (len--)
    {
        if (*str>='0' && *str<='9')
            h+=(*str)-'0';

        else return 0;

        h=h*10;str++;
    }

    if (*str>='0' && *str<='9')
        h+=(*str)-'0';

    else return 0;

    return h;

}
