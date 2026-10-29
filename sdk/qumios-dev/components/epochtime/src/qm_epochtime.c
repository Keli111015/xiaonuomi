#include "qm_types.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_udp.h"
#include "qm_epochtime.h"
#include "qm_config.h"

#define LOG_TAG  "ntp"

static volatile uint32_t g_qm_epochtime_log_flags = QM_EPOCHTIME_KCONFIG_LOG_FLAGS;

uint32_t qm_epochtime_get_log_flags(void)
{
    return g_qm_epochtime_log_flags;
}

int qm_epochtime_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_EPOCHTIME_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_epochtime_log_flags = log_flags;
    return QM_EOK;
}

#if CONFIG_QM_EPOCHTIME_SUPPORT

#ifndef CONFIG_QM_ALI_NTP_SUPPORT
#define CONFIG_QM_ALI_NTP_SUPPORT      1
#endif

#ifndef CONFIG_QM_TENCENT_NTP_SUPPORT
#define CONFIG_QM_TENCENT_NTP_SUPPORT  0
#endif

#if CONFIG_QM_ALI_NTP_SUPPORT
#define ALIYUN_NTP_SERVER       "ntp%d.aliyun.com"
#endif

#if CONFIG_QM_TENCENT_NTP_SUPPORT
#define TENCENT_NTP_SERVER      "time%d.cloud.tencent.com"
#endif

#ifndef QM_NTP_PORT
#define QM_NTP_PORT                123
#endif

#if !CONFIG_QM_ALI_NTP_SUPPORT && !CONFIG_QM_TENCENT_NTP_SUPPORT
    #error must specify ntp server
#endif


typedef struct{
    int num;
    char *server;
}ntp_server_list_t;

static ntp_server_list_t ntp_server_list[] = {
#if CONFIG_QM_ALI_NTP_SUPPORT
    {7, ALIYUN_NTP_SERVER},
#endif
#if CONFIG_QM_TENCENT_NTP_SUPPORT
    {5, TENCENT_NTP_SERVER},
#endif
};

#define NTP_SERVER_LIST_NUM  (sizeof(ntp_server_list)/sizeof(ntp_server_list[0]))

#define LI                      0
#define VN                      3
#define MODE                    3
#define STRATUM                 0
#define POLL                    4
#define PREC                   -6

#define JAN_1970                0x83aa7e80 /* 2208988800 1970 - 1900 in seconds */

/* How to multiply by 4294.967296 quickly (and not quite exactly)
 * without using floating point or greater than 32-bit integers.
 * If you want to fix the last 12 microseconds of error, add in
 * (2911*(x))>>28)
 */
#define NTPFRAC(x)              (4294 * (x) + ((1981 * (x)) >> 11))

/* The reverse of the above, needed if we want to set our microsecond
 * clock (via settimeofday) based on the incoming time in NTP format.
 * Basically exact.
 */
#define USEC(x)                 (((x) >> 12) - 759 * ((((x) >> 10) + 32768) >> 16))

/* Converts NTP delay and dispersion, apparently in seconds scaled
 * by 65536, to microseconds.  RFC1305 states this time is in seconds,
 * doesn't mention the scaling.
 * Should somehow be the same as 1000000 * x / 65536
 */
#define sec2u(x)                ((x) * 15.2587890625)


struct ntptime_t {
    uint32_t coarse;
    uint32_t fine;
};

struct ntp_packet_t {
    int li;
    int vn;
    int mode;
    int stratum;
    int poll;
    int prec;
    int delay;
    int disp;
    int refid;
};

/**
 * implement of htonl and ntohl
 */
#define BigLittleSwap(A)        ((((uint32_t)(A) & 0xff000000) >> 24) | \
                                (((uint32_t)(A) & 0x00ff0000) >> 8) | \
                                (((uint32_t)(A) & 0x0000ff00) << 8) | \
                                (((uint32_t)(A) & 0x000000ff) << 24))

/* return 1 if big endian */
static int _check_endian(void)
{
    union {
        uint32_t i;
        uint8_t c[4];
    } u;

    u.i = 0x12345678;
    return (0x12 == u.c[0]);
}

static uint32_t _htonl(uint32_t h)
{
    return _check_endian() ? h : BigLittleSwap(h);
}

static uint32_t _ntohl(uint32_t n)
{
    return _check_endian() ? n : BigLittleSwap(n);
}

static int _get_packet(unsigned char *packet, int *len)
{
    uint32_t data[12];
    struct timeval_t now = {0, 0};

    if (*len < 48) {
        QM_EPOCHTIME_LOGE(LOG_TAG, "packet buf too short!\n");
        return -QM_EINVAL;
    }

    memset(packet, 0, *len);

    data[0] = _htonl((LI << 30) | (VN << 27) | (MODE << 24) |
                     (STRATUM << 16) | (POLL << 8) | (PREC & 0xff));
    data[1] = _htonl(1 << 16);  /* Root Delay (seconds) */
    data[2] = _htonl(1 << 16);  /* Root Dispersion (seconds) */
    data[10] = _htonl(now.tv_sec + JAN_1970); /* Transmit Timestamp coarse */
    data[11] = _htonl(NTPFRAC(now.tv_usec));  /* Transmit Timestamp fine */

    memcpy(packet, data, 48);
    *len = 48;

    return 0;
}

#define NTP_DEBUG

