#include "honor_protocol_ota.h"
#include <ctype.h>
#include "mbedtls/sha256.h"


static uint32_t g_OtaPkgSize = 0;
static uint8_t *g_OtaPkgSha256 = NULL;
static OtaUpgradeType g_otaType = OTA_UPGRADE_BUTT;

/* ota缓存4K数据再写入，避免频繁写入flash */
static unsigned char *g_pkgBuffer = NULL;

/* 模组ota注册函数 */
static OtaGetCurFwVerFunc g_otaGetCurFwVerFunc = NULL;
static OtaWritePkgFunc g_otaPackageRecvFunc = NULL;
static OtaProcInstallFunc g_otaProcInstallFunc = NULL;
static GetBatteryLevelFunc g_getBatteryLevelFunc = NULL;
static GetPkgSaveSizeFunc g_getPkgSaveSizeFunc = NULL;

/* MCU ota注册函数 */
static McuOtaEndFunc g_mcuOtaEndFunc = NULL;
static McuOtaStartFunc g_mcuOtaStartFunc = NULL;
static OtaGetCurMcuVerFunc g_otaGetCurMcuVerFunc = NULL;
static McuOtaPackageRecvFunc g_mcuOtaPackageRecvFunc = NULL;
static McuOtaProcInstallFunc g_mcuOtaProcInstallFunc = NULL;
static NotifyMcuOtaExceptionFunc g_notifyMcuOtaExceptionFunc = NULL;

/* 协议注册接口 */
static SendFrame2AppFunc g_sendFrame2AppFunc = NULL;
static GetUint8ArrayFromTLVFunc g_getUint8ArrayFromTLVFunc = NULL;

/* 注册模组ota回调 */
void RegisterOtaGetCurFwVerFunc(OtaGetCurFwVerFunc otaGetCurFwVer)
{
    if (otaGetCurFwVer == NULL)
    {
        qm_printf("set ota get cur fw ver func error\n");
        return;
    }
    g_otaGetCurFwVerFunc = otaGetCurFwVer;
}

void RegisterOtaWritePkgFunc(OtaWritePkgFunc otaWritePkgFunc)
{
    if (otaWritePkgFunc == NULL)
    {
        qm_printf("set ota recv pkg func error\n");
        return;
    }
    g_otaPackageRecvFunc = otaWritePkgFunc;
}

void RegisterOtaProcInstallFunc(OtaProcInstallFunc otaProcInstallFunc)
{
    if (otaProcInstallFunc == NULL)
    {
        qm_printf("set ota recv pkg func error\n");
        return;
    }
    g_otaProcInstallFunc = otaProcInstallFunc;
}

void RegisterGetPkgSaveSizeFunc(GetPkgSaveSizeFunc getPkgSaveSizeFunc)
{
    if (getPkgSaveSizeFunc == NULL)
    {
        qm_printf("set ota recv pkg func error\n");
        return;
    }
    g_getPkgSaveSizeFunc = getPkgSaveSizeFunc;
}

void RegisterGetBatteryLevelFunc(GetBatteryLevelFunc getBatteryLevelFunc)
{
    if (getBatteryLevelFunc == NULL)
    {
        qm_printf("set get battery level func error\n");
        return;
    }
    g_getBatteryLevelFunc = getBatteryLevelFunc;
}

/* 注册MCU ota回调 */
void RegisterMcuOtaEndFunc(McuOtaEndFunc mcuOtaEndFunc)
{
    if (mcuOtaEndFunc == NULL)
    {
        qm_printf("set ota get cur mcu ver func error\n");
        return;
    }
    g_mcuOtaEndFunc = mcuOtaEndFunc;
}

void RegisterMcuOtaStartFunc(McuOtaStartFunc mcuOtaStartFunc)
{
    if (mcuOtaStartFunc == NULL)
    {
        qm_printf("set mcu ota start func error\n");
        return;
    }
    g_mcuOtaStartFunc = mcuOtaStartFunc;
}

void RegisterOtaGetCurMcuVerFunc(OtaGetCurMcuVerFunc otaGetCurMcuVer)
{
    if (otaGetCurMcuVer == NULL)
    {
        qm_printf("set ota get cur mcu ver func error\n");
        return;
    }
    g_otaGetCurMcuVerFunc = otaGetCurMcuVer;
}

