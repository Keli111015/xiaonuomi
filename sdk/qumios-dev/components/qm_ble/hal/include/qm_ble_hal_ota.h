#ifndef _QM_BLE_HAL_OTA_H_
#define _QM_BLE_HAL_OTA_H_




void qm_ble_ota_start( void );


/**
 * Write data to an area on a flash logical partition without erase
 *
 * @param[in/out]  offset    Point to the start address that the data is written to, and
 *                           point to the last unwritten address after this function is
 *                           returned, so you can call this function serval times without
 *                           update this start address.
 * @param[in]  buf           point to the data buffer that will be written to flash
 * @param[in]  buf_len  The size of the buffer
 *
 * @return  0 : On success,  otherwise is error
 */
 
int qm_ble_ota_flash_write(unsigned int* offset, unsigned char *buf ,int buf_len );

/**
* Read data from an area on a Flash to data buffer in RAM
* @param[in/out]  offset     Point to the start address that the data is read, and
*                            point to the last unread address after this function is
*                            returned, so you can call this function serval times without
*                            update this start address.
* @param[in]  buf            Point to the data buffer that stores the data read from flash
* @param[in]  buf_len        The length of the buffer
*
* @return  0 : On success,  otherwise is error
*/

int qm_ble_ota_flash_read( unsigned int* offset, unsigned char *buf ,int buf_len );



unsigned int qm_ble_flash_erase_sector_size(void);



void qm_ble_ota_end( int finish );



int qm_ble_ota_breakpoint_set(unsigned int break_point);

int qm_ble_ota_breakpoint_get(unsigned int break_point);





#endif

