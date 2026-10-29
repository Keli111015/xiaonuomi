
#include "util.h"
#include "ctype.h"
#include <stdbool.h>


double atof_my(const char *str)
{
    double s=0.0;

    double d=10.0;
    int jishu=0;

    bool falg=false;

    while(*str==' ')
    {
        str++;
    }

    if(*str=='-')//记录数字正负
    {
        falg=true;
        str++;
    }

    if(!(*str>='0'&&*str<='9'))//如果一开始非数字则退出，返回0.0
        return s;

    while(*str>='0'&&*str<='9'&&*str!='.')//计算小数点前整数部分
    {
        s=s*10.0+*str-'0';
        str++;
    }

    if(*str=='.')//以后为小树部分
        str++;

    while(*str>='0'&&*str<='9')//计算小数部分
    {
        s=s+(*str-'0')/d;
        d*=10.0;
        str++;
    }

    if(*str=='e'||*str=='E')//考虑科学计数法
    {
        str++;
        if(*str=='+')
        {
            str++;
            while(*str>='0'&&*str<='9')
            {
                jishu=jishu*10+*str-'0';
                str++;
            }
            while(jishu>0)
            {
                s*=10;
                jishu--;
            }
        }
        if(*str=='-')
        {
            str++;
            while(*str>='0'&&*str<='9')
            {
                jishu=jishu*10+*str-'0';
                str++;
            }
            while(jishu>0)
            {
                s/=10;
                jishu--;
            }
        }
    }

    return s*(falg?-1.0:1.0);
}


void str_n_cat(char *pDst, int n_str, ...)
{

    va_list list;
    va_start(list, n_str);

    const char *p = NULL;

    while (n_str--) {
        p = va_arg(list, const char *);
        strncat(pDst, p, strlen(p));
    }

    va_end(list);

}


//转换为无符号数
//n表示读取数字字符的最大长度
uint32_t  atoun(const char* c, size_t n)
{
	uint32_t dig = 0;
	const char *org = c;
	while(isdigit((int)*c) && (c-org < n) ){
		dig = dig*10 + *c - '0';
		c++;
	}
	return dig;
}

//16进制数串转换为无符号数
uint32_t  axtou(const char* c)
{
	uint32_t dig = 0;
//	const char *org = c;
	while(isxdigit((int)*c)){
		dig <<= 4;
		dig += 0x0000000F & hex_char_value((int)*c);
		c++;
	}
	return dig;
}

//16进制数串转换为无符号数 n限制采样字符数
uint32_t  axtoun(const char* c, size_t n)
{
	int dig = 0;
	const char *org = c;
	while(isxdigit((int)*c) && (c-org < n) ){
		dig = dig*16 + hex_char_value((int)*c);
		c++;
	}
	return dig;
}

//将HEX字符串转换为二进制数组
//in尺寸有限制
//out空间有限制
//in_len输出已处理输入长度
//返回输出长度
//axtobuf("12EF985xyz",out) 返回6，最后一个'5'被抛弃
size_t  axtobuf(const char* in, size_t in_size, uint8_t* out, size_t out_size, size_t *in_len)
{
	const char *org_in = in;
	uint8_t *org_out = out;

	while( isxdigit((int)*in) && isxdigit((int)*(in+1)) &&	//输入不硌牙
			(in  - org_in <  in_size) && (out - org_out < out_size)){	// 不超限

		*out = (0x0F & hex_char_value((int)*(in+1))) | (0xF0 & (hex_char_value((int)*in) << 4));	//转换处理

		in += 2; out += 1;	//调整指针
 	}

	if(in_len)
		*in_len = in  - org_in;

	return out - org_out;
}
//转换为无符号的数，n表示读取数字字符的最大长度。
uint64_t atou64n(const char* c, size_t n)
{
	uint64_t dig = 0;
	const char *org = c;
	while(isdigit((int)*c) && (c-org < n)){
		dig = dig*10 + *c - '0';
		c++;
	}
	return dig;
}

//转换为无符号数
//n表示读取数字字符的最大长度
int64_t atos64n(const char* c, size_t n)
{
    if (*c == '-')
        return 0 - (int64_t) atou64n(c + 1, n - 1);
    else
        return (int64_t) atou64n(c, n);
}


//转换为无符号数
//n表示读取数字字符的最大长度
int32_t atoin(const char * c, size_t n)
{
    if(*c == '-')
        return 0 - (int32_t) atoun(c + 1, n - 1);
    else
        return (int32_t) atoun(c, n);
}


int u64toa(uint64_t data, char *c)
{
    int num = 0, index;
    char tmp;

    while(data > 0) {
        c[num++] = data%10 + '0';
        data /= 10;
    }
    c[num] = '\0';
    for(index = 0; index < (num >> 1); index++) {
        tmp = c[index];
        c[index] = c[num - 1 - index];
        c[num - 1 - index] = tmp;
    }

    return num;
}

int s64toa(int64_t data, char *c)
{
	if(data < 0){
		c[0] = '-';
		data = -data;
		return (1 + u64toa(data, c+1));
	}

	return u64toa(data, c);
}

bool_t str_all_c(uint8_t *str, uint8_t c, size_t len)
{
	for(size_t i=0; i < len; i++){
		if(str[i] != c)
			return QM_FALSE;
	}
	return QM_TRUE;
}
