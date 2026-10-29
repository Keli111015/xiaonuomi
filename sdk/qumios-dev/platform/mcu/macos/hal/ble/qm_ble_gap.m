#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include "qm_ble_gap.h"
#include "qm_errno.h"

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>

@interface QMMacBleGapDelegate : NSObject <CBCentralManagerDelegate, CBPeripheralManagerDelegate>
@property(nonatomic, strong) CBCentralManager *central;
@property(nonatomic, strong) CBPeripheralManager *peripheral;
@property(nonatomic, strong) NSData *advData;
@property(nonatomic, strong) NSData *scanResponseData;
@property(nonatomic, assign) qm_gap_ble_cb_t callback;
@property(nonatomic, assign) BOOL scanRequested;
@property(nonatomic, assign) BOOL advertisingRequested;
@property(nonatomic, assign) BOOL scanning;
@property(nonatomic, assign) BOOL advertising;
@property(nonatomic, assign) uint32_t scanDuration;
@end

@implementation QMMacBleGapDelegate

- (instancetype)init
{
    self = [super init];
    if (self != nil) {
        _central = [[CBCentralManager alloc] initWithDelegate:self
                                                         queue:dispatch_get_main_queue()];
        _peripheral = [[CBPeripheralManager alloc] initWithDelegate:self
                                                                queue:dispatch_get_main_queue()];
    }
    return self;
}

- (void)emit:(qm_gap_ble_cb_event_t)event
{
    if (self.callback != NULL) {
        self.callback(event, NULL);
    }
}

- (void)scanForConfiguredDuration
{
    if (self.central.state != CBManagerStatePoweredOn || !self.scanRequested) {
        return;
    }

    [self.central scanForPeripheralsWithServices:nil options:nil];
    self.scanning = YES;
    [self emit:QM_GAP_BLE_SCAN_START_COMPLETE_EVT];

    if (self.scanDuration > 0) {
        uint32_t duration = self.scanDuration;
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                      (int64_t)duration * NSEC_PER_SEC),
                       dispatch_get_main_queue(), ^{
            if (self.scanning) {
                [self stopScanning];
            }
        });
    }
}

- (void)startAdvertisingIfReady
{
    if (self.peripheral.state != CBManagerStatePoweredOn || !self.advertisingRequested) {
        return;
    }

    NSMutableDictionary *advertisement = [NSMutableDictionary dictionary];
    if (self.advData.length > 0) {
        advertisement[CBAdvertisementDataManufacturerDataKey] = self.advData;
    }

    [self.peripheral startAdvertising:advertisement];
    self.advertising = YES;
    [self emit:QM_GAP_BLE_ADV_START_COMPLETE_EVT];
}

- (void)centralManagerDidUpdateState:(CBCentralManager *)central
{
    fprintf(stderr, "macOS BLE central state: %ld\n", (long)central.state);
    if (central.state == CBManagerStatePoweredOn) {
        [self scanForConfiguredDuration];
    }
}

- (void)centralManager:(CBCentralManager *)central
 didDiscoverPeripheral:(CBPeripheral *)peripheral
     advertisementData:(NSDictionary<NSString *, id> *)advertisementData
                  RSSI:(NSNumber *)RSSI
{
    (void)central;
    if (self.callback == NULL) {
        return;
    }

    qm_ble_gap_cb_param_t param;
    memset(&param, 0, sizeof(param));

    NSInteger rssi = RSSI.integerValue;
    if (rssi < -128) {
        rssi = -128;
    } else if (rssi > 127) {
        rssi = 127;
    }
    param.scan_rst.rssi = (int8_t)rssi;

    NSData *manufacturer = advertisementData[CBAdvertisementDataManufacturerDataKey];
    NSUInteger copyLength = manufacturer.length;
    if (copyLength > sizeof(param.scan_rst.ble_adv)) {
        copyLength = sizeof(param.scan_rst.ble_adv);
    }
    if (copyLength > 0) {
        [manufacturer getBytes:param.scan_rst.ble_adv length:copyLength];
    }
    param.scan_rst.adv_data_len = (uint8_t)copyLength;
    param.scan_rst.scan_rsp_len = 0;

    /* CoreBluetooth intentionally does not expose the peer's MAC address. */
    self.callback(QM_GAP_BLE_SCAN_RESULT_EVT, &param);
    (void)peripheral;
}

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral
{
    fprintf(stderr, "macOS BLE peripheral state: %ld\n", (long)peripheral.state);
    if (peripheral.state == CBManagerStatePoweredOn) {
        [self startAdvertisingIfReady];
    }
}

- (void)stopScanning
{
    [self.central stopScan];
    self.scanRequested = NO;
    self.scanning = NO;
    [self emit:QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT];
}

