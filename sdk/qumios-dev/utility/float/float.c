#include "float.h"

void ieee754_binary64_encode( float64_t x, uint8_t out[8] )
{
    bool_t sign = x < 0;
    uint16_t exponent;
    uint64_t fraction;
    int e = 0;
    if( isinf( x ) ){
        exponent = 0x7FF;
        fraction = 0;
    }else if( isnan( x ) ){ // nan check
        exponent = 0x7FF;
        fraction = 0xFFFFFFFFFFFFF;
    }else{
        if(sign){
            x = -x;
        }
        fraction = frexp( x, &e ) * ((uint64_t)2<<52);
        exponent = e + 1022;
        if( exponent > 0x7FF ){
            exponent = 0x7FF;
            fraction = 0;
        }
    }
    out[0] = ( ( sign << 7 ) & 0x80 )
          | ( ( exponent >>  4 ) & 0x7F );
    out[1] = ( ( exponent <<  4 ) & 0xF0 )
          | ( ( fraction >> 48 ) & 0x0F );
    out[2] =   ( fraction >> 40 ) & 0xFF;
    out[3] =   ( fraction >> 32 ) & 0xFF;
    out[4] =   ( fraction >> 24 ) & 0xFF;
    out[5] =   ( fraction >> 16 ) & 0xFF;
    out[6] =   ( fraction >>  8 ) & 0xFF;
    out[7] =     fraction         & 0xFF;
}

double ieee754_binary64_decode( uint8_t in[8] )
{
    bool_t sign = in[0] & 0x80;
    uint16_t exponent = ( ( in[0] << 4 ) & 0x7F0 )
                      | ( ( in[1] >> 4 ) & 0x0F );
    uint64_t fraction = ( (uint64_t)( in[1] & 0x0F ) << 48 )
                      | ( (uint64_t)( in[2] & 0xFF ) << 40 )
                      | ( (uint64_t)( in[3] & 0xFF ) << 32 )
                      | ( (uint64_t)( in[4] & 0xFF ) << 24 )
                      | ( (uint64_t)( in[5] & 0xFF ) << 16 )
                      | ( (uint64_t)( in[6] & 0xFF ) <<  8 )
                      |   (uint64_t)( in[7] & 0xFF )
                      | ( (uint64_t)1<<52 );
    double frac = (double)fraction / ( (uint64_t)2<<52 );
    if( exponent == 0x7FF ){
        if( fraction == (uint64_t)1<<52 ){ // Infinity
            return sign ? -1.0/0.0 : 1.0/0.0;
        }else{ // NaN
            return sign ? 0.0/0.0 : -(0.0/0.0);
        }
    }
    return ldexp( frac, exponent-1022 ) * ( sign ? -1 : 1 );
}

void ieee754_binary32_encode( float x, uint8_t out[4] )
{
    bool_t sign = x < 0;
    uint8_t exponent;
    uint32_t fraction;
    int e = 0;
    if( isinf( x ) ){
        exponent = 0xFF;
        fraction = 0;
    }else if( isnan( x ) ){ // nan check
        exponent = 0xFF;
        fraction = 0x7FFFFF;
    }else{
        if(sign){
            x = -x;
        }
        fraction = frexp( x, &e ) * ((uint32_t)2<<23);
        exponent = e + 126;
        if( e + 126 > 0xFF ){
            exponent = 0xFF;
            fraction = 0;
        }
    }
    out[0] = ( ( sign << 7 ) & 0x80 )
          | ( ( exponent >>  1 ) & 0x7F );
    out[1] = ( ( exponent <<  7 ) & 0x80 )
          | ( ( fraction >> 16 ) & 0x7F );
    out[2] =   ( fraction >>  8 ) & 0xFF;
    out[3] =     fraction         & 0xFF;
}

float ieee754_binary32_decode( uint8_t in[4] )
{
    bool_t sign = in[0] & 0x80;
    uint8_t exponent = ( ( in[0] << 1 ) & 0xFE )
                      | ( ( in[1] >> 7 ) & 0x01 );
    uint32_t fraction = ( (uint32_t)( in[1] & 0x7F ) << 16 )
                      | ( (uint32_t)( in[2] & 0xFF ) <<  8 )
                      |   (uint32_t)( in[3] & 0xFF )
                      | ( (uint32_t)1<<23 );
    float frac = (float)fraction / ( (uint32_t)2<<23 );
    if( exponent == 0xFF ){
        if( fraction == (uint32_t)1<<23 ){ // Infinity
            return sign ? -1.0/0.0 : 1.0/0.0;
        }else{ // NaN
            return sign ? 0.0/0.0 : -(0.0/0.0);
        }
    }
    return ldexp( frac, exponent-126 ) * ( sign ? -1 : 1 );
}
