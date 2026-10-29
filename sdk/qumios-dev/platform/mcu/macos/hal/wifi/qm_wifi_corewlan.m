#import <CoreWLAN/CoreWLAN.h>
#import <CoreLocation/CoreLocation.h>
#import <AppKit/AppKit.h>

#include "qm_wifi_corewlan.h"
#include "qm_errno.h"

#include <stdio.h>
#include <string.h>

static CLLocationManager *s_location_manager;
static BOOL s_bssid_warning_emitted;

static BOOL location_authorized(CLAuthorizationStatus status)
{
    return status == kCLAuthorizationStatusAuthorized;
}

static CLAuthorizationStatus location_status(void)
{
    if (@available(macOS 11.0, *)) {
        return s_location_manager.authorizationStatus;
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return [CLLocationManager authorizationStatus];
#pragma clang diagnostic pop
}

static void activate_application(void)
{
    NSApplication *application = [NSApplication sharedApplication];

    (void)[application setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [application finishLaunching];
    if (@available(macOS 14.0, *)) {
        [application activate];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [application activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}

int qm_macos_wifi_authorize_location(uint32_t timeout_ms)
{
    @autoreleasepool {
        CLAuthorizationStatus status;
        NSDate *deadline;

        if (![NSThread isMainThread] || timeout_ms == 0U) {
            return -QM_EINVAL;
        }

        if (s_location_manager == nil) {
            s_location_manager = [[CLLocationManager alloc] init];
        }

        activate_application();
        status = location_status();
        if (location_authorized(status)) {
            return QM_EOK;
        }
        if (status == kCLAuthorizationStatusDenied ||
            status == kCLAuthorizationStatusRestricted) {
            return -QM_EIO;
        }

        [s_location_manager requestWhenInUseAuthorization];

        deadline = [NSDate dateWithTimeIntervalSinceNow:(NSTimeInterval)timeout_ms / 1000.0];
        while ([deadline timeIntervalSinceNow] > 0.0) {
            [[NSRunLoop mainRunLoop]
                runMode:NSDefaultRunLoopMode
                beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            status = location_status();
            if (location_authorized(status)) {
                return QM_EOK;
            }
            if (status == kCLAuthorizationStatusDenied ||
                status == kCLAuthorizationStatusRestricted) {
                return -QM_EIO;
            }
        }
        return -QM_ETIMEOUT;
    }
}

static int parse_bssid(NSString *value, uint8_t bssid[6])
{
    unsigned int bytes[6];

    if (value == nil || bssid == NULL ||
        sscanf(value.UTF8String,
               "%x:%x:%x:%x:%x:%x",
               &bytes[0], &bytes[1], &bytes[2],
               &bytes[3], &bytes[4], &bytes[5]) != 6) {
        return -QM_EINVAL;
    }

    for (size_t i = 0; i < 6; ++i) {
        if (bytes[i] > UINT8_MAX) {
            return -QM_EINVAL;
        }
        bssid[i] = (uint8_t)bytes[i];
    }
    return QM_EOK;
}

int qm_macos_wifi_get_ap_info(const char *interface_name,
                              qm_wifi_ap_record_t *info)
{
    @autoreleasepool {
        NSString *name;
        CWWiFiClient *client;
        CWInterface *interface;
        NSString *ssid;
        NSString *bssid;
        CWChannel *channel;
        NSInteger rssi;
        size_t ssid_len;

        if (interface_name == NULL || info == NULL) {
            return -QM_EINVAL;
        }

        name = [NSString stringWithUTF8String:interface_name];
        client = [CWWiFiClient sharedWiFiClient];
        interface = [client interfaceWithName:name];
        if (interface == nil) {
            return -QM_ENOENT;
        }

        ssid = interface.ssid;
        if (ssid != nil) {
            const char *ssid_text = ssid.UTF8String;
            if (ssid_text != NULL) {
                ssid_len = strlen(ssid_text);
                if (ssid_len <= CONFIG_QM_WIFI_SSID_MAX_LEN) {
                    memcpy(info->ssid, ssid_text, ssid_len);
                    info->ssid[ssid_len] = '\0';
                    info->ssid_len = (uint8_t)ssid_len;
                }
            }
        }

        bssid = interface.bssid;
        (void)parse_bssid(bssid, info->bssid);
        if (bssid == nil && !s_bssid_warning_emitted) {
            fprintf(stderr,
                    "qm-macos-wifi: CoreWLAN hid BSSID (location status=%ld); "
                    "allow Location Services for QUMI JoyInside\n",
                    (long)location_status());
            s_bssid_warning_emitted = YES;
        }

        rssi = interface.rssiValue;
        if (rssi < INT8_MIN) {
            rssi = INT8_MIN;
        } else if (rssi > INT8_MAX) {
            rssi = INT8_MAX;
        }
        info->rssi = (int8_t)rssi;

        channel = interface.wlanChannel;
        if (channel != nil && channel.channelNumber <= UINT8_MAX) {
            info->channel = (uint8_t)channel.channelNumber;
        }

        return info->ssid_len != 0U || info->channel != 0U ||
                       info->bssid[0] != 0U || info->bssid[1] != 0U ||
                       info->bssid[2] != 0U || info->bssid[3] != 0U ||
                       info->bssid[4] != 0U || info->bssid[5] != 0U ?
                   QM_EOK : -QM_ENOENT;
    }
}
