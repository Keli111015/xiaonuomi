#include "qm.h"

#include "mbedtls/platform_util.h"

void mbedtls_platform_zeroize( void *buf, size_t len )
{
    volatile unsigned char *p = buf; 

    while( len-- ) 
    {
        *p++ = 0;
    }
    
}