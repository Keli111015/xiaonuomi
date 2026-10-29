/*
 * Copyright (c) Honor Technologies Co.,Ltd. 2022-2023. All rights reserved.
 * Description: BLE南向开放协议（包括广播、链接、配对、绑定、注册、信息读写、主动上报等）
 */
#include "honor_protocol.h"

#include "mbedtls/md.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/platform.h"
#include "mbedtls/sha256.h"

#define PRINT_UINT8_ARRAY(arr, len)           \
    qm_printf("%s[%s:%d]:", #arr, #len, len); \
    for (uint16_t i = 0; i < len - 1; ++i)    \
    {                                         \
        qm_printf("0x%02x-", arr[i]);         \
    }                                         \
    qm_printf("0x%02X\n", arr[len - 1]);

static uint8_t *g_sn1 = NULL;
static uint8_t *g_sn2 = NULL;
static uint8_t g_sn2Len = 0;
static uint8_t *g_regId = NULL;
static uint16_t g_regIdLen = 0;
static uint8_t *g_authCode = NULL;
static uint8_t *g_devRandom = NULL;
static uint8_t *g_verifyCode = NULL;
static uint16_t g_verifyCodeLen = 0;

static uint8_t g_linkStatus = 0;
static uint8_t g_connect_type = 0;

static uint16_t g_mtu = 0;
static uint8_t g_authCodeId = 0;
static uint8_t g_bondStatus = 1;    // 1-未绑定 0-已绑定
static uint8_t g_registerState = 0; // 0-未注册 1-已注册

static uint16_t g_maxFrameSize = 0;
static uint16_t g_sendInterval = 0;

static uint8_t g_connectType = BLE_BOND_REQ;
static uint8_t g_macAddr[BLE_MAC_LEN] = {0};
static BLE_VERIFY_METHOD g_verifyMethod = BLE_PRODUCT_VERIFY; // 默认product模式

static char g_checkSalt[BLE_SALT_LEN + 1] = {0};
static char g_calculateSalt[BLE_SALT_LEN + 1] = {0};

static char g_prdId[BLE_PRODUCT_ID_LEN + 1] = {0};
static char g_prdKey[BLE_PRODUCT_KEY_LEN + 1] = {0};
static char g_prdSecret[BLE_PRODUCT_SECRET_LEN + 1] = {0};
static char g_licenseKey[BLE_LICENSE_KEY_LEN + 1] = {0};
static char g_licenseSecret[BLE_LICENSE_SECRET_LEN + 1] = {0};

static char g_subPrdId[BLE_SUB_PRODUCT_ID_LEN + 1] = {0};

typedef enum
{
    BLE_DEV_CTRL_READ_GET_LEN,
    BLE_DEV_CTRL_READ_GET_BUF,
} BLE_DEV_CTRL_READ_GET_TYPE;

typedef enum
{
    BLE_PART_PACKAGE_INTACT,
    BLE_PART_PACKAGE_BEGIN,
    BLE_PART_PACKAGE_MIDDLE,
    BLE_PART_PACKAGE_END,
} BLE_PART_PACKAGE_TYPE;

typedef enum
{
    CONTROL_DEVICE_OK,
    CONTROL_DEVICE_UNKNOW_SID = 2,
    CONTROL_DEVICE_UNKNOW_CID,
    CONTROL_DEVICE_GET_PROPERTY_FAIL,
    CONTROL_DEVICE_SET_PROPERTY_FAIL,
} CONTROL_DEVICE_ERRCODE;

typedef struct
{
    uint8_t sof;
    uint16_t len;
    uint8_t ctrl;
    uint8_t reqId;
    uint8_t fsn;
    uint8_t sid;
    uint8_t cid;
    uint8_t *head;
    uint16_t headLen; // 包括headLen本身
    uint8_t *data;
    uint16_t dataLen; // payload长度，不写入TLV
} BleFrame;

typedef struct
{
    uint8_t sid;
    uint8_t cid;
    int8_t (*procFunc)(BleFrame *bleFrame);
} ProcFrameFunc;

typedef struct
{
    uint8_t recvSlice;                // 当前数据包是否需要继续接收分片 0:需要 1:不需要
    uint8_t *recvBuf;                 // 当前交互过程整个数据包
    uint16_t recvBufLen;              // 当前交互过程整个数据包长度
    uint16_t curPkgLen;               // 当前交互过程单个数据包的长度
    uint16_t curOffset;               // 当前交互过程单个数据包的偏移
    uint16_t curPkgFsn;               // 当前交互过程分包发送包的序号
    BLE_PART_PACKAGE_TYPE curPkgType; // 分包情况下，当前接收的包是首包、中间包还是尾包
} RecvDataRecord;

static RecvDataRecord g_recvDataRecord = {0};

typedef void (*DataCallback)(BleFrame *bleFrame);

static BleSendFunc g_bleHonorSendFunc = NULL;
static BleSendFunc g_bleNormalSendFunc = NULL;
static GenerateRandom g_genRandomFunc = NULL;
static ReadAuthCode g_readAuthCodeFunc = NULL;
static WriteAuthCode g_writeAuthCodeFunc = NULL;
static SetProperty g_setPropertyFunc = NULL;
static GetProperty g_getPropertyFunc = NULL;
static CfgAdvData g_cfgAdvDataFunc = NULL;
static RmvBondDev g_rmvBondDevFunc = NULL;
static AckWriteReportProperty g_ackWriteReportPropertyFunc = NULL;

uint8_t register_status(void)
{
    return g_registerState;
}

static void CalculateHMacSha256(const uint8_t *key, uint16_t keyLen, const uint8_t *inputBuf, uint16_t inputBufLen,
                                uint8_t *outputBuf, uint16_t outputBufLen)
{
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info;

    mbedtls_md_init(&ctx);
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_setup(&ctx, info, 1);

    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, inputBuf, inputBufLen);
    mbedtls_md_hmac_finish(&ctx, outputBuf);

    mbedtls_md_free(&ctx);
    return;
}

static void CalculateSha256(uint8_t *inputBuf, uint16_t inputBufLen, uint8_t *outputBuf, uint16_t outputBufLen)
{
    mbedtls_sha256_context sha256Ctx;
    mbedtls_sha256_init(&sha256Ctx);
    mbedtls_sha256_starts(&sha256Ctx, false);
    mbedtls_sha256_update(&sha256Ctx, (const unsigned char *)inputBuf, inputBufLen);
    mbedtls_sha256_finish(&sha256Ctx, outputBuf);
    mbedtls_sha256_free(&sha256Ctx);
    return;
}

static uint8_t ProcessCharacter(uint8_t character)
{
    uint8_t ret = character;
    if (ret <= 0x09)
    {
        ret = ret - 0x00 + '0';
    }
    else if (0x0A <= ret && ret <= 0x0F)
    {
        ret = ret - 0x0A + 'A';
    }
    else if (0x0a <= ret && ret <= 0x0f)
    {
        ret = ret - 0x0a + 'a';
    }
    return ret;
}

static void TranslateUint8ToCharacter(uint8_t *srcBuf, uint16_t srcBufLen, uint8_t *dstBuf, uint16_t dstBufLen)
{
    if (dstBufLen < srcBufLen)
    {
        qm_printf("dst len too small\n");
        return;
    }
    for (uint8_t i = 0; i < srcBufLen; ++i)
    {
        if (srcBufLen == BLE_HMAC_SHA256_LEN || srcBufLen == BLE_MAC_LEN)
        {
            sprintf((char *)(dstBuf + (i * 2)), (srcBufLen == BLE_MAC_LEN) ? ("%X") : ("%x"), srcBuf[i] >> 4);
            sprintf((char *)(dstBuf + (i * 2 + 1)), (srcBufLen == BLE_MAC_LEN) ? ("%X") : ("%x"), srcBuf[i] & 0x0F);
            continue;
        }
        uint8_t low = srcBuf[i] >> 4;
        uint8_t high = srcBuf[i] & 0x0F;
        dstBuf[i * 2] = ProcessCharacter(low);
        dstBuf[i * 2 + 1] = ProcessCharacter(high);
    }
    return;
}

static int8_t ReallocMemory(void **pointer, uint16_t srcLen, uint16_t dstLen)
{
    void *helperPointer = *pointer;
    *pointer = (void *)qm_malloc(dstLen);


    if (*pointer == NULL)
    {
        qm_printf("realloc memory error\n");
        *pointer = helperPointer;
        return -1;
    }
    (void)memset(*pointer, 0, dstLen);
    (void)memcpy(*pointer, helperPointer, srcLen);
    qm_free(helperPointer);
    return 0;
}

// 设置TLV格式的length，pos兼任入参和出参，退出时为length的后一位置，表示length计算完成
static void SetTLVLengthValue(uint16_t len, uint8_t *tlv, uint16_t *pos)
{
    // length字段可能是1个或2个字节
    // 首字节最高位为0，表示length只有1字节，最高位为1，表示2字节均为length
    if (len < BLE_LENGTH_ONE_BIT_MAX)
    {
        tlv[(*pos)++] = len;
        return;
    }
    tlv[(*pos)++] = ((len - (len & BLE_LENGTH_ONE_BIT_MAX)) >> (8 - 1)) | 0x80;
    tlv[(*pos)++] = len & BLE_LENGTH_ONE_BIT_MAX;
    return;
}