void RegisterMcuOtaRecvPkgFunc(McuOtaPackageRecvFunc mcuOtaPackageRecvFunc)
{
    if (mcuOtaPackageRecvFunc == NULL)
    {
        qm_printf("set mcu ota recv pkg func error\n");
        return;
    }
    g_mcuOtaPackageRecvFunc = mcuOtaPackageRecvFunc;
}

void RegisterMcuOtaProcInstallFunc(McuOtaProcInstallFunc mcuOtaProcInstallFunc)
{
    if (mcuOtaProcInstallFunc == NULL)
    {
        qm_printf("set mcu ota install func error\n");
        return;
    }
    g_mcuOtaProcInstallFunc = mcuOtaProcInstallFunc;
}

void RegisterNotifyMcuOtaExceptionFunc(NotifyMcuOtaExceptionFunc notifyMcuOtaExceptionFunc)
{
    if (notifyMcuOtaExceptionFunc == NULL)
    {
        qm_printf("set notify mcu ota exception func error\n");
        return;
    }
    g_notifyMcuOtaExceptionFunc = notifyMcuOtaExceptionFunc;
}

/* 协议注册接口 */
void RegisterSendFrame2AppFunc(SendFrame2AppFunc sendFrame2AppFunc)
{
    if (sendFrame2AppFunc == NULL)
    {
        qm_printf("set send frame to app func error\n");
        return;
    }
    g_sendFrame2AppFunc = sendFrame2AppFunc;
}

void RegisterGetUint8ArrayFromTLVFunc(GetUint8ArrayFromTLVFunc getUint8ArrayFromTLVFunc)
{
    if (getUint8ArrayFromTLVFunc == NULL)
    {
        qm_printf("set get uint8 array func error\n");
        return;
    }
    g_getUint8ArrayFromTLVFunc = getUint8ArrayFromTLVFunc;
}

static int8_t CheckButteryLevel()
{
    uint8_t batteryLevel = g_getBatteryLevelFunc();
    if (batteryLevel < BATTERY_UPGRADE_THRESHOLD)
    {
        return -1;
    }

    return 0;
}

static int8_t CheckPkgSaveSize(uint32_t pkgSize)
{
    qm_printf("[CheckPkgSaveSize]pkgSize=%u\n", pkgSize);
    uint32_t pkgSaveSize = g_getPkgSaveSizeFunc();
    if (pkgSaveSize == 0)
    {
        qm_printf("save size zero\n");
        return -1;
    }

    if (pkgSaveSize < pkgSize)
    {
        qm_printf("save size error\n");
        return -1;
    }

    return 0;
}

static int8_t SavePackageSha256(const uint8_t *sha256Value)
{
    if (g_OtaPkgSha256 != NULL)
    {
        qm_free(g_OtaPkgSha256);
        g_OtaPkgSha256 = NULL;
    }
    g_OtaPkgSha256 = (uint8_t *)qm_malloc(SHA256_VALUE_LEN);
    if (g_OtaPkgSha256 == NULL)
    {
        qm_printf("qm_malloc err\n");
        return -1;
    }
    (void)memset(g_OtaPkgSha256, 0, SHA256_VALUE_LEN);
    (void)memcpy(g_OtaPkgSha256, sha256Value, SHA256_VALUE_LEN);

    return 0;
}

static OtaErrCode OtaStart(uint8_t type, const uint8_t *version, uint8_t versionLen, uint32_t pkgSize, const uint8_t *sha256Value)
{
    qm_printf("ota start %s|%u\n", type == OTA_UPGRADE_MODULE ? "moudule" : "mcu", pkgSize);

    if (CheckButteryLevel() != 0)
    {
        qm_printf("battery low\n");
        return OTA_BATTERY_TOO_LOW;
    }

    g_otaType = type;
    g_OtaPkgSize = pkgSize;

    if (SavePackageSha256(sha256Value) != 0)
    {
        qm_printf("save sha256 fail\n");
        return OTA_DEVICE_PROCESS_FAIL;
    }

    if (g_otaType == OTA_UPGRADE_MODULE)
    {
        if (CheckPkgSaveSize(pkgSize) != 0)
        {
            qm_printf("space error\n");
            return OTA_PACKAGE_TOO_LARGE;
        }
    }

    if (g_otaType == OTA_UPGRADE_MCU)
    {
        if (g_mcuOtaStartFunc((char *)version, versionLen, g_OtaPkgSize, sha256Value,
                              SHA256_VALUE_LEN) != 0)
        {
            qm_printf("start\n");
            return OTA_MCU_PREPARE_OTA_FAIL;
        }
    }
    return OTA_OK;
}

