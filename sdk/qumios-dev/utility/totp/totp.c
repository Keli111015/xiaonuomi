#include "totp.h"
#include "qm_errno.h"
#include "qm_utils_hmac.h"
#include "qm_utils_string.h"

int totp_get_key(const uint8_t *secret, int secret_len, uint32_t timestamp, uint32_t *key)
{
	qm_err_t ret = QM_EOK;
	uint8_t hmac[20] = {0};		 			     /* HMAC.                     */
	char str_hmac[40+1] = {0};
	uint8_t time_padded[TOTP_TIME_PADDED] = {0}; /* Time padded into 8 bytes. */
	uint32_t ctime = 0;							 /* Reference time.           */
	int offset = 0;								 /* Result offset.            */
	int result = 0;								 /* Result.                   */

	if (secret == NULL || secret_len == 0 || timestamp == 0 || key == NULL){
		return -QM_EINVAL;
	}
	ctime = timestamp;

	/* Pad time into a byte array. */
	for (int i = TOTP_TIME_PADDED - 1; i >= 0; i--)
	{
		time_padded[i] = ctime;
		ctime >>= 8;
	}

	/* Calculates hmac. */
	ret = qm_utils_hmac_sha1((char*)time_padded, TOTP_TIME_PADDED, str_hmac, (char*)secret, secret_len);
	if(ret != QM_EOK){
		return ret;
	}

	hex_str_to_nums(str_hmac, 40, hmac);

	/* Get offset and result. */
	offset = hmac[20 - 1] & 0xF;
	result = (((hmac[offset + 0] & 0x7F) << 24) |
			  ((hmac[offset + 1] & 0xFF) << 16) |
			  ((hmac[offset + 2] & 0xFF) << 8) |
			  ((hmac[offset + 3] & 0xFF)));

	result &= 0x7FFFFFFF;
	result %= CONFIG_TOTP_DIGITS;

	*key = result;

	return QM_EOK;
}

int totp_get_keys(uint32_t refresh_interval, const uint8_t *secret, int secret_len, uint32_t timestamp, totp_t *p_totp, totp_t *p_last_totp)
{
	int i = 0;
	uint32_t interval = 0;
	uint32_t ctime = 0; 
	uint32_t last_ctime = 0; 
	uint32_t *key = (uint32_t*)p_totp;
	uint32_t *last_key = (uint32_t*)p_last_totp;

	if(refresh_interval < TOTP_REFRESH_MIN_INTERVAL || refresh_interval > TOTP_REFRESH_MAX_INTERVAL){
		return -QM_EINVAL;
	}

	if(secret == NULL || secret_len == 0 || p_totp == NULL || p_last_totp == NULL){
		return -QM_EINVAL;
	}
	
	ctime = timestamp;

	ctime -= ctime % refresh_interval;
	last_ctime = ctime - refresh_interval;

	interval = refresh_interval/CONFIG_TOTP_KEY_SETS;

	for(i = 0; i<CONFIG_TOTP_KEY_SETS; i++){
		totp_get_key(secret, secret_len, ctime, &key[i]);
		ctime += interval;
	}

	for(i = 0; i<CONFIG_TOTP_KEY_SETS; i++){
		totp_get_key(secret, secret_len, last_ctime, &last_key[i]);
		last_ctime += interval;
	}

	return QM_EOK;
}