// 获取TLV格式的length，pos兼任入参和出参，退出时为length的后一位置，表示length计算完成
static uint16_t GetTLVLengthValue(const uint8_t *tlv, uint16_t *pos)
{
    // length字段可能是1个或2个字节
    // 首字节最高位为0，表示length只有1字节，最高位为1，表示2字节均为length
    uint16_t ret = 0;
    if ((tlv[*pos] & 0x80) == 0x80)
    {
        ret += tlv[*pos] & BLE_LENGTH_ONE_BIT_MAX;
        ++(*pos);
        ret <<= (8 - 1);
    }
    ret += tlv[*pos];
    ++(*pos);
    return ret;
}

static void GetUint8ArrayFromTLV(const uint8_t *tlv, uint16_t *pos, uint8_t **buffer, uint16_t *bufLen)
{
    *pos += BLE_PROTOCOL_TYPE_LEN;
    *bufLen = GetTLVLengthValue(tlv, pos); // len已经在函数内做了偏移，外面无需再偏移len的长度
    *buffer = (uint8_t *)qm_malloc(*bufLen);
    if (*buffer == NULL)
    {
        qm_printf("get uint8 array qm_malloc error\n");
        return;
    }
    (void)memcpy(*buffer, tlv + (*pos), *bufLen);
    *pos += *bufLen;
    return;
}

static int8_t GetAuthCode()
{
    if(g_authCode == NULL)
    {
        g_authCode = (uint8_t *)qm_malloc(BLE_AUTH_CODE_LEN);
        if (g_authCode == NULL)
        {
            qm_printf("qm_malloc read authCode error\n");
            return -1;
        }
        memset(g_authCode, 0, BLE_AUTH_CODE_LEN);
    }

    if (g_readAuthCodeFunc(g_authCode, BLE_AUTH_CODE_LEN) == 0)
    {
        qm_printf("read authCode from flash error\n");
        
        return 0;
    }

    return -1;
}

static void BuildAdvData(uint8_t *advData, uint16_t advDataLen)
{
    uint16_t pos = 0;
    (void)memset(advData, 0, advDataLen);
    uint8_t advHead[] = {
        BLE_ADV_DATA_LEN - 1, // LENGTH
        0x16,                 // TYPE
        0xF8,
        0xFC, // UUID
        0x00, // HEADER
        0x81, // AdvType: 0x81-发布
        0x01,
        0x00, // Capability: mlink_bind
        0x11, // NetProtocol: BLE
    };
    (void)memcpy(advData, advHead, sizeof(advHead));
    pos += sizeof(advHead);

    advData[pos++] = g_bondStatus; // status: 1-未绑定; 0-已绑定
    qm_printf("==============register status = %d================\r\n", g_bondStatus);

    advData[pos++] = (0x5 << 4) + sizeof(g_macAddr); // mac
    (void)memcpy(advData + pos, g_macAddr, sizeof(g_macAddr));
    pos += sizeof(g_macAddr);

    advData[pos++] = (0x6 << 4) + strlen(g_prdId) + strlen(g_subPrdId); // prdId
    (void)memcpy(advData + pos, g_prdId, strlen(g_prdId));
    pos += strlen(g_prdId);

    (void)memcpy(advData + pos, g_subPrdId, strlen(g_subPrdId)); // subPrdId
    pos += strlen(g_subPrdId);

    return;
}

uint8_t dev_register_status_get(void)
{
    return g_bondStatus = (GetAuthCode() == 0 ? 0 : 1);
}

void SetAdvData()
{
    uint8_t advData[BLE_ADV_DATA_LEN] = {0};
    uint16_t advDataLen = BLE_ADV_DATA_LEN;

    g_bondStatus = GetAuthCode() == 0 ? 0 : 1;
    BuildAdvData(advData, advDataLen);

    if (g_cfgAdvDataFunc != NULL)
        g_cfgAdvDataFunc(advData, advDataLen);
    return;
}

static int8_t UnpackRecvFrame(const uint8_t *frame, uint16_t len, BleFrame *bleFrame)
{
    uint16_t pos = 0;
    bleFrame->sof = frame[pos++];
    bleFrame->len = (frame[pos++] << 8 & BLE_BIT_MAX_VALUE_UINT16_HIGH);
    bleFrame->len += frame[pos++];
    bleFrame->ctrl = frame[pos++];
    if ((bleFrame->ctrl & 0x0C) != 0)
    {
        bleFrame->reqId = ((frame[pos++] << 8) & BLE_BIT_MAX_VALUE_UINT16_HIGH);
        bleFrame->reqId += frame[pos++];
    }
    if ((bleFrame->ctrl & 0x03) != 0)
    {
        bleFrame->fsn = frame[pos++];
    }

    // 单包发送或者分包的首包才需要解析sid cid head字段
    if (bleFrame->ctrl == BLE_PART_PACKAGE_INTACT || bleFrame->ctrl == BLE_PART_PACKAGE_BEGIN)
    {
        bleFrame->sid = frame[pos++];
        bleFrame->cid = frame[pos++];
        bleFrame->headLen = frame[pos] + 1; // head长度包括headLen本身
        if (bleFrame->headLen == 1)
        {
            // 透传方案，连接绑定注册流程，head为0
            bleFrame->head = (uint8_t *)(&frame[pos]);
        }
        else
        {
            // profile方案，head为LV，L为head总长度，V为reqId的TLV
            bleFrame->head = (uint8_t *)qm_malloc(bleFrame->headLen);
            if (bleFrame->head == NULL)
            {
                qm_printf("qm_malloc head reqid error\n");
                return -1;
            }
            (void)memcpy(bleFrame->head, frame + pos, bleFrame->headLen);
        }
        pos += bleFrame->headLen;
    }
    if (len - pos < 0)
    {
        qm_printf("parse frame error\n");
        return -1;
    }

    bleFrame->dataLen = len - pos;
    bleFrame->data = (uint8_t *)(frame + pos);
    return 0;
}

static void SendNormalRspBuf(const char *sendHead, const uint8_t *frame, uint16_t len)
{
    uint16_t sendHeadLen = strlen(sendHead);
    uint16_t sendBufLen = sendHeadLen + len;
    uint8_t *sendBuf = (uint8_t *)qm_malloc(sendBufLen);
    if (sendBuf == NULL)
    {
        qm_printf("normal send rsp qm_malloc error\n");
        return;
    }
    (void)memset(sendBuf, 0, sendBufLen);
    (void)memcpy(sendBuf, sendHead, sendHeadLen);
    (void)memcpy(sendBuf + sendHeadLen, frame, len);
    g_bleNormalSendFunc(sendBuf, sendBufLen);
    qm_free(sendBuf);
    return;
}

static void RecvNormalFrameFromApp(const uint8_t *frame, uint16_t len)
{
    const char *writeRspBuf = "write rsp:";
    SendNormalRspBuf(writeRspBuf, frame, len);
    const char *notifyRspBuf = "notify:";
    SendNormalRspBuf(notifyRspBuf, frame, len);
    const char *indicateRspBuf = "indicate:";
    SendNormalRspBuf(indicateRspBuf, frame, len);
    return;
}

static void ProcessRecvFinish()
{
    // 更新head中的len字段 1:len的高位 2:len的低位
    g_recvDataRecord.recvBuf[0 + SOF_SIZE] = (g_recvDataRecord.recvBufLen > BLE_BIT_MAX_VALUE_UINT8)
                                                 ? g_recvDataRecord.recvBufLen - BLE_BIT_MAX_VALUE_UINT8
                                                 : 0;
    g_recvDataRecord.recvBuf[0 + SOF_SIZE + LEN_SIZE - 1] =
        (g_recvDataRecord.recvBufLen > BLE_BIT_MAX_VALUE_UINT8) ? BLE_BIT_MAX_VALUE_UINT8 : g_recvDataRecord.recvBufLen;
    // 删除head中的fsn
    for (uint16_t i = SOF_SIZE + LEN_SIZE + CTRL_SIZE; i < g_recvDataRecord.recvBufLen - 1; ++i)
    {
        g_recvDataRecord.recvBuf[i] = g_recvDataRecord.recvBuf[i + 1];
    }
    g_recvDataRecord.recvBuf[0 + SOF_SIZE + LEN_SIZE] = 0x00;         // 更新head中的ctrl为0x00
    g_recvDataRecord.recvBuf[g_recvDataRecord.recvBufLen - 1] = 0x00; // 将buffer最后一位置为0x00
    g_recvDataRecord.recvBufLen -= 1;                                 // 前面去除fsn，记录的长度要减去1
    return;
}

static int8_t ProcessSliceRecv(const uint8_t *frame, uint16_t len)
{
    uint16_t lastBufLen = g_recvDataRecord.recvBufLen;
    uint16_t curBufLen = lastBufLen + len;
    if (ReallocMemory((void **)(&g_recvDataRecord.recvBuf), lastBufLen, curBufLen) != 0)
    {
        qm_printf("realloc memory error\n");
        return -1;
    }

    (void)memcpy(g_recvDataRecord.recvBuf + g_recvDataRecord.recvBufLen, frame, len);
    // 更新总长度和当前包偏移
    g_recvDataRecord.recvBufLen += len;
    g_recvDataRecord.curOffset += len;
    if (g_recvDataRecord.curOffset < g_recvDataRecord.curPkgLen)
    {
        qm_printf("continue recv slice\n");
        return 0;
    }
    // 当前包分片接收完成
    g_recvDataRecord.recvSlice = 1;
    g_recvDataRecord.curPkgLen = 0;
    g_recvDataRecord.curOffset = 0;
    if (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_INTACT || g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_END)
    {
        // 当前交互过程所有数据包接收完成
        g_recvDataRecord.curPkgFsn = 0;
        if (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_END)
        {
            ProcessRecvFinish();
        }
        return 1;
    }
    return 0;
}

