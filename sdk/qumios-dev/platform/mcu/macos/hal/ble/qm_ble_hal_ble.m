#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include "qm_ble_hal_ble.h"
#include "qm_ble_gap.h"
#include "qm_errno.h"

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <string.h>

#define QM_MAC_BLE_VALUE_MAX 512u

@interface QMMacBleStackDelegate : NSObject <CBPeripheralManagerDelegate>
@property(nonatomic, strong) CBPeripheralManager *peripheral;
@property(nonatomic, strong) CBMutableService *service;
@property(nonatomic, strong) CBMutableCharacteristic *rcCharacteristic;
@property(nonatomic, strong) CBMutableCharacteristic *wcCharacteristic;
@property(nonatomic, strong) CBMutableCharacteristic *icCharacteristic;
@property(nonatomic, strong) CBMutableCharacteristic *wwnrcCharacteristic;
@property(nonatomic, strong) CBMutableCharacteristic *ncCharacteristic;
@property(nonatomic, assign) qms_bt_init_t *initParam;
@property(nonatomic, assign) BOOL subscribed;
@property(nonatomic, assign) BOOL servicePublished;
@property(nonatomic, assign) BOOL servicePublishPending;
@property(nonatomic, assign) BOOL advertising;
@property(nonatomic, assign) BOOL advertisingRequested;
@property(nonatomic, assign) BOOL advertisingPending;
@property(nonatomic, strong) NSDictionary *advertisement;
@property(nonatomic, assign) void (*indicationDone)(uint8_t res);
@end

static QMMacBleStackDelegate *g_stack;

static CBUUID *mac_uuid_from_qms(qms_uuid_t *uuid)
{
    if (uuid == NULL) {
        return nil;
    }

    switch (uuid->type) {
    case QMS_UUID_TYPE_16: {
        qms_uuid_16_t *value = (qms_uuid_16_t *)uuid;
        return [CBUUID UUIDWithString:[NSString stringWithFormat:@"%04X", value->val]];
    }
    case QMS_UUID_TYPE_32: {
        qms_uuid_32_t *value = (qms_uuid_32_t *)uuid;
        return [CBUUID UUIDWithString:[NSString stringWithFormat:@"%08X", value->val]];
    }
    case QMS_UUID_TYPE_128: {
        qms_uuid_128_t *value = (qms_uuid_128_t *)uuid;
        NSMutableString *string = [NSMutableString stringWithCapacity:36];
        for (NSUInteger i = 0; i < 16; i++) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                [string appendString:@"-"];
            }
            [string appendFormat:@"%02X", value->val[i]];
        }
        return [CBUUID UUIDWithString:string];
    }
    default:
        return nil;
    }
}

static CBCharacteristicProperties mac_properties(uint8_t prop)
{
    CBCharacteristicProperties result = 0;
    if (prop & QMS_GATT_CHRC_BROADCAST) {
        result |= CBCharacteristicPropertyBroadcast;
    }
    if (prop & QMS_GATT_CHRC_READ) {
        result |= CBCharacteristicPropertyRead;
    }
    if (prop & QMS_GATT_CHRC_WRITE_WITHOUT_RESP) {
        result |= CBCharacteristicPropertyWriteWithoutResponse;
    }
    if (prop & QMS_GATT_CHRC_WRITE) {
        result |= CBCharacteristicPropertyWrite;
    }
    if (prop & QMS_GATT_CHRC_NOTIFY) {
        result |= CBCharacteristicPropertyNotify;
    }
    if (prop & QMS_GATT_CHRC_INDICATE) {
        result |= CBCharacteristicPropertyIndicate;
    }
    return result;
}

static CBAttributePermissions mac_permissions(uint8_t perm)
{
    CBAttributePermissions result = 0;
    if (perm & QMS_GATT_PERM_READ) {
        result |= CBAttributePermissionsReadable;
    }
    if (perm & QMS_GATT_PERM_WRITE) {
        result |= CBAttributePermissionsWriteable;
    }
    if (perm & QMS_GATT_PERM_READ_ENCRYPT) {
        result |= CBAttributePermissionsReadEncryptionRequired;
    }
    if (perm & QMS_GATT_PERM_WRITE_ENCRYPT) {
        result |= CBAttributePermissionsWriteEncryptionRequired;
    }
    return result;
}

