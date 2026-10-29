#ifndef IEE754_FLOAT_H
#define IEE754_FLOAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

void ieee754_binary64_encode( float64_t x, uint8_t out[8] );
float64_t ieee754_binary64_decode( uint8_t in[8] );
void ieee754_binary32_encode( float32_t x, uint8_t out[4] );
float32_t ieee754_binary32_decode( uint8_t in[4] );

#ifdef __cplusplus
}
#endif

#endif