static int8_t ProcessPackageMalloc(uint16_t len, uint16_t dataLen)
{
    if (g_recvDataRecord.recvBuf == NULL)
    {
        g_recvDataRecord.recvBuf = (uint8_t *)qm_malloc(len);
        if (g_recvDataRecord.recvBuf == NULL)
        {
            qm_printf("qm_malloc recvBuf error\n");
            return -1;
        }
        (void)memset(g_recvDataRecord.recvBuf, 0, len);
        return 0;
    }
    // 已经接收数据，先暂存last的buf，再申请新的长度，拷贝进新buf，释放last的buf
    uint16_t lastBufLen = g_recvDataRecord.recvBufLen;
    uint16_t curBufLen = lastBufLen + dataLen;
    if (ReallocMemory((void **)(&g_recvDataRecord.recvBuf), lastBufLen, curBufLen) != 0)
    {
        qm_printf("realloc memory error\n");
        return -1;
    }
    return 0;
}

// return: -1:处理失败 0:处理成功 1:当前交互数据接收完成
static int8_t RecvHonorFrameFromApp(const uint8_t *frame, uint16_t len)
{
    if (g_recvDataRecord.recvSlice == 0)
    {
        // 接收分片数据
        return ProcessSliceRecv(frame, len);
    }

    BleFrame bleFrame = {0};
    if (UnpackRecvFrame(frame, len, &bleFrame) != 0)
    {
        qm_printf("unpack error\n");
        if (bleFrame.head != NULL && bleFrame.headLen != 1)
        {
            qm_free(bleFrame.head);
        }
        return -1;
    }

    if (bleFrame.head != NULL && bleFrame.headLen != 1)
    {
        qm_free(bleFrame.head);
    }

    if (ProcessPackageMalloc(len, bleFrame.dataLen) != 0)
    {
        qm_printf("realloc memory error\n");
        return -1;
    }

    g_recvDataRecord.curPkgType = (BLE_PART_PACKAGE_TYPE)bleFrame.ctrl;
    if (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_INTACT)
    {
        (void)memcpy(g_recvDataRecord.recvBuf, frame, len);
        g_recvDataRecord.recvSlice = (bleFrame.len > (len - SOF_SIZE - LEN_SIZE)) ? 0 : 1;
        g_recvDataRecord.recvBufLen += len;
        return g_recvDataRecord.recvSlice;
    }

    // 非首包的包长度应该是bleFrame.len减去除数据的部分，即ctrl、fsn等部分的长度
    // CTRL + REQID + FSN = len - SOF - LEN - dataLen --> TEMP
    // 当前包所有分片的数据（不包含SOF/LEN/CTRL/REQID/FSN）总长度 = LEN - TEMP
    g_recvDataRecord.curPkgLen = (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_BEGIN)
                                     ? (SOF_SIZE + LEN_SIZE + bleFrame.len)
                                     : (bleFrame.len - (len - SOF_SIZE - LEN_SIZE - bleFrame.dataLen));

    if (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_BEGIN && bleFrame.fsn != 0)
    {
        qm_printf("begin package fsn error\n");
        return -1;
    }
    if ((g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_MIDDLE ||
         g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_END) &&
        g_recvDataRecord.curPkgFsn + 1 != bleFrame.fsn)
    {
        qm_printf("mid or end package fsn error\n");
        return -1;
    }

    const uint8_t *appendBuf = (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_BEGIN) ? frame : bleFrame.data;
    uint16_t appendLen = (g_recvDataRecord.curPkgType == BLE_PART_PACKAGE_BEGIN) ? len : bleFrame.dataLen;
    (void)memcpy(g_recvDataRecord.recvBuf + g_recvDataRecord.recvBufLen, appendBuf, appendLen);

    g_recvDataRecord.curPkgFsn = bleFrame.fsn;
    g_recvDataRecord.recvBufLen += appendLen;
    g_recvDataRecord.curOffset += appendLen;
    g_recvDataRecord.recvSlice = (g_recvDataRecord.curOffset < g_recvDataRecord.curPkgLen) ? 0 : 1;
    if (bleFrame.ctrl == BLE_PART_PACKAGE_END && g_recvDataRecord.recvSlice == 1)
    {
        g_recvDataRecord.curPkgLen = 0;
        g_recvDataRecord.curOffset = 0;
        g_recvDataRecord.curPkgFsn = 0;
        ProcessRecvFinish();
        return 1;
    }

    return 0;
}

static int8_t BuildSendPackage(BleFrame *bleFrame, uint8_t **sendPkg, uint16_t *sendPkgLen)
{
    uint16_t totalLen = 0;
    totalLen += SOF_SIZE;
    totalLen += LEN_SIZE;
    totalLen += CTRL_SIZE;
    totalLen += (bleFrame->reqId ? REQID_SIZE : 0);
    totalLen += (bleFrame->ctrl ? FSN_SIZE : 0);
    if (bleFrame->ctrl == BLE_PART_PACKAGE_INTACT || bleFrame->ctrl == BLE_PART_PACKAGE_BEGIN)
    {
        totalLen += SID_SIZE;
        totalLen += CID_SIZE;
        totalLen += bleFrame->headLen;
    }
    totalLen += bleFrame->dataLen;

    uint16_t pos = 0;
    *sendPkg = (uint8_t *)qm_malloc(totalLen);
    if (*sendPkg == NULL)
    {
        qm_printf("qm_malloc frame error\n");
        return -1;
    }
    (void)memset(*sendPkg, 0, totalLen);
    (*sendPkg)[pos++] = TRANS_PROTO_SOF;
    (*sendPkg)[pos++] = (totalLen - SOF_SIZE - LEN_SIZE) >> 8;
    (*sendPkg)[pos++] = (totalLen - SOF_SIZE - LEN_SIZE) & BLE_BIT_MAX_VALUE_UINT8;
    (*sendPkg)[pos++] = bleFrame->reqId ? (bleFrame->ctrl & 0x7) : bleFrame->ctrl; // reqid不为0，则需要置位bit2~bit3位
    if (bleFrame->reqId != 0)
    {
        (*sendPkg)[pos++] = (bleFrame->reqId >> 8) & BLE_BIT_MAX_VALUE_UINT16_LOW;
        (*sendPkg)[pos++] = bleFrame->reqId & BLE_BIT_MAX_VALUE_UINT16_LOW;
    }
    if ((bleFrame->ctrl & 0x03) != 0)
    {
        (*sendPkg)[pos++] = bleFrame->fsn;
    }
    // 单包发送或分包发送首包，添加sid cid head，分包的中间包和结尾包不需要添加sid cid head
    if (bleFrame->ctrl == BLE_PART_PACKAGE_INTACT || bleFrame->ctrl == BLE_PART_PACKAGE_BEGIN)
    {
        (*sendPkg)[pos++] = bleFrame->sid;
        (*sendPkg)[pos++] = bleFrame->cid;
        (void)memcpy((*sendPkg) + pos, bleFrame->head, bleFrame->headLen);
        pos += bleFrame->headLen;
    }
    (void)memcpy((*sendPkg) + pos, bleFrame->data, bleFrame->dataLen);
    pos += bleFrame->dataLen;
    *sendPkgLen = pos;
    return 0;
}

static void SendFrameCore(const uint8_t *sendPkg, uint16_t sendPkgLen)
{
    uint16_t sliceCount = (sendPkgLen == g_mtu) ? 1 : (sendPkgLen / g_mtu + 1);
    for (uint16_t i = 0; i < sliceCount; ++i)
    {
        const uint8_t *sendBuf = sendPkg + i * g_mtu;
        uint16_t sendBufLen = (i == sliceCount - 1) ? sendPkgLen - i * g_mtu : g_mtu;
        g_bleHonorSendFunc(sendBuf, sendBufLen);
    }
    return;
}

static int8_t SendFrame2App(BleFrame *bleFrame)
{
    uint8_t headerLen = SOF_SIZE + LEN_SIZE + CTRL_SIZE + (bleFrame->reqId ? REQID_SIZE : 0);
    uint16_t totalHeaderLen = headerLen + SID_SIZE + CID_SIZE + bleFrame->headLen;

    uint16_t totalLen = totalHeaderLen + bleFrame->dataLen; // 待发送的数据总长度
    uint16_t packageCount = (totalLen == g_maxFrameSize) ? 1 : (totalLen / g_maxFrameSize + 1);

    if (packageCount > 1)
    {
        headerLen += FSN_SIZE;
        totalLen += (packageCount - 1) * headerLen;
        packageCount = totalLen / g_maxFrameSize + 1;
    }

    uint16_t curOffset = 0;                   // 发送偏移
    uint8_t *sendData = bleFrame->data;       // 将data指向内存用sendData接管，用来最后释放data指向的待发送数据的内存
    uint16_t sendDataLen = bleFrame->dataLen; // 待发送的数据总长度

    for (uint16_t i = 0; i < packageCount; ++i)
    {
        bleFrame->fsn = i;
        bleFrame->data = sendData + curOffset;

        if (i == 0)
        {
            if (packageCount == 1)
            {
                bleFrame->ctrl = BLE_PART_PACKAGE_INTACT;
                bleFrame->dataLen = sendDataLen;
            }
            else
            {
                bleFrame->ctrl = BLE_PART_PACKAGE_BEGIN;
                bleFrame->dataLen = g_maxFrameSize - headerLen;
            }
        }
        else if (i == packageCount - 1)
        {
            bleFrame->ctrl = BLE_PART_PACKAGE_END;
            bleFrame->dataLen = sendDataLen - curOffset;
        }
        else
        {
            bleFrame->ctrl = BLE_PART_PACKAGE_MIDDLE;
            // 分包中间包不携带sid cid和head
            bleFrame->dataLen = g_maxFrameSize - headerLen;
        }
        // 构建待发送数据包会申请内存，发送完成需要释放
        uint8_t *sendPkg = NULL;
        uint16_t sendPkgLen = 0;
        if (BuildSendPackage(bleFrame, &sendPkg, &sendPkgLen) != 0)
        {
            qm_printf("build send package error\n");
            return -1;
        }
        SendFrameCore(sendPkg, sendPkgLen);
        qm_free(sendPkg);
        // 更新发送偏移
        curOffset += bleFrame->dataLen;
    }
    // 重新指回原有内存，在外面释放
    bleFrame->data = sendData;
    return 0;
}