static qms_char_init_t *mac_characteristic_init(CBCharacteristic *characteristic)
{
    if (g_stack == nil || g_stack.initParam == NULL) {
        return NULL;
    }
    if (characteristic == g_stack.rcCharacteristic) {
        return &g_stack.initParam->rc;
    }
    if (characteristic == g_stack.wcCharacteristic) {
        return &g_stack.initParam->wc;
    }
    if (characteristic == g_stack.icCharacteristic) {
        return &g_stack.initParam->ic;
    }
    if (characteristic == g_stack.wwnrcCharacteristic) {
        return &g_stack.initParam->wwnrc;
    }
    if (characteristic == g_stack.ncCharacteristic) {
        return &g_stack.initParam->nc;
    }
    return NULL;
}

static CBMutableCharacteristic *mac_make_characteristic(qms_char_init_t *init)
{
    CBUUID *uuid = mac_uuid_from_qms(init->uuid);
    if (uuid == nil) {
        return nil;
    }
    return [[CBMutableCharacteristic alloc] initWithType:uuid
                                              properties:mac_properties(init->prop)
                                                   value:nil
                                             permissions:mac_permissions(init->perm)];
}

static qms_err_t mac_prepare_service(qms_bt_init_t *init)
{
    CBUUID *service_uuid = mac_uuid_from_qms(init->uuid_svc);
    if (service_uuid == nil) {
        return QMS_ERR_STACK_FAIL;
    }

    g_stack.service = [[CBMutableService alloc] initWithType:service_uuid primary:YES];
    g_stack.rcCharacteristic = mac_make_characteristic(&init->rc);
    g_stack.wcCharacteristic = mac_make_characteristic(&init->wc);
    g_stack.icCharacteristic = mac_make_characteristic(&init->ic);
    g_stack.wwnrcCharacteristic = mac_make_characteristic(&init->wwnrc);
    g_stack.ncCharacteristic = mac_make_characteristic(&init->nc);

    if (g_stack.rcCharacteristic == nil ||
        g_stack.wcCharacteristic == nil ||
        g_stack.icCharacteristic == nil ||
        g_stack.wwnrcCharacteristic == nil ||
        g_stack.ncCharacteristic == nil) {
        return QMS_ERR_STACK_FAIL;
    }

    g_stack.service.characteristics = @[
        g_stack.rcCharacteristic,
        g_stack.wcCharacteristic,
        g_stack.icCharacteristic,
        g_stack.wwnrcCharacteristic,
        g_stack.ncCharacteristic,
    ];
    return QMS_ERR_SUCCESS;
}

static void mac_start_advertising_if_ready(void)
{
    if (g_stack == nil ||
        g_stack.peripheral.state != CBManagerStatePoweredOn ||
        !g_stack.servicePublished ||
        !g_stack.advertisingRequested ||
        g_stack.advertisement == nil ||
        g_stack.advertising ||
        g_stack.advertisingPending) {
        return;
    }

    g_stack.advertisingPending = YES;
    [g_stack.peripheral startAdvertising:g_stack.advertisement];
}

static void mac_publish_service_if_ready(void)
{
    if (g_stack == nil ||
        g_stack.peripheral.state != CBManagerStatePoweredOn ||
        g_stack.service == nil ||
        g_stack.servicePublished ||
        g_stack.servicePublishPending) {
        return;
    }

    g_stack.servicePublishPending = YES;
    [g_stack.peripheral addService:g_stack.service];
}

@implementation QMMacBleStackDelegate

- (instancetype)init
{
    self = [super init];
    if (self != nil) {
        _peripheral = [[CBPeripheralManager alloc] initWithDelegate:self
                                                                queue:dispatch_get_main_queue()];
    }
    return self;
}