static int8_t TransHexToStr(const unsigned char *src, unsigned int srcLen, char *dst, unsigned int dstLen)
{
    if (srcLen * 2 + 1 > dstLen)
    {
        qm_printf("len\n");
        return -1;
    }

    for (int i = 0; i < srcLen; i++)
    {
        if (snprintf(dst + strlen(dst), dstLen - strlen(dst), "%02x", src[i]) < 0)
        {
            qm_printf("sprintf\n");
            return -1;
        }
    }
    return 0;
}

/* 小写字母转换为大写字母 */
static void TranslateLowerToUpper(char *buffer)
{
    for (unsigned int i = 0; i < strlen(buffer); ++i)
    {
        buffer[i] = toupper(buffer[i]);
    }
    return;
}

static int8_t UpdatePkgCheckSHA256(const unsigned char *buffer, const unsigned int bufLen,
                                   const DownLoadPackageType downloadType)
{
    static mbedtls_sha256_context *sha256Context = NULL;
    if (downloadType == DOWNLOAD_PACKAGE_FIRST)
    {
        if (sha256Context != NULL)
        {
            mbedtls_sha256_free(sha256Context);
            qm_free(sha256Context);
            sha256Context = NULL;
        }

        sha256Context = qm_malloc(sizeof(mbedtls_sha256_context));
        if (sha256Context == NULL)
        {
            qm_printf("qm_malloc error\n");
            return -1;
        }

        mbedtls_sha256_init(sha256Context);

        mbedtls_sha256_starts(sha256Context, false);
    }

    if (sha256Context == NULL)
    {
        qm_printf("sha256Context is not init, error\n");
        return -1;
    }

    mbedtls_sha256_update(sha256Context, buffer, bufLen);

    /* 最后一包 */
    if (downloadType == DOWNLOAD_PACKAGE_LAST)
    {
        unsigned char tempCalRet[32] = {0};
        mbedtls_sha256_finish(sha256Context, tempCalRet);

        char calRet[SHA256_VALUE_LEN + 1] = {0};
        if (TransHexToStr(tempCalRet, 32, calRet, sizeof(calRet)) != 0)
        {
            qm_printf("convert error\n");
            goto FAIL;
        }

        mbedtls_sha256_free(sha256Context);
        qm_free(sha256Context);
        sha256Context = NULL;

        TranslateLowerToUpper(calRet);

        if (g_OtaPkgSha256 == NULL)
        {
            qm_printf("g_OtaPkgSha256 is null\n");
            return -1;
        }
        qm_printf("g_OtaPkgSha256 = [%s]\n", g_OtaPkgSha256);
        if (memcmp(calRet, g_OtaPkgSha256, SHA256_VALUE_LEN) != 0)
        {
            qm_printf("compare target sha256 error\n");
            return -1;
        }
    }

    return 0;
FAIL:
    mbedtls_sha256_free(sha256Context);
    qm_free(sha256Context);
    sha256Context = NULL;
    return -1;
}

static int8_t OtaPackageCheck(void)
{
    int8_t res = UpdatePkgCheckSHA256((unsigned char *)"", 0, DOWNLOAD_PACKAGE_LAST);

    if (g_otaType == OTA_UPGRADE_MCU)
    {
        if (res != 0)
        {
            g_notifyMcuOtaExceptionFunc(-1, "package sha256 check fail\n");
        }
        g_mcuOtaEndFunc();
    }

    g_OtaPkgSize = 0;
    if (g_OtaPkgSha256 != NULL)
    {
        qm_free(g_OtaPkgSha256);
        g_OtaPkgSha256 = NULL;
    }
    qm_printf("ota end res %d\n", res);
    return res;
}

static int8_t StopOtaProcess()
{
    if (g_otaType == OTA_UPGRADE_MCU)
    {
        g_notifyMcuOtaExceptionFunc(-1, "user cancle upgrade\n");
    }

    g_OtaPkgSize = 0;
    if (g_OtaPkgSha256 != NULL)
    {
        qm_free(g_OtaPkgSha256);
        g_OtaPkgSha256 = NULL;
    }
    g_otaType = OTA_UPGRADE_BUTT;
    return 0;
}