static int8_t ParamNegotiateRsp(BleFrame *bleFrame)
{
    g_devRandom = (uint8_t *)qm_malloc(BLE_RANDOM_LEN);
    if (g_devRandom == NULL)
    {
        qm_printf("qm_malloc error\n");
        return -1;
    }
    g_genRandomFunc(g_devRandom, sizeof(g_devRandom));

    uint8_t tmpMacAddr[BLE_MAC_LEN * 2] = {0};
    TranslateUint8ToCharacter(g_macAddr, sizeof(g_macAddr), tmpMacAddr, sizeof(tmpMacAddr));

    uint16_t tlvLen = 0;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_ONE;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_TWO;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_TWO;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_TWO;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_ONE;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_RANDOM_LEN;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_ONE;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + sizeof(tmpMacAddr);

    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc param negotiate rsp error\n");
        return -1;
    }

    uint16_t pos = 0;
    tlv[pos++] = BLE_MESSAGE_TYPE_1; // proto_version: 0x01
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_ONE;
    tlv[pos++] = BLE_PROTOCOL_VERSION;
    tlv[pos++] = BLE_MESSAGE_TYPE_2; // max_frame_size
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = g_maxFrameSize >> 8;
    tlv[pos++] = g_maxFrameSize & BLE_BIT_MAX_VALUE_UINT16_LOW;
    tlv[pos++] = BLE_MESSAGE_TYPE_3; // mtu
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = g_mtu >> 8;
    tlv[pos++] = g_mtu & BLE_BIT_MAX_VALUE_UINT16_LOW;
    tlv[pos++] = BLE_MESSAGE_TYPE_4; // interval: 10毫秒，数据包发送间隔
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_TWO;
    tlv[pos++] = g_sendInterval >> 8;
    tlv[pos++] = g_sendInterval & BLE_BIT_MAX_VALUE_UINT16_LOW;
    tlv[pos++] = BLE_MESSAGE_TYPE_5; // authtype: hmacsha256
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_ONE;
    tlv[pos++] = BLE_AUTH_TYPE_HMACSHA256;
    tlv[pos++] = BLE_MESSAGE_TYPE_6; // random
    tlv[pos++] = BLE_RANDOM_LEN;
    (void)memcpy(tlv + pos, g_devRandom, BLE_RANDOM_LEN);
    pos += BLE_RANDOM_LEN;
    tlv[pos++] = BLE_MESSAGE_TYPE_8; // register_state: 0x00-未注册; 0x01-已注册
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_ONE;
    tlv[pos++] = g_registerState;
    tlv[pos++] = BLE_MESSAGE_TYPE_9; // udid: mac
    tlv[pos++] = sizeof(tmpMacAddr);
    (void)memcpy(tlv + pos, tmpMacAddr, sizeof(tmpMacAddr));

    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;
    return 0;
}

static int8_t CheckHmacSha256Value(const uint8_t *appAuthCode, uint8_t appAuthCodeLen, const uint8_t *devAuthCode,
                                   uint8_t devAuthCodeLen)
{
    if (appAuthCodeLen != devAuthCodeLen)
    {
        qm_printf("len difference\n");
        return -1;
    }

    for (uint8_t i = 0; i < appAuthCodeLen; ++i)
    {
        if (appAuthCode[i] != devAuthCode[i])
        {
            qm_printf("appAuthCode[0x%02x]:0x%02x, devAuthCode[0x%02x]:0x%02x\n", i, appAuthCode[i], i, devAuthCode[i]);
            return -1;
        }
    }
    return 0;
}

static void CalculateDevicePin(uint8_t *pin, uint16_t pinLen)
{
    uint16_t inBufLen = strlen(g_prdId) + (BLE_CALCULATE_PIN_MAC_LEN * 2);
    uint8_t *inBuf = (uint8_t *)qm_malloc(inBufLen);
    if (inBuf == NULL)
    {
        qm_printf("cal pin qm_malloc error\n");
        return;
    }
    (void)memset(inBuf, 0, inBufLen);

    (void)memcpy(inBuf, g_prdId, strlen(g_prdId));

    uint8_t asciiMacAddr[BLE_CALCULATE_PIN_MAC_LEN * 2] = {0};
    TranslateUint8ToCharacter(g_macAddr + (BLE_MAC_LEN - BLE_CALCULATE_PIN_MAC_LEN), BLE_CALCULATE_PIN_MAC_LEN,
                              asciiMacAddr, sizeof(asciiMacAddr));
    (void)memcpy(inBuf + strlen(g_prdId), asciiMacAddr, sizeof(asciiMacAddr));

    uint8_t outBuf[BLE_HMAC_SHA256_LEN] = {0};
    CalculateSha256(inBuf, inBufLen, outBuf, sizeof(outBuf));
    (void)memcpy(pin, outBuf + (BLE_HMAC_SHA256_LEN - BLE_PIN_LEN), BLE_PIN_LEN);

    qm_free(inBuf);
    return;
}

uint8_t honor_link_status_get(void)
{
    return g_linkStatus;
}

static int8_t CertApprovedRsp(BleFrame *bleFrame, const uint8_t *appAuthCode, uint8_t authCodeLen,
                              const uint8_t *appRandom, uint8_t appRandomLen)
{
    // 计算HMAC-SHA256, KEY: pin+salt BUFFER: randomDev+randomApp
    uint8_t devAuthCode[BLE_HMAC_SHA256_LEN] = {0};
    uint8_t pin[BLE_PIN_LEN] = {0};
    uint16_t keyLen = strlen(g_checkSalt) + BLE_PIN_LEN;
    uint8_t *key = (uint8_t *)qm_malloc(keyLen);
    if (key == NULL)
    {
        qm_printf("check hmac qm_malloc error\n");
        return -1;
    }
    (void)memset(key, 0, keyLen);
    uint16_t inLen = BLE_RANDOM_LEN * 2;
    uint8_t *in = (uint8_t *)qm_malloc(inLen);
    if (in == NULL)
    {
        qm_printf("check hmac qm_malloc error\n");
        qm_free(key);
        return -1;
    }
    (void)memset(in, 0, inLen);

    const uint8_t *pinAuthCode = NULL;
    uint16_t pinAuthCodeLen = 0;
    if (g_connectType == BLE_CONNECT_REQ)
    {
        // registered connect request
        qm_printf("registered connect\n");
        if (g_authCode == NULL)
        {
            qm_printf("authCode is NULL read flash\n");
            // 从flash读取authCode到g_authCode
            if (GetAuthCode() != 0)
            {
                qm_printf("no authCode\n");
                qm_free(key);
                qm_free(in);
                return -1;
            }
        }
        pinAuthCode = g_authCode;
        pinAuthCodeLen = BLE_AUTH_CODE_LEN;
        g_connect_type = 1;
    }
    else
    {
        // unregistered bond request
        qm_printf("Unregistered connect\n");
        CalculateDevicePin(pin, BLE_PIN_LEN);
        pinAuthCode = pin;
        pinAuthCodeLen = BLE_PIN_LEN;
    }
    (void)memcpy(key, pinAuthCode, pinAuthCodeLen);
    (void)memcpy(key + pinAuthCodeLen, g_checkSalt, BLE_SALT_LEN);
    (void)memcpy(in, g_devRandom, BLE_RANDOM_LEN);
    (void)memcpy(in + BLE_RANDOM_LEN, appRandom, appRandomLen);
    if (g_devRandom != NULL)
    {
        qm_free(g_devRandom);
        g_devRandom = NULL;
    }

    CalculateHMacSha256(key, keyLen, in, inLen, devAuthCode, BLE_HMAC_SHA256_LEN);
    if (CheckHmacSha256Value(appAuthCode, authCodeLen, devAuthCode, sizeof(devAuthCode)) != 0)
    {
        qm_printf("auth code difference\n");
        qm_free(key);
        qm_free(in);
        return -1;
    }

    (void)memset(key, 0, BLE_PIN_LEN + BLE_SALT_LEN);
    (void)memset(devAuthCode, 0, BLE_HMAC_SHA256_LEN);

    (void)memcpy(key, pinAuthCode, pinAuthCodeLen);
    (void)memcpy(key + pinAuthCodeLen, g_calculateSalt, BLE_SALT_LEN);

    CalculateHMacSha256(key, keyLen, in, inLen, devAuthCode, BLE_HMAC_SHA256_LEN);
    uint16_t tlvLen = BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_HMAC_SHA256_LEN;
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc cert approve rsp error\n");
        qm_free(key);
        qm_free(in);
        return -1;
    }
    tlv[0] = BLE_MESSAGE_TYPE_1;
    tlv[1] = BLE_HMAC_SHA256_LEN;
    (void)memcpy(tlv + BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN, devAuthCode, BLE_HMAC_SHA256_LEN);

    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;

    qm_free(key);
    qm_free(in);
    qm_printf("connect end begin register\n");
    return 0;
}

