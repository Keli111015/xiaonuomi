
#ifndef _QM_UTILS_BASE64_H_
#define _QM_UTILS_BASE64_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"


int qm_utils_base64encode(const uint8_t *data, uint32_t inputLength, uint32_t outputLenMax,
                              uint8_t *encodedData, uint32_t *outputLength);
int qm_utils_base64decode(const uint8_t *data, uint32_t inputLength, uint32_t outputLenMax,
                              uint8_t *decodedData, uint32_t *outputLength);



#ifdef __cplusplus
}
#endif

#endif
