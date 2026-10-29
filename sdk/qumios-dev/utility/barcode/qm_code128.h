#ifndef _QM_CODE128_H_
#define _QM_CODE128_H_

#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Since the FNCn characters are not ASCII, define versions here to
// simplify encoding strings that include them.
#define CODE128_FNC1 '\xf1'
#define CODE128_FNC2 '\xf2'
#define CODE128_FNC3 '\xf3'
#define CODE128_FNC4 '\xf4'

uint32_t qm_code128_estimate_len(const char * s);
uint32_t qm_code128_encode_gs1(const char * s, char * out, uint32_t maxlength);
uint32_t qm_code128_encode_raw(const char * s, char * out, uint32_t maxlength);

#ifdef __cplusplus
}
#endif

#endif /* QM_CODE128_H */