static int8_t SetConfigRsp(BleFrame *bleFrame)
{
    uint16_t tlvLen = BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_PROTOCOL_VALUE_LEN_ONE;
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc set config rsp error\n");
        return -1;
    }
    tlv[0] = BLE_DEVICE_CONNECT_GET_CHALLENGE_CODE;
    tlv[1] = BLE_MESSAGE_TYPE_1;
    tlv[2] = g_registerState;

    bleFrame->cid = BLE_DEVICE_CONNECT_SET_CONFIG;
    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;
    return 0;
}

static int8_t GetDevRegChallengeCodeReq(BleFrame *bleFrame)
{
    uint8_t tmpSn1[BLE_SN_LEN >> 1] = {0}; // sn1--random
    g_genRandomFunc(tmpSn1, BLE_SN_LEN >> 1);
    g_sn1 = (uint8_t *)qm_malloc(BLE_SN_LEN);
    if (g_sn1 == NULL)
    {
        qm_printf("qm_malloc sn1 error\n");
        return -1;
    }
    (void)memset(g_sn1, 0, BLE_SN_LEN);
    TranslateUint8ToCharacter(tmpSn1, sizeof(tmpSn1), g_sn1, BLE_SN_LEN);

    uint16_t tlvLen = 0;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + 1;          // usage
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + g_regIdLen; // regId
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_SN_LEN; // sn1
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("challenge code qm_malloc error\n");
        return -1;
    }
    uint8_t pos = 0;
    tlv[pos++] = BLE_MESSAGE_TYPE_1;
    tlv[pos++] = BLE_PROTOCOL_VALUE_LEN_ONE;
    tlv[pos++] = BLE_DEVICE_LOGIN;

    tlv[pos++] = BLE_MESSAGE_TYPE_2;
    tlv[pos++] = g_regIdLen;
    (void)memcpy(tlv + pos, g_regId, g_regIdLen);
    pos += g_regIdLen;

    tlv[pos++] = BLE_MESSAGE_TYPE_3;
    tlv[pos++] = BLE_SN_LEN;
    (void)memcpy(tlv + pos, g_sn1, BLE_SN_LEN);

    bleFrame->cid = BLE_DEVICE_CONNECT_GET_CHALLENGE_CODE;
    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;
    return 0;
}

static uint16_t GetSignBufLen()
{
    uint16_t bufLen = 0;
    bufLen += BLE_SN_LEN; // sn1
    bufLen += strlen(":");
    bufLen += g_sn2Len; // sn2
    bufLen += strlen(":");
    bufLen += g_regIdLen; // regId
    bufLen += strlen(":");
    bufLen += strlen(g_prdId); // prdId
    bufLen += strlen(":");
    bufLen += sizeof(g_macAddr) * 2; // mac
    bufLen += strlen(":");
    bufLen += 0; // sn字段不填充，预留能力
    bufLen += strlen(":");
    bufLen += g_verifyCodeLen; // verifyCode
    bufLen += strlen(":");
    bufLen += sizeof(uint8_t); // authCodeId
    bufLen += strlen(":");
    bufLen += BLE_AUTH_CODE_LEN; // authCode
    return bufLen;
}

static int8_t BuildCalculateSignBuf(uint8_t *buf, uint16_t bufLen)
{
    uint16_t pos = 0;
    (void)memcpy(buf + pos, g_sn1, BLE_SN_LEN);
    pos += BLE_SN_LEN;
    buf[pos++] = ':';

    (void)memcpy(buf + pos, g_sn2, g_sn2Len);
    pos += g_sn2Len;
    buf[pos++] = ':';

    (void)memcpy(buf + pos, g_regId, g_regIdLen);
    pos += g_regIdLen;
    buf[pos++] = ':';

    (void)memcpy(buf + pos, g_prdId, strlen(g_prdId));
    pos += strlen(g_prdId);
    buf[pos++] = ':';

    uint8_t tmpMacAddr[BLE_MAC_LEN * 2] = {0};
    TranslateUint8ToCharacter(g_macAddr, sizeof(g_macAddr), tmpMacAddr, sizeof(tmpMacAddr));
    (void)memcpy(buf + pos, tmpMacAddr, sizeof(tmpMacAddr));
    pos += sizeof(tmpMacAddr);
    buf[pos++] = ':';

    // sn字段不填充，预留能力
    buf[pos++] = ':';

    (void)memcpy(buf + pos, g_verifyCode, g_verifyCodeLen);
    pos += g_verifyCodeLen;
    buf[pos++] = ':';

    buf[pos++] = g_authCodeId;
    buf[pos++] = ':';

    (void)memcpy(buf + pos, g_authCode, BLE_AUTH_CODE_LEN);
    pos += BLE_AUTH_CODE_LEN;

    if (pos > bufLen)
    {
        qm_printf("build buf len error\n");
        return -1;
    }
    return 0;
}

static int8_t CalSignBuf(uint8_t *sign, uint16_t signLen)
{
    uint16_t bufLen = GetSignBufLen();
    uint8_t *buf = (uint8_t *)qm_malloc(bufLen);
    if (buf == NULL)
    {
        qm_printf("qm_malloc hmacsha256 error\n");
        return -1;
    }
    (void)memset(buf, 0, bufLen);

    int8_t buildRet = BuildCalculateSignBuf(buf, bufLen);
    if (g_sn1 != NULL)
    {
        qm_free(g_sn1);
        g_sn1 = NULL;
    }
    if (g_sn2 != NULL)
    {
        qm_free(g_sn2);
        g_sn2 = NULL;
    }
    if (buildRet != 0)
    {
        qm_free(buf);
        return -1;
    }

    const uint8_t *key = (const uint8_t *)((g_verifyMethod == BLE_PRODUCT_VERIFY) ? g_prdSecret : g_licenseSecret);
    uint8_t keyLen = (g_verifyMethod == BLE_PRODUCT_VERIFY) ? strlen(g_prdSecret) : strlen(g_licenseSecret);
    CalculateHMacSha256(key, keyLen, buf, bufLen, sign, signLen);

    qm_free(buf);
    return 0;
}

static uint16_t GetRegisterTLVLen()
{
    uint16_t tlvLen = 0;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + g_regIdLen;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + strlen(g_prdId);
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + sizeof(g_macAddr) * 2;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + g_verifyCodeLen;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + sizeof(uint8_t);
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_AUTH_CODE_LEN;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_HMAC_SHA256_LEN * 2;
    tlvLen += BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN;
    tlvLen += (g_verifyMethod == BLE_PRODUCT_VERIFY) ? strlen(g_prdKey) : strlen(g_licenseKey);
    return tlvLen;
}

static void BuildDeviceRegisterReq(const uint8_t *sign, uint16_t signLen, const uint8_t *key, uint16_t keyLen,
                                   uint8_t keyType, uint8_t *tlv)
{
    uint8_t pos = 0;
    tlv[pos++] = BLE_MESSAGE_TYPE_1;
    tlv[pos++] = g_regIdLen;
    (void)memcpy(tlv + pos, g_regId, g_regIdLen);
    pos += g_regIdLen;

    tlv[pos++] = BLE_MESSAGE_TYPE_2;
    tlv[pos++] = strlen(g_prdId);
    (void)memcpy(tlv + pos, g_prdId, strlen(g_prdId));
    pos += strlen(g_prdId);

    tlv[pos++] = BLE_MESSAGE_TYPE_3;
    uint8_t tmpMacAddr[BLE_MAC_LEN * 2] = {0};
    TranslateUint8ToCharacter(g_macAddr, sizeof(g_macAddr), tmpMacAddr, sizeof(tmpMacAddr));
    tlv[pos++] = sizeof(tmpMacAddr);
    (void)memcpy(tlv + pos, tmpMacAddr, sizeof(tmpMacAddr));
    pos += sizeof(tmpMacAddr);

    tlv[pos++] = BLE_MESSAGE_TYPE_4;
    tlv[pos++] = g_verifyCodeLen;
    (void)memcpy(tlv + pos, g_verifyCode, g_verifyCodeLen);
    pos += g_verifyCodeLen;

    tlv[pos++] = BLE_MESSAGE_TYPE_5;
    tlv[pos++] = sizeof(g_authCodeId);
    tlv[pos++] = g_authCodeId;

    tlv[pos++] = BLE_MESSAGE_TYPE_6;
    tlv[pos++] = BLE_AUTH_CODE_LEN;
    (void)memcpy(tlv + pos, g_authCode, BLE_AUTH_CODE_LEN);
    pos += BLE_AUTH_CODE_LEN;

    tlv[pos++] = BLE_MESSAGE_TYPE_7;
    tlv[pos++] = signLen;
    (void)memcpy(tlv + pos, sign, signLen);
    pos += signLen;

    tlv[pos++] = keyType;
    tlv[pos++] = keyLen;
    (void)memcpy(tlv + pos, key, keyLen);
    return;
}