static void _rfc1305_parse_timeval(unsigned char *read_buf, struct timeval_t *tv)
{
/* straight out of RFC-1305 Appendix A */
    struct ntp_packet_t ntp_packet;
    struct ntptime_t xmttime;
#ifdef NTP_DEBUG
    struct ntptime_t reftime, orgtime, rectime;
#endif
    memset(&ntp_packet, 0, sizeof(struct ntp_packet_t));

#define Data(i) _ntohl(((unsigned int *)read_buf)[i])
    ntp_packet.li      = Data(0) >> 30 & 0x03;
    ntp_packet.vn      = Data(0) >> 27 & 0x07;
    ntp_packet.mode    = Data(0) >> 24 & 0x07;
    ntp_packet.stratum = Data(0) >> 16 & 0xff;
    ntp_packet.poll    = Data(0) >>  8 & 0xff;
    ntp_packet.prec    = Data(0)       & 0xff;
    if (ntp_packet.prec & 0x80) ntp_packet.prec |= 0xffffff00;
    ntp_packet.delay   = Data(1);
    ntp_packet.disp    = Data(2);
    ntp_packet.refid   = Data(3);

#ifdef NTP_DEBUG
    reftime.coarse = Data(4);
    reftime.fine   = Data(5);
    orgtime.coarse = Data(6);
    orgtime.fine   = Data(7);
    rectime.coarse = Data(8);
    rectime.fine   = Data(9);
#endif
    xmttime.coarse = Data(10);
    xmttime.fine   = Data(11);
#undef Data

#ifdef NTP_DEBUG
    QM_EPOCHTIME_LOGD(LOG_TAG, QM_EPOCHTIME_LOG_PAYLOAD,
              "LI=%d  VN=%d  Mode=%d  Stratum=%d  Poll=%d  Precision=%d\n",
              ntp_packet.li, ntp_packet.vn, ntp_packet.mode,
              ntp_packet.stratum, ntp_packet.poll, ntp_packet.prec);
    QM_EPOCHTIME_LOGD(LOG_TAG, QM_EPOCHTIME_LOG_PAYLOAD,
              "Delay=%.1f  Dispersion=%.1f  Refid=%u.%u.%u.%u\n",
              sec2u(ntp_packet.delay), sec2u(ntp_packet.disp),
              ntp_packet.refid >> 24 & 0xff, ntp_packet.refid >> 16 & 0xff,
              ntp_packet.refid >> 8 & 0xff, ntp_packet.refid & 0xff);
    QM_EPOCHTIME_LOGD(LOG_TAG, QM_EPOCHTIME_LOG_PAYLOAD,
              "Reference %u.%.6u\n", reftime.coarse - JAN_1970, USEC(reftime.fine));
    QM_EPOCHTIME_LOGD(LOG_TAG, QM_EPOCHTIME_LOG_PAYLOAD,
              "Originate %u.%.6u\n", orgtime.coarse - JAN_1970, USEC(orgtime.fine));
    QM_EPOCHTIME_LOGD(LOG_TAG, QM_EPOCHTIME_LOG_PAYLOAD,
              "Receive   %u.%.6u\n", rectime.coarse - JAN_1970, USEC(rectime.fine));
    QM_EPOCHTIME_LOGD(LOG_TAG, QM_EPOCHTIME_LOG_PAYLOAD,
              "Transmit  %u.%.6u\n", xmttime.coarse - JAN_1970, USEC(xmttime.fine));
#endif

    tv->tv_sec = xmttime.coarse - JAN_1970;
    tv->tv_usec = USEC(xmttime.fine);
}

static int _get_timestamp_from_ntp(const char *host, struct timeval_t *tv)
{
    int fd;
    int ret = -1;
    unsigned char write_buf[48] = {0};
    int write_len = sizeof(write_buf);
    unsigned char read_buf[1500] = {0};

    fd = (int)qm_udp_client_create((char *)host, QM_NTP_PORT);
    if (fd < 0) {
        QM_EPOCHTIME_LOGE(LOG_TAG, "udp create error!");
        return -QM_EIO;
    }

    ret = _get_packet(write_buf, &write_len);
    if (ret < 0) {
        QM_EPOCHTIME_LOGE(LOG_TAG, "get_packet error!");
        goto __exit;
    }

    ret = qm_udp_write(fd, (char*)write_buf, write_len, 1000);
    if (ret < 0) {
        QM_EPOCHTIME_LOGE(LOG_TAG, "udp write error!");
        goto __exit;
    }

    ret = qm_udp_readtimeout(fd, (char*)read_buf, sizeof(read_buf), 3000);
    if (ret < 0) {
        QM_EPOCHTIME_LOGE(LOG_TAG, "udp read error!");
        goto __exit;
    }

    _rfc1305_parse_timeval(read_buf, tv);

__exit:
    qm_udp_close(fd);
    return ret;
}

int qm_get_epoch_time_from_ntp(struct timeval_t *time)
{
    int ret = 0;
    char ntp_server[32] = {0};
    int j = 1;
    int i = 0;

    if(time == NULL){
        return -QM_EINVAL;
    }

    for(i = 0; i < NTP_SERVER_LIST_NUM; i++){
        if(ntp_server_list[i].num != 0){
            for (j = 1; j <= ntp_server_list[i].num; j ++) {
                snprintf(ntp_server, 32, ntp_server_list[i].server, j);
                ret = _get_timestamp_from_ntp(ntp_server, time);
                if (ret > 0) {
                    goto __exit;
                }
            }
        }else{
            snprintf(ntp_server, 32, "%s", ntp_server_list[i].server);
            ret = _get_timestamp_from_ntp(ntp_server, time);
            if (ret > 0) {
                goto __exit;
            }
        }
    }

__exit:
    if(ret > 0){
        return QM_EOK;
    }else{
        return QM_ERROR;
    }
}

#endif