static int8_t BuildOtaErrorCodeRsp(OtaFrame *otaFrame, OtaErrCode errCode)
{
    uint16_t tlvLen = BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + sizeof(errCode);
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc error code error\n");
        return -1;
    }
    uint8_t pos = 0;
    tlv[pos++] = BLE_OTA_ERRCODE_TYPE;
    tlv[pos++] = sizeof(errCode);
    for (uint8_t i = 0; i < sizeof(errCode); ++i)
    {
        tlv[pos++] = ((errCode >> (8 * (sizeof(errCode) - i - 1))) & 0x000000FF);
    }

    otaFrame->data = tlv;
    otaFrame->dataLen = tlvLen;
    return 0;
}

static int8_t ProcOtaGetCurVer(OtaFrame *otaFrame)
{
    uint16_t pos = 0;
    /* 升级类型, MCU or 模组 */
    pos += BLE_OTA_PROTOCOL_TYPE_LEN;
    pos += BLE_OTA_PROTOCOL_LENGTH_LEN;
    uint8_t type = otaFrame->data[pos++];

    OtaErrCode errcode = OTA_OK;
    uint8_t *tlv = NULL;
    uint8_t *fwVer = NULL;
    uint8_t fwVerLen = 0;
    uint8_t *mcuVer = NULL;
    uint8_t mcuVerLen = 0;

    /* 获取当前固件版本号 */
    if (type == OTA_UPGRADE_MUC_MODULE || type == OTA_UPGRADE_MODULE)
    {
        if (g_otaGetCurFwVerFunc(&fwVer, &fwVerLen) != 0)
        {
            qm_printf("get fw ver fail\n");
            errcode = OTA_GET_FIRMVARE_VERSION_FAIL;
            goto FAIL;
        }
    }
    /* 获取当前MCU版本号 */
    if (type == OTA_UPGRADE_MUC_MODULE || type == OTA_UPGRADE_MCU)
    {
        if (g_otaGetCurMcuVerFunc(&mcuVer, &mcuVerLen) != 0)
        {
            qm_printf("get fw ver fail\n");
            errcode = OTA_GET_MCU_VERSION_FAIL;
            goto FAIL;
        }
    }

    pos = 0;
    uint8_t tlvLen = (fwVerLen == 0 ? 0 : (BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + fwVerLen));
    tlvLen += (mcuVerLen == 0 ? 0 : (BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + mcuVerLen));
    tlv = qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc fail\n");
        errcode = OTA_DEVICE_PROCESS_FAIL;
        goto FAIL;
    }

    if (fwVerLen != 0 && fwVer != NULL)
    {
        tlv[pos++] = BLE_OTA_MESSAGE_TYPE_2;
        tlv[pos++] = fwVerLen;
        (void)memcpy(tlv + pos, fwVer, fwVerLen);
        pos += fwVerLen;
        qm_free(fwVer);
    }
    if (mcuVerLen != 0 && mcuVer != NULL)
    {
        tlv[pos++] = BLE_OTA_MESSAGE_TYPE_3;
        tlv[pos++] = mcuVerLen;
        (void)memcpy(tlv + pos, mcuVer, mcuVerLen);
        qm_free(mcuVer);
    }

    otaFrame->data = tlv;
    otaFrame->dataLen = tlvLen;
    return 0;

FAIL:
    if (tlv != NULL)
    {
        qm_free(tlv);
    }
    if (fwVer != NULL)
    {
        qm_free(fwVer);
    }
    if (mcuVer != NULL)
    {
        qm_free(mcuVer);
    }
    BuildOtaErrorCodeRsp(otaFrame, errcode);
    return 0;
}