static int8_t DeviceRegisterReq(BleFrame *bleFrame)
{
    if (GetAuthCode() != 0)
    {
        uint8_t tmpAuthCode[BLE_AUTH_CODE_LEN >> 1] = {0};
        g_genRandomFunc(tmpAuthCode, BLE_AUTH_CODE_LEN >> 1);
        TranslateUint8ToCharacter(tmpAuthCode, sizeof(tmpAuthCode), g_authCode, BLE_AUTH_CODE_LEN);
    }

    uint8_t tmpSign[BLE_HMAC_SHA256_LEN] = {0}; // hmacsha256
    if (CalSignBuf(tmpSign, sizeof(tmpSign)) != 0)
    {
        qm_printf("calculate sign error\n");
        goto FAIL;
    }

    uint8_t sign[BLE_HMAC_SHA256_LEN * 2 + 1] = {0};
    TranslateUint8ToCharacter(tmpSign, sizeof(tmpSign), sign, sizeof(sign));

    uint16_t tlvLen = GetRegisterTLVLen();
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("dev reg qm_malloc error\n");
        goto FAIL;
    }
    (void)memset(tlv, 0, tlvLen);

    const uint8_t *key = (const uint8_t *)((g_verifyMethod == BLE_PRODUCT_VERIFY) ? g_prdKey : g_licenseKey);
    uint16_t keyLen = (g_verifyMethod == BLE_PRODUCT_VERIFY) ? strlen(g_prdKey) : strlen(g_licenseKey);
    uint8_t keyType = (g_verifyMethod == BLE_PRODUCT_VERIFY) ? BLE_MESSAGE_TYPE_9 : BLE_MESSAGE_TYPE_10;
    BuildDeviceRegisterReq(sign, BLE_HMAC_SHA256_LEN * 2, key, keyLen, keyType, tlv);
    if (g_regId != NULL)
    {
        qm_free(g_regId);
        g_regId = NULL;
    }
    if (g_verifyCode != NULL)
    {
        qm_free(g_verifyCode);
        g_verifyCode = NULL;
    }

    bleFrame->cid = BLE_DEVICE_CONNECT_REGISTER;
    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;
    return 0;
FAIL:
    if (g_authCode != NULL)
    {
        qm_free(g_authCode);
        g_authCode = NULL;
    }
    if (g_regId != NULL)
    {
        qm_free(g_regId);
        g_regId = NULL;
    }
    if (g_verifyCode != NULL)
    {
        qm_free(g_verifyCode);
        g_verifyCode = NULL;
    }
    return -1;
}

static int8_t ParamNegotiate(BleFrame *bleFrame)
{
    g_connect_type = 0;
    g_linkStatus = 0;

    if (bleFrame->data[0] == BLE_MESSAGE_TYPE_7)
    {
        g_connectType = bleFrame->data[2];
    }
    return ParamNegotiateRsp(bleFrame);
}

static int8_t CertApproved(BleFrame *bleFrame)
{
    uint16_t pos = 0;
    // authCode
    uint16_t appAuthCodeLen = 0;
    uint8_t *appAuthCode = NULL;
    GetUint8ArrayFromTLV(bleFrame->data, &pos, &appAuthCode, &appAuthCodeLen);
    if (appAuthCodeLen != BLE_HMAC_SHA256_LEN)
    {
        qm_printf("invalid authCode len:0x%02x\n", appAuthCodeLen);
        return -1;
    }
    // random
    uint16_t appRandomLen = 0;
    uint8_t *appRandom = NULL;
    GetUint8ArrayFromTLV(bleFrame->data, &pos, &appRandom, &appRandomLen);

    if (appRandomLen != BLE_RANDOM_LEN)
    {
        qm_printf("invalid random len:0x%02x\n", appRandomLen);
        return -1;
    }

    int8_t ret = CertApprovedRsp(bleFrame, appAuthCode, appAuthCodeLen, appRandom, appRandomLen);
    if (appAuthCode != NULL)
    {
        qm_free(appAuthCode);
        appAuthCode = NULL;
    }
    if (appRandom != NULL)
    {
        qm_free(appRandom);
        appRandom = NULL;
    }
    if(g_connect_type == 1)
    {
        g_linkStatus = 1;
    }
    return ret;
}

static int8_t SetConfig(BleFrame *bleFrame)
{
    uint16_t pos = 0;
    // regId
    GetUint8ArrayFromTLV(bleFrame->data, &pos, &g_regId, &g_regIdLen);
    // verifyCode
    GetUint8ArrayFromTLV(bleFrame->data, &pos, &g_verifyCode, &g_verifyCodeLen);

    // 最大等待10秒，用户在设备上按键确认
    qm_printf("Waiting user to confirm the pairing request...\n");
    qm_printf("User confirmed\n");

    return GetDevRegChallengeCodeReq(bleFrame);
}

static int8_t GetDevChallengeCodeRsp(BleFrame *bleFrame)
{
    uint8_t pos = 0;
    // sn2: cloud
    pos += BLE_PROTOCOL_TYPE_LEN;
    g_sn2Len = bleFrame->data[pos];
    pos += BLE_PROTOCOL_LENGTH_LEN;
    g_sn2 = (uint8_t *)qm_malloc(g_sn2Len);
    if (g_sn2 == NULL)
    {
        qm_printf("qm_malloc error\n");
        return -1;
    }
    (void)memcpy(g_sn2, bleFrame->data + pos, g_sn2Len);
    return DeviceRegisterReq(bleFrame);
}

static int8_t DeviceRegisterRsp(BleFrame *bleFrame)
{
    if (bleFrame->data[0] == 0x08)
    {
        g_registerState = bleFrame->data[2];
        SetConfigRsp(bleFrame);
        if (g_registerState != BLE_DEVICE_REGISTER_SUCCEED)
        {
            qm_printf("======!!! REGISTER FAILED !!!======\n");
            void *handle = generic_serial_iot_handle_get();
            if(handle == NULL){
                return;
            }
            iot_generic_reset(handle);
            return -1;
        }
        // 更新连接类型
        g_connectType = BLE_CONNECT_REQ;
        // g_authCode写入flash中
        g_writeAuthCodeFunc(g_authCode, BLE_AUTH_CODE_LEN);
        // 更新广播内容
        SetAdvData();
        qm_printf("======!!! REGISTER SUCCEED !!!======\n");
        g_linkStatus = 1;
    }
    return 0;
}

static int8_t DeleteDevice(BleFrame *bleFrame)
{
    if (bleFrame->data[2] != BLE_DEVICE_DELETE)
    {
        qm_printf("delete device value error:0x%02x\n", bleFrame->data[2]);
        return -1;
    }
    // 删除绑定设备
    g_rmvBondDevFunc();
    bleFrame->data = NULL;
    bleFrame->dataLen = 0;
    return 0;
}

// 内部申请内存放在bleFrame.data中，发送完成记得释放
static int8_t BuildErrorCode(BleFrame *bleFrame, CONTROL_DEVICE_ERRCODE ctrlDevType)
{
    uint16_t tlvLen = BLE_PROTOCOL_TYPE_LEN + BLE_PROTOCOL_LENGTH_LEN + BLE_ERRCODE_LEN;
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc error code error\n");
        return -1;
    }
    uint8_t pos = 0;
    tlv[pos++] = BLE_ERRCODE_TYPE;
    tlv[pos++] = BLE_ERRCODE_LEN;
    for (uint8_t i = 0; i < BLE_ERRCODE_LEN; ++i)
    {
        tlv[pos++] = ((ctrlDevType >> (8 * (sizeof(ctrlDevType) - i - 1))) & 0x000000FF);
    }

    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;
    return 0;
}

static int8_t GetDeviceProperty(uint8_t service, uint8_t property, uint8_t **buf, uint16_t *pos)
{
    if (g_getPropertyFunc == NULL)
    {
        qm_printf("register get property function first\n");
        return -1;
    }
    return g_getPropertyFunc(service, property, buf, pos);
}

static int8_t SetDeviceProperty(uint8_t service, uint8_t property, uint8_t *buf, uint16_t bufLen)
{
    if (g_setPropertyFunc == NULL)
    {
        qm_printf("register set property function first\n");
        return -1;
    }
    return g_setPropertyFunc(service, property, buf, bufLen);
}

static int8_t AckWriteReport(uint8_t service, uint8_t property, uint8_t *buf, uint16_t bufLen)
{
    if (g_ackWriteReportPropertyFunc == NULL)
    {
        qm_printf("register set property function first\n");
        return -1;
    }
    return g_ackWriteReportPropertyFunc(service, property, buf, bufLen);
}

static void ProcDevCtrlReadCore(BLE_DEV_CTRL_READ_GET_TYPE procType, uint8_t *svcLens, BleFrame *bleFrame, uint8_t *rsp,
                                uint16_t *rspLen)
{
    uint8_t *req = bleFrame->data;
    uint16_t reqLen = bleFrame->dataLen;
    uint16_t rspPos = 0;
    uint16_t reqPos = 0;
    uint16_t rspSvcLen = 0; // 每个svc的len，存储进svcLens
    for (uint16_t i = 0, k = 0; i < reqLen && k < BLE_READ_PROPERTY_MAX_COUNT; ++k)
    {
        uint8_t svcType = req[reqPos];
        if (procType == BLE_DEV_CTRL_READ_GET_BUF)
        {
            rsp[rspPos++] = svcType;                     // svcIndex
            SetTLVLengthValue(svcLens[k], rsp, &rspPos); // 将svcLen填入rspBuffer
        }
        ++reqPos;                                             // svcIndex
        uint16_t reqSvcLen = GetTLVLengthValue(req, &reqPos); // svcLen
        if (procType == BLE_DEV_CTRL_READ_GET_LEN)
        {
            rspPos += BLE_PROTOCOL_TYPE_LEN;
            rspPos += (reqSvcLen > BLE_LENGTH_ONE_BIT_MAX) ? BLE_PROTOCOL_VALUE_LEN_TWO : BLE_PROTOCOL_VALUE_LEN_ONE;
            rspSvcLen = 0;
        }
        for (uint16_t j = 0; j < reqSvcLen; j += 2)
        {
            uint8_t propType = req[reqPos++];
            if (procType == BLE_DEV_CTRL_READ_GET_LEN)
            {
                ++rspPos;
                ++rspSvcLen;
            }
            uint16_t propLen = 0;
            uint8_t *propValue = NULL;
            if (GetDeviceProperty(svcType, propType, &propValue, &propLen) != 0)
            {
                qm_printf("get dev prop error:0x%02x\n", propType);
                return;
            }
            if (procType == BLE_DEV_CTRL_READ_GET_BUF)
            {
                rsp[rspPos++] = propType;
                SetTLVLengthValue(propLen, rsp, &rspPos);
                (void)memcpy(rsp + rspPos, propValue, propLen);
                rspPos += propLen;
            }
            qm_free(propValue);
            ++reqPos; // 加上property的len的偏移
            if (procType == BLE_DEV_CTRL_READ_GET_LEN)
            {
                rspPos += (propLen > BLE_LENGTH_ONE_BIT_MAX) ? 2 : 1;
                rspPos += propLen;
                rspSvcLen += (propLen > BLE_LENGTH_ONE_BIT_MAX) ? 2 : 1;
                rspSvcLen += propLen;
            }
        }
        if (procType == BLE_DEV_CTRL_READ_GET_LEN)
        {
            svcLens[k] = rspSvcLen;
        }
        i = reqPos;
    }

    *rspLen = rspPos;
    return;
}

