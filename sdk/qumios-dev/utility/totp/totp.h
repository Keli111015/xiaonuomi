
#ifndef _TOTP_H_
#define _TOTP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_types.h"

/* Amount of digits for the final key. */
#ifndef CONFIG_TOTP_KEY_DIGITS  
#define CONFIG_TOTP_KEY_DIGITS   6
#endif 

/* totp key sets */
#ifndef CONFIG_TOTP_KEY_SETS
#define CONFIG_TOTP_KEY_SETS     8
#endif

#if CONFIG_TOTP_KEY_DIGITS == 3
#define CONFIG_TOTP_DIGITS 1000
#elif CONFIG_TOTP_KEY_DIGITS == 4
#define CONFIG_TOTP_DIGITS 10000
#elif CONFIG_TOTP_KEY_DIGITS == 5
#define CONFIG_TOTP_DIGITS 100000
#elif CONFIG_TOTP_KEY_DIGITS == 6
#define CONFIG_TOTP_DIGITS 1000000
#elif CONFIG_TOTP_KEY_DIGITS == 7
#define CONFIG_TOTP_DIGITS 10000000
#elif CONFIG_TOTP_KEY_DIGITS == 8
#define CONFIG_TOTP_DIGITS 100000000
#endif

/* Time padded size, in bytes. */
#define TOTP_TIME_PADDED    8

#define TOTP_REFRESH_MIN_INTERVAL  600
#define TOTP_REFRESH_MAX_INTERVAL  3600

typedef uint32_t totp_t[CONFIG_TOTP_KEY_SETS];

/**
 * generate a password from given secret key and utc timestamp
 *
 * @param [in] secret: Your secret-key
 * 
 * @param [in] secret_len: Secret key size, in bytes. 
 * 
 * @param [in] timestamp: target time in Unix Time Stamp
 *
 * @param [out] key: password
 * 
 * @return 0:success,otherwise is error
 *
 */
extern int totp_get_key(const uint8_t *secret, int secret_len, uint32_t timestamp, uint32_t *key);

/**
 * generate 8 sets password from given secret key and utc timestamp
 *
 * @param [in] refresh_interval : OTP validity period in second. Range from 600s to 3600s.
 * 
 * @param [in] secret : Your secret-key
 * 
 * @param [in] secret_len: Secret key size, in bytes. 
 *
 * @param [in] timestamp : target time in Unix Time Stamp
 * 
 * @param [out] p_totp : pointer the memory that OTP to be stored.
 * 
 * @param [out] p_last_totp : pointer the memory that last valid OTP to be stored
 *
 * @return 0:success,otherwise is error
 *
 */
extern int totp_get_keys(uint32_t refresh_interval, const uint8_t *secret, int secret_len, uint32_t timestamp, totp_t *p_totp, totp_t *p_last_totp);



#ifdef __cplusplus
}
#endif

#endif /* QM_TOTP_H */