static int8_t ProcOtaPrepare(OtaFrame *otaFrame)
{
    uint16_t pos = 0;
    /* 升级类型, MCU or 模组 */
    pos += BLE_OTA_PROTOCOL_TYPE_LEN;
    pos += BLE_OTA_PROTOCOL_LENGTH_LEN;
    uint8_t type = otaFrame->data[pos++];

    /* 版本包大小 */
    uint8_t *pkgSizeBuf = NULL;
    uint16_t pkgSizeBufLen = 0;
    g_getUint8ArrayFromTLVFunc(otaFrame->data, &pos, &pkgSizeBuf, &pkgSizeBufLen);
    uint32_t pkgSize = 0;
    for (uint8_t i = 0; i < pkgSizeBufLen; ++i)
    {
        pkgSize += ((uint32_t)pkgSizeBuf[i] << ((pkgSizeBufLen - i - 1) * 8));
    }

    /* 版本号 */
    uint8_t *versionBuf = NULL;
    uint16_t versionBufLen = 0;
    g_getUint8ArrayFromTLVFunc(otaFrame->data, &pos, &versionBuf, &versionBufLen);
    /* sha256值 */
    uint8_t *sha256Buf = NULL;
    uint16_t sha256BufLen = 0;
    g_getUint8ArrayFromTLVFunc(otaFrame->data, &pos, &sha256Buf, &sha256BufLen);

    OtaErrCode ret = OTA_OK;
    if (sha256BufLen != SHA256_VALUE_LEN)
    {
        ret = OTA_FIRMVARE_SHA256_CHECK_FAIL;
        goto OUT;
    }

    ret = OtaStart(type, versionBuf, versionBufLen, pkgSize, sha256Buf);

OUT:
    qm_free(pkgSizeBuf);
    qm_free(versionBuf);
    qm_free(sha256Buf);

    BuildOtaErrorCodeRsp(otaFrame, ret);
    return 0;
}