static int8_t ProcDevCtrlRead(BleFrame *bleFrame)
{
    uint8_t svcLens[BLE_READ_PROPERTY_MAX_COUNT] = {0};
    uint16_t tlvLen = 0;
    ProcDevCtrlReadCore(BLE_DEV_CTRL_READ_GET_LEN, svcLens, bleFrame, NULL, &tlvLen);
    uint8_t *tlv = (uint8_t *)qm_malloc(tlvLen);
    if (tlv == NULL)
    {
        qm_printf("qm_malloc error\n");
        return -1;
    }
    (void)memset(tlv, 0, tlvLen);

    ProcDevCtrlReadCore(BLE_DEV_CTRL_READ_GET_BUF, svcLens, bleFrame, tlv, &tlvLen);
    bleFrame->data = tlv;
    bleFrame->dataLen = tlvLen;
    return 0;
}

static int8_t ProcDevCtrlWrite(BleFrame *bleFrame)
{
    CONTROL_DEVICE_ERRCODE errCode = CONTROL_DEVICE_OK;
    uint16_t pos = 0;
    uint8_t svcType = 0;
    uint8_t propType = 0;
    uint16_t propLen = 0;
    uint8_t *frame_temp = NULL;
    for (uint8_t i = 0; i < bleFrame->dataLen;)
    {
        svcType = bleFrame->data[pos];
        ++pos;                                                        // svcIndex
        uint16_t reqSvcLen = GetTLVLengthValue(bleFrame->data, &pos); // svcLen
        for (uint16_t j = 0; j < reqSvcLen;)
        {
            propType = bleFrame->data[pos++];
            j += BLE_PROTOCOL_TYPE_LEN;
            propLen = GetTLVLengthValue(bleFrame->data, &pos);
            j += (propLen > BLE_LENGTH_ONE_BIT_MAX) ? BLE_PROTOCOL_VALUE_LEN_TWO : BLE_PROTOCOL_VALUE_LEN_ONE;
            if (SetDeviceProperty(svcType, propType, bleFrame->data + pos, propLen) != 0)
            {
                qm_printf("set dev prop error:0x%02x\n", propType);
                errCode = CONTROL_DEVICE_SET_PROPERTY_FAIL;
                break;
            }
            frame_temp = bleFrame->data + pos;
            pos += propLen;
            j += propLen;
        }
        if (errCode != CONTROL_DEVICE_OK)
        {
            break;
        }
        i = pos;
    }

    if (BuildErrorCode(bleFrame, errCode) != 0)
    {
        return (-1);
    }

    AckWriteReport(svcType, propType, frame_temp, propLen);

    return 0;
}

static ProcFrameFunc g_procFrameFunc[] = {
    {BLE_DEVICE_CONNECT_SERVICE, BLE_DEVICE_CONNECT_PARAMETER_NEGOTIATE, ParamNegotiate},
    {BLE_DEVICE_CONNECT_SERVICE, BLE_DEVICE_CONNECT_CERTIFICATE_APPROVED, CertApproved},
    {BLE_DEVICE_CONNECT_SERVICE, BLE_DEVICE_CONNECT_SET_CONFIG, SetConfig},
    {BLE_DEVICE_CONNECT_SERVICE, BLE_DEVICE_CONNECT_GET_CHALLENGE_CODE, GetDevChallengeCodeRsp},
    {BLE_DEVICE_CONNECT_SERVICE, BLE_DEVICE_CONNECT_REGISTER, DeviceRegisterRsp},
    {BLE_DEVICE_CONNECT_SERVICE, BLE_DEVICE_CONNECT_DELETE, DeleteDevice},
    {BLE_DEVICE_CONTROL_SERVICE, BLE_DEVICE_CONTROL_READ, ProcDevCtrlRead},
    {BLE_DEVICE_CONTROL_SERVICE, BLE_DEVICE_CONTROL_WRITE, ProcDevCtrlWrite},
};

static int8_t ProcRecvAppData(BleFrame *bleFrame)
{
    uint16_t count = sizeof(g_procFrameFunc) / sizeof(ProcFrameFunc);
    for (uint16_t i = 0; i < count; ++i)
    {
        if (g_procFrameFunc[i].sid == bleFrame->sid && g_procFrameFunc[i].cid == bleFrame->cid)
        {
            return g_procFrameFunc[i].procFunc(bleFrame);
        }
    }

    if (bleFrame->sid == BLE_DEVICE_OTA_SERVICE)
    {
        return ProcOtaAppData(bleFrame);
    }
    qm_printf("unknow sid:0x%02x, cid:0x%02x\n", bleFrame->sid, bleFrame->cid);
    return -1;
}

void SetSalt(const char *checkSalt, const char *calculateSalt)
{
    if (checkSalt == NULL || calculateSalt == NULL || strlen(checkSalt) > BLE_SALT_LEN ||
        strlen(calculateSalt) > BLE_SALT_LEN)
    {
        qm_printf("set checkSalt and calculateSalt error\n");
        return;
    }
    (void)memcpy(g_checkSalt, checkSalt, strlen(checkSalt));
    (void)memcpy(g_calculateSalt, calculateSalt, strlen(calculateSalt));
}

void SetPrdId(const char *prdId)
{
    if (prdId == NULL || strlen(prdId) > BLE_PRODUCT_ID_LEN)
    {
        qm_printf("set prdId error\n");
        return;
    }
    (void)memcpy(g_prdId, prdId, strlen(prdId));
}

void SetPrdKey(const char *prdKey)
{
    if (prdKey == NULL || strlen(prdKey) > BLE_PRODUCT_KEY_LEN)
    {
        qm_printf("set prdKey error\n");
        return;
    }
    (void)memcpy(g_prdKey, prdKey, strlen(prdKey));
}

void SetPrdSecret(const char *prdSecret)
{
    if (prdSecret == NULL || strlen(prdSecret) > BLE_PRODUCT_SECRET_LEN)
    {
        qm_printf("set prdSecret error\n");
        return;
    }
    (void)memcpy(g_prdSecret, prdSecret, strlen(prdSecret));
}

void SetLicenseKey(const char *licenseKey)
{
    if (licenseKey == NULL || strlen(licenseKey) > BLE_LICENSE_KEY_LEN)
    {
        qm_printf("set licenseKey error\n");
        return;
    }
    (void)memcpy(g_licenseKey, licenseKey, strlen(licenseKey));
}

void SetLicenseSecret(const char *licenseSecret)
{
    if (licenseSecret == NULL || strlen(licenseSecret) > BLE_LICENSE_SECRET_LEN)
    {
        qm_printf("set licenseSecret error\n");
        return;
    }
    (void)memcpy(g_licenseSecret, licenseSecret, strlen(licenseSecret));
}

void SetSubPrdId(const char *subPrdId)
{
    if (subPrdId == NULL || strlen(subPrdId) > BLE_SUB_PRODUCT_ID_LEN)
    {
        qm_printf("set subPrdId error\n");
        return;
    }
    (void)memcpy(g_subPrdId, subPrdId, strlen(subPrdId));
    return;
}

void SetMacAddress(const uint8_t *macAddress, uint8_t macAddressLen)
{
    if (macAddress == NULL || macAddressLen == 0 || macAddressLen != BLE_MAC_LEN)
    {
        qm_printf("set mac para invalid\n");
        return;
    }
    (void)memcpy(g_macAddr, macAddress, macAddressLen);
    return;
}

void SetAuthCodeId(uint8_t id)
{
    g_authCodeId = id;
}

void SetMtu(uint16_t mtu)
{
    g_mtu = mtu;
}

void SetMaxFrameSize(uint16_t maxFrameSize)
{
    g_maxFrameSize = maxFrameSize;
}

void SetSendPkgInterval(uint16_t sendInterval)
{
    g_sendInterval = sendInterval;
}

void SetVerifyMethod(BLE_VERIFY_METHOD verifyMethod)
{
    g_verifyMethod = verifyMethod;
}

void RegisterHonorSendFunc(BleSendFunc bleSendFunc)
{
    if (bleSendFunc == NULL)
    {
        qm_printf("set ble send func error\n");
        return;
    }
    g_bleHonorSendFunc = bleSendFunc;
}

void RegisterAckWriteReportFunc(AckWriteReportProperty ackWriteReportFunc)
{
    if (ackWriteReportFunc == NULL)
    {
        qm_printf("set ble send func error\n");
        return;
    }
    g_ackWriteReportPropertyFunc = ackWriteReportFunc;
}