- (void)stopAdvertising
{
    [self.peripheral stopAdvertising];
    self.advertisingRequested = NO;
    self.advertising = NO;
    [self emit:QM_GAP_BLE_ADV_STOP_COMPLETE_EVT];
}

@end

static QMMacBleGapDelegate *g_gap_delegate;

static NSData *mac_persistent_identity(void)
{
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSData *stored = [defaults dataForKey:@"QUMIHostBluetoothIdentity"];
    if (stored.length == QM_BD_ADDR_LEN) {
        return stored;
    }

    uuid_t uuid;
    NSMutableData *identity = [NSMutableData dataWithLength:QM_BD_ADDR_LEN];
    [[[NSUUID alloc] init] getUUIDBytes:uuid];
    memcpy(identity.mutableBytes, uuid, QM_BD_ADDR_LEN);

    /* Locally administered, unicast address-shaped identity. */
    uint8_t *bytes = (uint8_t *)identity.mutableBytes;
    bytes[0] = (uint8_t)((bytes[0] & 0xfcU) | 0x02U);
    [defaults setObject:identity forKey:@"QUMIHostBluetoothIdentity"];
    [defaults synchronize];
    return identity;
}

static QMMacBleGapDelegate *mac_gap_delegate(void)
{
    static dispatch_once_t once;

    dispatch_once(&once, ^{
        g_gap_delegate = [QMMacBleGapDelegate new];
    });
    return g_gap_delegate;
}

int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    if (callback == NULL) {
        return -QM_EINVAL;
    }
    mac_gap_delegate().callback = callback;
    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    QMMacBleGapDelegate *delegate = mac_gap_delegate();
    if (callback != NULL && delegate.callback != callback) {
        return -QM_EINVAL;
    }
    delegate.callback = NULL;
    return QM_EOK;
}

int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    if (adv_data == NULL ||
        (adv_data->manufacturer_len > 0 && adv_data->p_manufacturer_data == NULL) ||
        adv_data->manufacturer_len > QM_BLE_ADV_DATA_LEN_MAX) {
        return -QM_EINVAL;
    }

    QMMacBleGapDelegate *delegate = mac_gap_delegate();
    delegate.advData = [NSData dataWithBytes:adv_data->p_manufacturer_data
                                       length:adv_data->manufacturer_len];
    [delegate emit:QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT];
    return QM_EOK;
}

int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    if (raw_data == NULL && raw_data_len != 0) {
        return -QM_EINVAL;
    }
    if (raw_data_len > QM_BLE_ADV_DATA_LEN_MAX) {
        return -QM_EINVAL;
    }

    QMMacBleGapDelegate *delegate = mac_gap_delegate();
    delegate.advData = [NSData dataWithBytes:raw_data length:raw_data_len];
    [delegate emit:QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT];
    return QM_EOK;
}

int qm_ble_gap_set_scan_rsp_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    if (raw_data == NULL && raw_data_len != 0) {
        return -QM_EINVAL;
    }
    if (raw_data_len > QM_BLE_SCAN_RSP_DATA_LEN_MAX) {
        return -QM_EINVAL;
    }

    QMMacBleGapDelegate *delegate = mac_gap_delegate();
    delegate.scanResponseData = [NSData dataWithBytes:raw_data length:raw_data_len];
    [delegate emit:QM_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT];
    return QM_EOK;
}

int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    (void)adv_params;
    QMMacBleGapDelegate *delegate = mac_gap_delegate();
    delegate.advertisingRequested = YES;
    [delegate startAdvertisingIfReady];
    return QM_EOK;
}

int qm_ble_gap_stop_advertising(void)
{
    [mac_gap_delegate() stopAdvertising];
    return QM_EOK;
}

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    (void)scan_params;
    [mac_gap_delegate() emit:QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT];
    return QM_EOK;
}

int qm_ble_gap_start_scanning(uint32_t duration)
{
    QMMacBleGapDelegate *delegate = mac_gap_delegate();
    delegate.scanDuration = duration;
    delegate.scanRequested = YES;
    [delegate scanForConfiguredDuration];
    return QM_EOK;
}

int qm_ble_gap_stop_scanning(void)
{
    [mac_gap_delegate() stopScanning];
    return QM_EOK;
}

int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    NSData *identity;

    if (addr == NULL) {
        return -QM_EINVAL;
    }

    identity = mac_persistent_identity();
    if (identity.length != QM_BD_ADDR_LEN) {
        memset(addr, 0, QM_BD_ADDR_LEN);
        return -QM_EIO;
    }
    memcpy(addr, identity.bytes, QM_BD_ADDR_LEN);
    return QM_EOK;
}
