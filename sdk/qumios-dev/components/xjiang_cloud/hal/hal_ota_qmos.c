
#include "qm_types.h"
#include "qm_iot_import.h"
#include "iot_cloud_debug.h"

/**
 * @brief initialize a firmware upgrade.
 *
 * @param None
 * @return None.
 * @see None.
 * @note None.
 */
void hal_firmware_upgrade_start( void )
{ 
    qm_ota_start(NULL);
}

/**
 * @brief save firmware upgrade data to flash.
 *
 * @param[in] buffer: @n A pointer to a buffer to save data.
 * @param[in] length: @n The length, in bytes, of the data pointed to by the buffer parameter.
 * @return 0, Save success; -1, Save failure.
 * @see None.
 * @note None.
 */

int hal_firmware_upgrade_write(char *buffer, uint32_t length )
{
    return qm_ota_write(NULL, buffer, length);
}
/**
 * @brief indicate firmware upgrade data complete, and trigger data integrity checking,
 and then reboot the system.
 *
 * @param None.
 * @return 0: Success; -1: Failure.
 * @see None.
 * @note None.
 */
int hal_firmware_upgrade_finish(int finish)
{
    return qm_ota_end(NULL);
}