void RegisterNormalSendFunc(BleSendFunc bleSendFunc)
{
    if (bleSendFunc == NULL)
    {
        qm_printf("set ble send func error\n");
        return;
    }
    g_bleNormalSendFunc = bleSendFunc;
}

void RegisterGenRandomFunc(GenerateRandom genRandomFunc)
{
    if (genRandomFunc == NULL)
    {
        qm_printf("set random func error\n");
        return;
    }
    g_genRandomFunc = genRandomFunc;
}

void RegisterReadAuthCodeFunc(ReadAuthCode readAuthCodeFunc)
{
    if (readAuthCodeFunc == NULL)
    {
        qm_printf("set read func error\n");
        return;
    }
    g_readAuthCodeFunc = readAuthCodeFunc;
}

void RegisterWriteAuthCodeFunc(WriteAuthCode writeAuthCodeFunc)
{
    if (writeAuthCodeFunc == NULL)
    {
        qm_printf("set write func error\n");
        return;
    }
    g_writeAuthCodeFunc = writeAuthCodeFunc;
}

void RegisterGetPropertyFunc(GetProperty getPropertyFunc)
{
    if (getPropertyFunc == NULL)
    {
        qm_printf("set property func error\n");
        return;
    }
    g_getPropertyFunc = getPropertyFunc;
}

void RegisterSetPropertyFunc(SetProperty setPropertyFunc)
{
    if (setPropertyFunc == NULL)
    {
        qm_printf("set property func error\n");
        return;
    }
    g_setPropertyFunc = setPropertyFunc;
}

void RegisterCfgAdvDataFunc(CfgAdvData cfgAdvDataFunc)
{
    if (cfgAdvDataFunc == NULL)
    {
        qm_printf("set adv data func error\n");
        return;
    }
    g_cfgAdvDataFunc = cfgAdvDataFunc;
}

void RegisterRmvBondDevFunc(RmvBondDev rmvBondDevFunc)
{
    if (rmvBondDevFunc == NULL)
    {
        qm_printf("set remove bond dev func error\n");
        return;
    }
    g_rmvBondDevFunc = rmvBondDevFunc;
}

static int8_t SendOtaFrame2App(void *frame)
{
    return SendFrame2App((BleFrame *)frame);
}

int8_t HonorBleInit(const BleInitParam *param, const BleInitCallback *callback, const BleInitOtaCallback *otaCallback)
{
    if (param == NULL || callback == NULL)
    {
        qm_printf("honor ble init error\n");
        return -1;
    }

    SetSalt(param->checkSalt, param->calculateSalt);
    SetPrdId(param->prdId);
    SetPrdKey(param->prdKey);
    SetPrdSecret(param->prdSecret);
    SetLicenseKey(param->licenseKey);
    SetLicenseSecret(param->licenseSecret);
    SetSubPrdId(param->subPrdId);
    SetMacAddress(param->macAddress, param->macAddressLen);
    SetAuthCodeId(param->authCodeId);
    SetMtu(param->mtu);
    SetMaxFrameSize(param->maxFrameSize);
    SetSendPkgInterval(param->interval);
    SetVerifyMethod(param->verifyMethod);

    RegisterHonorSendFunc(callback->bleHonorSendFunc);
    RegisterAckWriteReportFunc(callback->AckWriteReportPropertyFunc);
    RegisterNormalSendFunc(callback->bleNormalSendFunc);
    RegisterGenRandomFunc(callback->genRandomFunc);
    RegisterReadAuthCodeFunc(callback->readAuthCodeFunc);
    RegisterWriteAuthCodeFunc(callback->writeAuthCodeFunc);
    RegisterGetPropertyFunc(callback->getPropertyFunc);
    RegisterSetPropertyFunc(callback->setPropertyFunc);
    RegisterCfgAdvDataFunc(callback->cfgAdvDataFunc);
    RegisterRmvBondDevFunc(callback->rmvBondDevFunc);

    if (otaCallback != NULL)
    {
        // OTA所需回调接口
        RegisterOtaGetCurFwVerFunc(otaCallback->otaGetCurFwVer);
        RegisterOtaWritePkgFunc(otaCallback->otaWritePkgFunc);
        RegisterGetPkgSaveSizeFunc(otaCallback->getPkgSaveSizeFunc);
        RegisterOtaProcInstallFunc(otaCallback->otaProcInstallFunc);
        RegisterGetBatteryLevelFunc(otaCallback->getBatteryLevelFunc);

        RegisterMcuOtaEndFunc(otaCallback->mcuOtaEndFunc);
        RegisterMcuOtaStartFunc(otaCallback->mcuOtaStartFunc);
        RegisterOtaGetCurMcuVerFunc(otaCallback->otaGetCurMcuVer);
        RegisterMcuOtaRecvPkgFunc(otaCallback->mcuOtaPackageRecvFunc);
        RegisterMcuOtaProcInstallFunc(otaCallback->mcuOtaProcInstallFunc);
        RegisterNotifyMcuOtaExceptionFunc(otaCallback->notifyMcuOtaExceptionFunc);

        RegisterSendFrame2AppFunc(SendOtaFrame2App);
        RegisterGetUint8ArrayFromTLVFunc(GetUint8ArrayFromTLV);
    }
    // 注册完成，设置广播内容
    SetAdvData();

    return 0;
}

void HonorBleDeinit()
{
    (void)memset(g_checkSalt, 0, sizeof(g_checkSalt));
    (void)memset(g_calculateSalt, 0, sizeof(g_calculateSalt));
    (void)memset(g_prdId, 0, sizeof(g_prdId));
    (void)memset(g_prdKey, 0, sizeof(g_prdKey));
    (void)memset(g_prdSecret, 0, sizeof(g_prdSecret));
    (void)memset(g_licenseKey, 0, sizeof(g_licenseKey));
    (void)memset(g_licenseSecret, 0, sizeof(g_licenseSecret));
    (void)memset(g_subPrdId, 0, sizeof(g_subPrdId));

    g_bleHonorSendFunc = NULL;
    g_bleNormalSendFunc = NULL;
    g_genRandomFunc = NULL;
    g_readAuthCodeFunc = NULL;
    g_writeAuthCodeFunc = NULL;
    g_setPropertyFunc = NULL;
    g_getPropertyFunc = NULL;
    g_cfgAdvDataFunc = NULL;
    g_rmvBondDevFunc = NULL;

    return;
}

void dev_report_value(uint8_t serviceId, uint16_t serviceLen, const uint8_t *value,
                      BLE_DEVICE_REPORT_TYPE reportType)
{
    uint8_t head = 0x00;
    BleFrame bleFrame = {0};
    bleFrame.sid = BLE_DEVICE_CONTROL_SERVICE;
    bleFrame.cid = reportType;
    bleFrame.head = &head;
    bleFrame.headLen = sizeof(head);

    uint16_t dataLen = BLE_PROTOCOL_TYPE_LEN;
    dataLen += ((serviceLen > BLE_LENGTH_ONE_BIT_MAX) ? BLE_PROTOCOL_VALUE_LEN_TWO : BLE_PROTOCOL_VALUE_LEN_ONE);
    dataLen += serviceLen;

    uint8_t *data = (uint8_t *)qm_malloc(dataLen);
    if (data == NULL)
    {
        qm_printf("report prop error\n");
        return;
    }
    (void)memset(data, 0, dataLen);

    uint16_t pos = 0;
    data[pos++] = serviceId;
    SetTLVLengthValue(serviceLen, data, &pos);
    (void)memcpy(data + pos, value, serviceLen);
    pos += serviceLen;

    bleFrame.data = data;
    bleFrame.dataLen = dataLen;
    SendFrame2App(&bleFrame);
    qm_free(data);
    return;
}

static int8_t IsPrepared()
{
    if (g_bleHonorSendFunc == NULL || g_bleNormalSendFunc == NULL || g_genRandomFunc == NULL ||
        g_readAuthCodeFunc == NULL || g_writeAuthCodeFunc == NULL || g_setPropertyFunc == NULL ||
        g_getPropertyFunc == NULL)
    {
        qm_printf("complete register first\n");
        return -1;
    }
    return 0;
}

void ProcessFrame(BLE_SEND_TYPE sendType, const uint8_t *frame, uint16_t len)
{
    if (IsPrepared() != 0)
    {
        return;
    }

    // if (sendType == BLE_NORMAL_SEND) {
    //     RecvNormalFrameFromApp(frame, len);
    //     return;
    // }

    int8_t recvRet = RecvHonorFrameFromApp(frame, len);
    if (recvRet == 0 || recvRet == -1)
    {
        qm_printf("recv not finish or proc error\n");
        return;
    }

    BleFrame bleFrame = {0};
    // 透传方案head为0x00，profile方案head有一定长度，在函数中申请内存，消息回复完成需要释放
    if (UnpackRecvFrame(g_recvDataRecord.recvBuf, g_recvDataRecord.recvBufLen, &bleFrame) != 0)
    {
        qm_printf("unpack fail error\n");
        qm_free(g_recvDataRecord.recvBuf);
        return;
    }

    if (ProcRecvAppData(&bleFrame) == 0)
    {
        // 当前指向发送数据的内存，删除设备指令不需要回复消息，data也不用释放
        if (bleFrame.data != NULL)
        {
            SendFrame2App(&bleFrame);
            qm_free(bleFrame.data);
        }
    }

    if (bleFrame.headLen != 1)
    {
        qm_free(bleFrame.head);
    }
    qm_free(g_recvDataRecord.recvBuf);
    g_recvDataRecord.recvBuf = NULL;
    g_recvDataRecord.recvBufLen = 0;
    return;
}
