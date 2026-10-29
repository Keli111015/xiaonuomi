#import <AVFoundation/AVFoundation.h>

#include "qm_audio_permission.h"
#include "qm_errno.h"

#include <dispatch/dispatch.h>
#include <stdint.h>

int qm_macos_microphone_authorize(uint32_t timeout_ms)
{
    @autoreleasepool {
        AVAuthorizationStatus status =
            [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];

        if (status == AVAuthorizationStatusAuthorized) {
            return QM_EOK;
        }
        if (status == AVAuthorizationStatusDenied ||
            status == AVAuthorizationStatusRestricted) {
            return -QM_EIO;
        }

        __block BOOL granted = NO;
        dispatch_semaphore_t completion = dispatch_semaphore_create(0);
        dispatch_time_t deadline = timeout_ms == UINT32_MAX ?
                                   DISPATCH_TIME_FOREVER :
                                   dispatch_time(DISPATCH_TIME_NOW,
                                                 (int64_t)timeout_ms * NSEC_PER_MSEC);

        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL allowed) {
                                     granted = allowed;
                                     dispatch_semaphore_signal(completion);
                                 }];

        if (dispatch_semaphore_wait(completion, deadline) != 0) {
            return -QM_ETIMEOUT;
        }
        return granted ? QM_EOK : -QM_EIO;
    }
}