- (void)peripheralManagerDidUpdateState:(CBPeripheralManager *)peripheral
{
    fprintf(stderr, "macOS QMS peripheral state: %ld\n", (long)peripheral.state);
    if (peripheral.state == CBManagerStatePoweredOn) {
        mac_publish_service_if_ready();
        mac_start_advertising_if_ready();
        return;
    }

    self.advertising = NO;
    self.advertisingPending = NO;
    if (peripheral.state == CBManagerStateResetting ||
        peripheral.state == CBManagerStatePoweredOff) {
        self.servicePublished = NO;
        self.servicePublishPending = NO;
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
            didAddService:(CBService *)service
                    error:(NSError *)error
{
    if (service != self.service) {
        return;
    }

    self.servicePublishPending = NO;
    if (error != nil) {
        self.servicePublished = NO;
        fprintf(stderr,
                "macOS QMS service registration failed uuid=%s error=%s\n",
                service.UUID.UUIDString.UTF8String,
                error.localizedDescription.UTF8String);
        return;
    }

    self.servicePublished = YES;
    fprintf(stderr,
            "macOS QMS service registered uuid=%s\n",
            service.UUID.UUIDString.UTF8String);
    mac_start_advertising_if_ready();
    (void)peripheral;
}

- (void)peripheralManagerDidStartAdvertising:(CBPeripheralManager *)peripheral
                                        error:(NSError *)error
{
    self.advertisingPending = NO;
    self.advertising = (error == nil);
    if (error != nil) {
        fprintf(stderr,
                "macOS QMS advertising failed error=%s\n",
                error.localizedDescription.UTF8String);
    } else {
        fprintf(stderr, "macOS QMS advertising started\n");
    }
    (void)peripheral;
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
    didReceiveReadRequest:(CBATTRequest *)request
{
    qms_char_init_t *init = mac_characteristic_init(request.characteristic);
    if (init == NULL || init->on_read == NULL) {
        [peripheral respondToRequest:request withResult:CBATTErrorRequestNotSupported];
        return;
    }

    uint8_t buffer[QM_MAC_BLE_VALUE_MAX];
    size_t length = init->on_read(buffer, (uint16_t)sizeof(buffer));
    if (length > sizeof(buffer) || request.offset > length) {
        [peripheral respondToRequest:request withResult:CBATTErrorInvalidOffset];
        return;
    }

    request.value = [NSData dataWithBytes:buffer + request.offset
                                   length:length - request.offset];
    [peripheral respondToRequest:request withResult:CBATTErrorSuccess];
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
    didReceiveWriteRequests:(NSArray<CBATTRequest *> *)requests
{
    CBATTError result = CBATTErrorSuccess;
    for (CBATTRequest *request in requests) {
        qms_char_init_t *init = mac_characteristic_init(request.characteristic);
        if (init == NULL || init->on_write == NULL || request.value == nil) {
            result = CBATTErrorRequestNotSupported;
            break;
        }

        NSData *value = request.value;
        if (request.offset > value.length) {
            result = CBATTErrorInvalidOffset;
            break;
        }
        const uint8_t *bytes = (const uint8_t *)value.bytes + request.offset;
        size_t length = value.length - request.offset;
        if (init->on_write((void *)bytes, (uint16_t)length) != length) {
            result = CBATTErrorUnlikelyError;
            break;
        }
    }

    if (requests.count > 0 && requests[0].characteristic.properties & CBCharacteristicPropertyWrite) {
        [peripheral respondToRequest:requests[0] withResult:result];
    }
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
                    central:(CBCentral *)central
 didSubscribeToCharacteristic:(CBCharacteristic *)characteristic
{
    qms_char_init_t *init = mac_characteristic_init(characteristic);
    if (init == NULL) {
        return;
    }
    if (!self.subscribed) {
        self.subscribed = YES;
        if (self.initParam->on_connected != NULL) {
            self.initParam->on_connected();
        }
    }
    if (init->on_ccc_change != NULL) {
        init->on_ccc_change((characteristic.properties & CBCharacteristicPropertyIndicate) ?
                            QMS_CCC_VALUE_INDICATE : QMS_CCC_VALUE_NOTIFY);
    }
    (void)peripheral;
    (void)central;
}

- (void)peripheralManager:(CBPeripheralManager *)peripheral
                    central:(CBCentral *)central
didUnsubscribeFromCharacteristic:(CBCharacteristic *)characteristic
{
    qms_char_init_t *init = mac_characteristic_init(characteristic);
    if (init != NULL && init->on_ccc_change != NULL) {
        init->on_ccc_change(QMS_CCC_VALUE_NONE);
    }
    self.subscribed = NO;
    if (self.initParam != NULL && self.initParam->on_disconnected != NULL) {
        self.initParam->on_disconnected();
    }
    (void)peripheral;
    (void)central;
}

- (void)peripheralManagerIsReadyToUpdateSubscribers:(CBPeripheralManager *)peripheral
{
    (void)peripheral;
}

@end

qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    if (qms_init == NULL) {
        return QMS_ERR_STACK_FAIL;
    }
    if (g_stack != nil) {
        return QMS_ERR_STACK_FAIL;
    }

    g_stack = [QMMacBleStackDelegate new];
    g_stack.initParam = qms_init;
    qms_err_t ret = mac_prepare_service(qms_init);
    if (ret != QMS_ERR_SUCCESS) {
        g_stack = nil;
        return ret;
    }
    mac_publish_service_if_ready();
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_stack_deinit(void)
{
    if (g_stack == nil) {
        return QMS_ERR_SUCCESS;
    }
    [g_stack.peripheral stopAdvertising];
    g_stack.advertisingRequested = NO;
    g_stack.advertisingPending = NO;
    g_stack.advertisement = nil;
    if (g_stack.servicePublished &&
        g_stack.peripheral.state == CBManagerStatePoweredOn) {
        [g_stack.peripheral removeService:g_stack.service];
    }
    g_stack = nil;
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    if (g_stack == nil || g_stack.ncCharacteristic == nil || !g_stack.subscribed ||
        (p_data == NULL && length != 0)) {
        return QMS_ERR_GATT_NOTIFY_FAIL;
    }
    NSData *value = [NSData dataWithBytes:p_data length:length];
    BOOL ok = [g_stack.peripheral updateValue:value
                              forCharacteristic:g_stack.ncCharacteristic
                           onSubscribedCentrals:nil];
    return ok ? QMS_ERR_SUCCESS : QMS_ERR_GATT_NOTIFY_FAIL;
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    if (g_stack == nil || g_stack.icCharacteristic == nil || !g_stack.subscribed ||
        (p_data == NULL && length != 0)) {
        return QMS_ERR_GATT_INDICATE_FAIL;
    }
    NSData *value = [NSData dataWithBytes:p_data length:length];
    BOOL ok = [g_stack.peripheral updateValue:value
                              forCharacteristic:g_stack.icCharacteristic
                           onSubscribedCentrals:nil];
    if (ok && txdone != NULL) {
        txdone(0);
    }
    return ok ? QMS_ERR_SUCCESS : QMS_ERR_GATT_INDICATE_FAIL;
}

void qm_ble_disconnect(uint8_t reason)
{
    (void)reason;
    /* CoreBluetooth does not expose a peripheral-side disconnect primitive. */
}

qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    if (g_stack == nil || adv == NULL || adv->name.name == NULL) {
        return QMS_ERR_INVALID_ADV_DATA;
    }

    NSMutableDictionary *advertisement = [NSMutableDictionary dictionary];
    advertisement[CBAdvertisementDataLocalNameKey] = [NSString stringWithUTF8String:adv->name.name];
    if (g_stack.service.UUID != nil) {
        advertisement[CBAdvertisementDataServiceUUIDsKey] = @[g_stack.service.UUID];
    }
    if (adv->vdata.len > 0) {
        if (adv->vdata.len > MAX_VENDOR_DATA_LEN) {
            return QMS_ERR_INVALID_ADV_DATA;
        }
        advertisement[CBAdvertisementDataManufacturerDataKey] =
            [NSData dataWithBytes:adv->vdata.data length:adv->vdata.len];
    }

    g_stack.advertisement = advertisement;
    g_stack.advertisingRequested = YES;
    mac_publish_service_if_ready();
    mac_start_advertising_if_ready();
    if (!g_stack.servicePublished) {
        fprintf(stderr,
                "macOS QMS advertising queued; peripheral state=%ld service_published=%d\n",
                (long)g_stack.peripheral.state,
                g_stack.servicePublished ? 1 : 0);
    } else if (!g_stack.advertising && !g_stack.advertisingPending) {
        fprintf(stderr,
                "macOS QMS advertising not started; peripheral state=%ld\n",
                (long)g_stack.peripheral.state);
    }
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_advertising_stop(void)
{
    if (g_stack == nil) {
        return QMS_ERR_SUCCESS;
    }
    [g_stack.peripheral stopAdvertising];
    g_stack.advertisingRequested = NO;
    g_stack.advertisingPending = NO;
    g_stack.advertisement = nil;
    g_stack.advertising = NO;
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    if (mac == NULL) {
        return QMS_ERR_STACK_FAIL;
    }
    return (qms_err_t)qm_ble_gap_get_local_addr(mac);
}

#ifdef EN_LONG_MTU
int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    if (att_mtu == NULL) {
        return -QM_EINVAL;
    }
    *att_mtu = 23;
    return QM_EOK;
}
#endif
