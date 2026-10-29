#include "qm.h"
#include "qm_iot_streams_base64.h"

#include "qm_utils_base64.h"

/**
 * @brief Decode Base64 encoded data.
 *
 * @param[in]  data Pointer to a buffer containing the Base64 encoded
 *             data that is intended to be decoded.
 * @param[in]  inputLength Length of the encodedData buffer.
 * @param[in]  outputLenMax Length of the dest buffer.
 * @param[out] decodedData Pointer to a buffer for storing the decoded result.
 * @param[out] outputLength Pointer to the length of the decoded result.
 * 
 * @return     One of the following:
 *             - #Base64Success if the Base64 encoded data was valid
 *               and successfully decoded.
 *             - An error code defined in ota_base64_private.h if the
 *               encoded data or input parameters are invalid.
 */
int qm_iot_streams_base64_decode(const uint8_t *data, uint32_t inputLength, 
                                uint32_t outputLenMax,
                                uint8_t *decodedData, uint32_t *outputLength )
{
    return qm_utils_base64decode(data, inputLength, outputLenMax, decodedData, outputLength);
}