static int8_t ProcOtaParNegotiation(OtaFrame *otaFrame)
{
    uint8_t tlvLen = 0;
    tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_TWO;

    uint8_t pos = 0;
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc param negotiate rsp error\n");
        return -1;
    }
    tlv[pos++] = BLE_OTA_MESSAGE_TYPE_1; /* 校验等待时长 */
    tlv[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = SHA256_CHECK_TIMER_LEN >> 8;
    tlv[pos++] = SHA256_CHECK_TIMER_LEN & 0xff;
    tlv[pos++] = BLE_OTA_MESSAGE_TYPE_2; /* 重启等待时长 */
    tlv[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = OTA_RESTART_TIMER_LEN >> 8;
    tlv[pos++] = OTA_RESTART_TIMER_LEN & 0xff;
    tlv[pos++] = BLE_OTA_MESSAGE_TYPE_3; /* 手机推包时间间隔 */
    tlv[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = OTA_PUSH_PACKAGE_IINTERVAL >> 8;
    tlv[pos++] = OTA_PUSH_PACKAGE_IINTERVAL & 0xff;
    tlv[pos++] = BLE_OTA_MESSAGE_TYPE_4; /* 手机推包方式 */
    tlv[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    tlv[pos++] = OTA_PACKAGE_PUSH;
    tlv[pos++] = BLE_OTA_MESSAGE_TYPE_5; /* OTA单包数据长度 */
    tlv[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = OTA_PER_PACKAGE_SIZE >> 8;
    tlv[pos++] = OTA_PER_PACKAGE_SIZE & 0xff;

    otaFrame->data = tlv;
    otaFrame->dataLen = tlvLen;
    return 0;
}

static void ProcOtaStop(OtaFrame *otaFrame)
{
    if (g_pkgBuffer != NULL)
    {
        qm_free(g_pkgBuffer);
        g_pkgBuffer = NULL;
    }
    int8_t stopResult = StopOtaProcess();
    BuildOtaErrorCodeRsp(otaFrame, (stopResult == 0) ? OTA_OK : OTA_DEVICE_PROCESS_FAIL);
    return;
}

void ReportOtaError(OtaErrCode errCode)
{
    qm_printf("[ReportOtaError]errCode=%d\n", errCode);
    OtaFrame otaFrame = {0};
    otaFrame.sid = BLE_DEVICE_OTA_SERVICE;
    otaFrame.cid = OTA_CID_EXCEPTION_REPORT;

    /* 上报errcode时，head长度为0 */
    uint8_t head = 0x00;
    otaFrame.head = &head;
    otaFrame.headLen = sizeof(head);

    uint8_t dataLen = BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + sizeof(errCode);
    uint8_t *data = (uint8_t *)qm_malloc(dataLen);
    if (data == NULL)
    {
        qm_printf("report ota error qm_malloc fail\n");
        return;
    }

    uint8_t pos = 0;
    data[pos++] = BLE_OTA_ERRCODE_TYPE;
    data[pos++] = sizeof(errCode);
    for (uint8_t count = 0; count < sizeof(errCode); ++count)
    {
        data[pos++] = ((errCode >> (8 * (sizeof(errCode) - count - 1))) & 0x000000FF);
    }

    otaFrame.data = data;
    otaFrame.dataLen = dataLen;

    g_sendFrame2AppFunc(&otaFrame);
    qm_free(data);
    return;
}

static void SendOtaPkgCheckResult(OtaPackageCheckResult checkResult)
{
    qm_printf("[SendOtaPkgCheckResult]checkResult=%d\n", checkResult);
    OtaFrame otaFrame = {0};
    otaFrame.sid = BLE_DEVICE_OTA_SERVICE;
    otaFrame.cid = OTA_CID_PACKAGE_CHECK_ACK;
    static uint32_t reqId = 1;

    uint8_t headBufLen = BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + sizeof(reqId);
    uint8_t *head = (uint8_t *)qm_malloc(headBufLen);
    if (head == NULL)
    {
        qm_printf("send ota check result qm_malloc head fail\n");
        return;
    }
    uint8_t headLen = BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + sizeof(reqId);
    uint8_t pos = 0;
    head[pos++] = headLen;
    head[pos++] = BLE_OTA_PROTOCOL_TYPE_LEN;
    head[pos++] = sizeof(reqId);
    for (uint8_t count = 0; count < 4; count++)
    {
        head[pos++] = (reqId >> (8 * (4 - count - 1))) & 0x000000FF;
    }

    otaFrame.head = head;
    otaFrame.headLen = headBufLen;

    uint8_t tlvLen = 0;
    tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    if (checkResult != OTA_PACKAGE_CHECK_SUCCESS)
    {
        tlvLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + sizeof(OtaErrCode);
    }

    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_free(head);
        qm_printf("send ota check result qm_malloc tlv fail\n");
        return;
    }
    pos = 0;
    /* 校验结果 */
    tlv[pos++] = BLE_OTA_MESSAGE_TYPE_1;
    tlv[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    tlv[pos++] = checkResult;

    /* 校验错误码 */
    if (checkResult != OTA_PACKAGE_CHECK_SUCCESS)
    {
        uint32_t errcode = OTA_FIRMVARE_SHA256_CHECK_FAIL;
        tlv[pos++] = BLE_OTA_ERRCODE_TYPE;
        tlv[pos++] = sizeof(errcode);
        for (uint8_t count = 0; count < 4; count++)
        {
            tlv[pos++] = (errcode >> (8 * (4 - count - 1))) & 0x000000FF;
        }
    }

    otaFrame.data = tlv;
    otaFrame.dataLen = tlvLen;
    g_sendFrame2AppFunc(&otaFrame);
    qm_free(head);
    qm_free(tlv);
    return;
}

static int8_t OtaRecvPkg(unsigned int offset, const uint8_t *pkgData, uint32_t pkgDataLen)
{
    if (pkgData == NULL || pkgDataLen == 0)
    {
        qm_printf("param\n");
        goto FAIL;
    }

    const DownLoadPackageType pkgBufType = (offset == 0) ? DOWNLOAD_PACKAGE_FIRST : DOWNLOAD_PACKAGE_MIDDLE;
    if (UpdatePkgCheckSHA256((unsigned char *)pkgData, pkgDataLen, pkgBufType) != 0)
    {
        qm_printf("check buffer error\n");
        goto FAIL;
    }

    if (g_otaType == OTA_UPGRADE_MCU)
    {
        if (g_mcuOtaPackageRecvFunc(offset, pkgData, pkgDataLen) != 0)
        {
            qm_printf("mcu\n");
            goto FAIL;
        }
        return 0;
    }

    if (g_otaType == OTA_UPGRADE_MODULE)
    {
        static uint32_t currentPkgLen = 0;
        if (g_pkgBuffer == NULL)
        {
            g_pkgBuffer = qm_malloc(FLASH_SEC_SIZE);

            if (g_pkgBuffer == NULL)
            {
                goto FAIL;
            }
            (void)memset(g_pkgBuffer, 0, FLASH_SEC_SIZE);
        }

        if (offset == 0)
        {
            currentPkgLen = 0;
            (void)memset(g_pkgBuffer, 0, FLASH_SEC_SIZE);
        }

        if (currentPkgLen + pkgDataLen > FLASH_SEC_SIZE || offset + pkgDataLen > g_OtaPkgSize)
        {
            qm_printf("pacake data too long\n");
            goto FAIL;
        }
        (void)memcpy(g_pkgBuffer + currentPkgLen, pkgData, pkgDataLen);
        currentPkgLen += pkgDataLen;

        if (currentPkgLen == FLASH_SEC_SIZE || offset + pkgDataLen == g_OtaPkgSize)
        {
            if (g_otaPackageRecvFunc(offset + pkgDataLen - currentPkgLen, g_pkgBuffer, currentPkgLen) != 0)
            {
                qm_printf("moudle\n");
                goto FAIL;
            }
            currentPkgLen = 0;
            (void)memset(g_pkgBuffer, 0, FLASH_SEC_SIZE);

            if (offset + pkgDataLen == g_OtaPkgSize)
            {
                qm_free(g_pkgBuffer);
                g_pkgBuffer = NULL;
            }
        }
        return 0;
    }
    qm_printf("type error:%d\n", g_otaType);

FAIL:
    if (g_otaType == OTA_UPGRADE_MCU)
    {
        g_notifyMcuOtaExceptionFunc(-1, "recv mcu package fail\n");
    }
    g_otaType = OTA_UPGRADE_BUTT;
    if (g_pkgBuffer != NULL)
    {
        qm_free(g_pkgBuffer);
        g_pkgBuffer = NULL;
    }
    return -1;
}

static int8_t ProcOtaPackageRecv(OtaFrame *otaFrame)
{
    uint16_t pos = 0;
    /* 是否最后一包标识 */
    pos += BLE_OTA_PROTOCOL_TYPE_LEN;
    pos += BLE_OTA_PROTOCOL_LENGTH_LEN;
    uint8_t lastPackageFlag = otaFrame->data[pos++];

    /* 版本包偏移 */
    uint8_t *offsetBuf = NULL;
    uint16_t offsetBufLen = 0;
    uint32_t offset = 0;
    g_getUint8ArrayFromTLVFunc(otaFrame->data, &pos, &offsetBuf, &offsetBufLen);
    for (uint8_t i = 0; i < offsetBufLen; ++i)
    {
        offset += ((uint32_t)offsetBuf[i] << ((offsetBufLen - i - 1) * 8));
    }

    /* 版本包数据 */
    uint8_t *packageBuf = NULL;
    uint16_t packageBufLen = 0;
    g_getUint8ArrayFromTLVFunc(otaFrame->data, &pos, &packageBuf, &packageBufLen);

    int8_t res = -1;
    if (OtaRecvPkg(offset, packageBuf, packageBufLen) != 0)
    {
        ReportOtaError(OTA_SAVE_PACKAGE_FAIL);
        goto EXIT;
    }

    if (lastPackageFlag == true)
    {
        qm_printf("recv %s last pkg\n", g_otaType == OTA_UPGRADE_MODULE ? "moudule" : "mcu");
        int8_t ret = OtaPackageCheck();
        SendOtaPkgCheckResult(ret == 0 ? OTA_PACKAGE_CHECK_SUCCESS : OTA_PACKAGE_CHECK_FAIL);
    }

    res = 0;
EXIT:
    if (offsetBuf != NULL)
    {
        qm_free(offsetBuf);
    }
    if (packageBuf != NULL)
    {
        qm_free(packageBuf);
    }
    otaFrame->data = NULL;
    return res;
}

static int8_t ReportUpgradeStatus(uint8_t otaType, uint8_t otaState, int res)
{
    qm_printf("[ReportUpgradeStatus]res=%d\n", res);
    uint8_t head = 0x00;
    OtaFrame otaFrame = {0};
    otaFrame.sid = BLE_DEVICE_OTA_SERVICE;
    otaFrame.cid = OTA_OTA_STATUS_REPORT;
    otaFrame.head = &head;
    otaFrame.headLen = sizeof(head);

    uint8_t dataLen = 0;
    dataLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    dataLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    dataLen += BLE_OTA_PROTOCOL_TYPE_LEN + BLE_OTA_PROTOCOL_LENGTH_LEN + sizeof(res);
    uint8_t *data = qm_malloc(dataLen);
    uint8_t pos = 0;
    /* ota_type, 当前只上报MCU升级结束 */
    data[pos++] = BLE_OTA_MESSAGE_TYPE_1;
    data[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    data[pos++] = otaType;

    /* state, 当前只上报MCU升级结束 */
    data[pos++] = BLE_OTA_MESSAGE_TYPE_2;
    data[pos++] = BLE_OTA_PROTOCOL_VALUE_LEN_ONE;
    data[pos++] = otaState;

    /* errcode */
    data[pos++] = BLE_OTA_ERRCODE_TYPE;
    data[pos++] = sizeof(res);
    for (uint8_t count = 0; count < sizeof(res); ++count)
    {
        data[pos++] = ((res >> (8 * (4 - count - 1))) & 0x000000FF);
    }

    otaFrame.data = data;
    otaFrame.dataLen = pos;
    g_sendFrame2AppFunc(&otaFrame);
    qm_free(data);
    return 0;
}

static int8_t ProcOtaInstall(OtaFrame *otaFrame)
{
    if (g_pkgBuffer != NULL)
    {
        qm_free(g_pkgBuffer);
        g_pkgBuffer = NULL;
    }

    otaFrame->data = NULL;

    if (g_otaType == OTA_UPGRADE_MCU)
    {
        if (g_mcuOtaProcInstallFunc() != 0)
        {
            qm_printf("mcu install\n");
            ReportUpgradeStatus(OTA_UPGRADE_MCU, OTA_STATUS_UPGRADE_DONE, OTA_MCU_UPGRADE_FAIL);
            return -1;
        }
        ReportUpgradeStatus(OTA_UPGRADE_MCU, OTA_STATUS_UPGRADE_DONE, OTA_OK);
        return 0;
    }

    if (g_otaType == OTA_UPGRADE_MODULE)
    {
        if (g_otaProcInstallFunc() != 0)
        {
            qm_printf("moudle install\n");
            return -1;
        }
        return 0;
    }

    qm_printf("type error\n");
    return -1;
}

static bool IsOtaFuncRegisted()
{
    if (g_otaGetCurFwVerFunc == NULL || g_otaPackageRecvFunc == NULL || g_otaProcInstallFunc == NULL || g_getBatteryLevelFunc == NULL || g_getPkgSaveSizeFunc == NULL)
    {
        return false;
    }

    if (g_mcuOtaEndFunc == NULL || g_mcuOtaStartFunc == NULL || g_otaGetCurMcuVerFunc == NULL || g_mcuOtaPackageRecvFunc == NULL || g_mcuOtaProcInstallFunc == NULL || g_notifyMcuOtaExceptionFunc == NULL)
    {
        return false;
    }

    if (g_sendFrame2AppFunc == NULL || g_getUint8ArrayFromTLVFunc == NULL)
    {
        return false;
    }
    return true;
}

int8_t ProcOtaAppData(void *frame)
{
    if (frame == NULL)
    {
        qm_printf("frame invalid");
        return -1;
    }

    if (IsOtaFuncRegisted() != true)
    {
        qm_printf("ota func not registed");
        return -1;
    }

    OtaFrame *otaFrame = (OtaFrame *)frame;
    switch (otaFrame->cid)
    {
    case OTA_CID_GET_CUR_VER:
        ProcOtaGetCurVer(otaFrame);
        break;
    case OTA_CID_UPGRADE_PREPARE:
        ProcOtaPrepare(otaFrame);
        break;
    case OTA_CID_PAR_NEGOTIATION:
        ProcOtaParNegotiation(otaFrame);
        break;
    case OTA_CID_PACKAGE_RECV:
        ProcOtaPackageRecv(otaFrame);
        break;
    case OTA_CID_PACKAGE_CHECK_ACK:
        ProcOtaInstall(otaFrame);
        break;
    case OTA_CID_STOP_UPGRADE:
        ProcOtaStop(otaFrame);
        break;
    default:
        qm_printf("unknow cid\n");
        BuildOtaErrorCodeRsp(otaFrame, OTA_CONTROL_DEVICE_UNKNOW_CID);
        break;
    }
    return 0;
}
