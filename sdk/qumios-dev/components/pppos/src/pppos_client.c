#include "pppos_client.h"

#if CONFIG_QM_PPPOS_CLIENT_SUPPORT == 1

#include "netif/ppp/pppos.h"
#include "netif/ppp/pppapi.h"
#include "netif/ppp/ppp.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "tcpip_adapter.h"

#include "qm_gpio.h"
#include "qm_log.h"
#include "qm_kernel.h"
#if CONFIG_QM_PPPOS_CLIENT_UART_SUPPORT == 1
#include "qm_uart.h"
#endif
#include "qm_errno.h"
#include "qm_work.h"

/** PPPOS client event base used by event-loop APIs. */
QM_EVENT_DEFINE_BASE(QM_EVENT_PPPOS);

#define LOG_TAG "pppos"

#define CONFIG_GSM_INTERNET_USER      ""
#define CONFIG_GSM_INTERNET_PASSWORD  ""
#define CONFIG_GSM_APN         "playmetric"


#ifndef QM_PPPOS_MODULE_POWERKEY_PIN
#define QM_PPPOS_MODULE_POWERKEY_PIN        23
#endif


static int module_init(void *handle);
static void module_reset(void *handle);

static void module_network_timeout(qm_timer_t *timer, void *arg);
static void module_network_timer_start(void *handle);
static void module_network_timer_stop(void *handle);


#define MODULE_NETWORK_TIMEOUT  15000
#define PPPOS_BUF_SIZE       (2048)

const char *PPP_User = "";
const char *PPP_Pass = "";
const char *PPP_ApnATReq = "AT+CGDCONT=1,\"IP\",\"" \
                           CONFIG_GSM_APN \
                           "\"";

typedef struct {
    
    /* The PPP control block */
    ppp_pcb *ppp;
    /* The PPP IP interface */
    struct netif ppp_netif;
    char *buffer;
    qm_timer_t network_timer;
    int is_timer_start;
#if CONFIG_QM_PPPOS_CLIENT_UART_SUPPORT == 1
    qm_uart_dev_t uart_dev;
#endif
    int (*read)(void *handle, uint8_t *buf, uint32_t len, uint32_t timeout);
    int (*write)(void *handle, const uint8_t *buf, uint32_t len, uint32_t timeout);
    qm_gpio_dev_t powerkey_dev;
    int errcode;
    qm_task_t task;

}pppos_ctx_t;

typedef void (*cmd_handler)(char *cmd);
static void csq_handler(char *cmd);

static pppos_ctx_t g_pppos_ctx = {0};

typedef struct {
    const char *cmd;
    uint16_t cmdSize;
    const char *cmdResponseOnOk;
    uint32_t timeoutMs;
    uint8_t retry_count;
    cmd_handler handler;
} GSM_Cmd;

#define GSM_OK_Str "OK"

GSM_Cmd GSM_MGR_InitCmds[] = {
    {
        .cmd = "AT\r",
        .cmdSize = sizeof("AT\r") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 1000,
        .retry_count = 15,
        .handler = NULL,
    },
    {
        .cmd = "ATE0\r",
        .cmdSize = sizeof("ATE0\r") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 100,
        .retry_count = 2,
        .handler = NULL,
    },
    {
        .cmd = "AT+CPIN?\r",
        .cmdSize = sizeof("AT+CPIN?\r") - 1,
        .cmdResponseOnOk = "CPIN: READY",
        .timeoutMs = 100,
        .retry_count = 2,
        .handler = NULL,
    },
    {
        //AT+CGDCONT=1,"IP","apn"
        .cmd = "AT+CGDCONT=1,\"IP\",\"CMNET\"\r",
        .cmdSize = sizeof("AT+CGDCONT=1,\"IP\",\"CMNET\"\r") - 1,
        .cmdResponseOnOk = GSM_OK_Str,
        .timeoutMs = 200,
        .retry_count = 2,
        .handler = NULL,
    },
    
    {
        .cmd = "AT+CSQ\r",
        .cmdSize = sizeof("AT+CSQ\r") - 1,
        .cmdResponseOnOk = "CSQ",
        .timeoutMs = 100,
        .retry_count = 2,
        .handler = csq_handler,
    },

    {
        .cmd = "ATDT*99***1#\r",
        .cmdSize = sizeof("ATDT*99***1#\r") - 1,
        .cmdResponseOnOk = "CONNECT",
        .timeoutMs = 1000,
        .retry_count = 2,
        .handler = NULL,
    }
    /*

    {
        .cmd = "ATD*99#\r",
        .cmdSize = sizeof("ATD*99#\r") - 1,
        .cmdResponseOnOk = "CONNECT",
        .timeoutMs = 2000,
        .retry_count = 2,
    }
*/

};

#define GSM_MGR_InitCmdsSize  (sizeof(GSM_MGR_InitCmds)/sizeof(GSM_Cmd))

static void csq_handler(char *cmd)
{
    char *tmp = NULL;
    int len = 0;
    char str_csq[4]={0};
    int csq = 0;
    char *flag="CSQ: ";
    int i = 0;
    tmp = strstr(cmd, flag);
    tmp += strlen(flag);
    if(!tmp){
        return;
    }
    len = strlen(tmp);
    while(len--){
        if(*tmp == ','){
            csq = atoi(str_csq);
            QM_LOGD(LOG_TAG, "csq:%d", csq);
            break;
        }else{
            str_csq[i++] = *tmp;
            if(i == 4){
                break;
            }
            tmp++;
        }
    }
}

/* PPP status callback example */
static void ppp_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    struct netif *pppif = ppp_netif(pcb);
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)ctx;

    pppos_ctx->errcode = err_code;

    switch (err_code) {
        case PPPERR_NONE: {
            QM_LOGD(LOG_TAG, "status_cb: Connected\n");

                
    #if PPP_IPV4_SUPPORT
            QM_LOGD(LOG_TAG, "   our_ipaddr  = %s\n", ipaddr_ntoa(&pppif->ip_addr));
            QM_LOGD(LOG_TAG, "   his_ipaddr  = %s\n", ipaddr_ntoa(&pppif->gw));
            QM_LOGD(LOG_TAG, "   netmask     = %s\n", ipaddr_ntoa(&pppif->netmask));
    #endif /* PPP_IPV4_SUPPORT */
    #if PPP_IPV6_SUPPORT
            QM_LOGD(LOG_TAG, "   our6_ipaddr = %s\n", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
    #endif /* PPP_IPV6_SUPPORT */

            qm_event_post(QM_EVENT_PPPOS, PPPOS_CLIENT_EVENT_LINK_ON, NULL, 0, QM_NO_WAIT);

            module_network_timer_stop(pppos_ctx);
            break;
        }
        case PPPERR_PARAM: {
            QM_LOGE(LOG_TAG, "status_cb: Invalid parameter\n");
            break;
        }
        case PPPERR_OPEN: {
            QM_LOGE(LOG_TAG, "status_cb: Unable to open PPP session\n");
            break;
        }
        case PPPERR_DEVICE: {
            QM_LOGE(LOG_TAG, "status_cb: Invalid I/O device for PPP\n");
            break;
        }
        case PPPERR_ALLOC: {
            QM_LOGE(LOG_TAG, "status_cb: Unable to allocate resources\n");
            break;
        }
        case PPPERR_USER: {
            QM_LOGE(LOG_TAG, "status_cb: User interrupt\n");
            break;
        }
        case PPPERR_CONNECT: {
            QM_LOGE(LOG_TAG, "status_cb: Connection lost\n");
            qm_event_post(QM_EVENT_PPPOS, PPPOS_CLIENT_EVENT_LINK_OFF, NULL, 0, QM_NO_WAIT);
            break;
        }
        case PPPERR_AUTHFAIL: {
            QM_LOGE(LOG_TAG, "status_cb: Failed authentication challenge\n");
            break;
        }
        case PPPERR_PROTOCOL: {
            QM_LOGE(LOG_TAG, "status_cb: Failed to meet protocol\n");
            break;
        }
        case PPPERR_PEERDEAD: {
            QM_LOGE(LOG_TAG, "status_cb: Connection timeout\n");
            qm_event_post(QM_EVENT_PPPOS, PPPOS_CLIENT_EVENT_LINK_OFF, NULL, 0, QM_NO_WAIT);
            break;
        }
        case PPPERR_IDLETIMEOUT: {
            QM_LOGE(LOG_TAG, "status_cb: Idle Timeout\n");
            break;
        }
        case PPPERR_CONNECTTIME: {
            QM_LOGE(LOG_TAG, "status_cb: Max connect time reached\n");
            break;
        }
        case PPPERR_LOOPBACK: {
            QM_LOGE(LOG_TAG, "status_cb: Loopback detected\n");
            break;
        }
        default: {
            QM_LOGE(LOG_TAG, "status_cb: Unknown error code %d\n", err_code);
            break;
        }
    }

    /*
     * This should be in the switch case, this is put outside of the switch
     * case for example readability.
     */
    /*
     * Try to reconnect in 30 seconds, if you need a modem chatscript you have
     * to do a much better signaling here ;-)
     */
    //ppp_connect(pcb, 30);
    /* OR ppp_listen(pcb); */          

}

static uint32_t ppp_output_callback(ppp_pcb *pcb, uint8_t *data, uint32_t len, void *ctx)
{
    qm_err_t ret = QM_EOK;
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)ctx;
    //QM_LOGD(LOG_TAG, "PPP tx len %d", len);
    ret =  pppos_ctx->write(&pppos_ctx->uart_dev, data, len, 0);
    return ret;
}


static void pppos_client_task(void *arg)
{
    int gsmCmdIter = 0;
    int retry_count = 0;
    int is_init = 0;
    int len = 0;

    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)arg;

init:
    module_reset(pppos_ctx);

    while (1) {
        //init gsm
        gsmCmdIter = 0;
        while (1) {
              
            retry_count = 0;
            while (1) {
                QM_LOGD(LOG_TAG, "pppos send %s", GSM_MGR_InitCmds[gsmCmdIter].cmd);
                pppos_ctx->write(&pppos_ctx->uart_dev, (const uint8_t *)GSM_MGR_InitCmds[gsmCmdIter].cmd, GSM_MGR_InitCmds[gsmCmdIter].cmdSize, 0);
                memset(pppos_ctx->buffer, 0, PPPOS_BUF_SIZE);
                len =  pppos_ctx->read(&pppos_ctx->uart_dev, (uint8_t*)pppos_ctx->buffer, PPPOS_BUF_SIZE, GSM_MGR_InitCmds[gsmCmdIter].timeoutMs);
                if (len > 0) {
                    QM_LOGD(LOG_TAG, "pppos recv %s", pppos_ctx->buffer);
                }

                retry_count ++;
                if (strstr(pppos_ctx->buffer, GSM_MGR_InitCmds[gsmCmdIter].cmdResponseOnOk) != NULL) {
                    if(GSM_MGR_InitCmds[gsmCmdIter].handler){
                        GSM_MGR_InitCmds[gsmCmdIter].handler(pppos_ctx->buffer);
                    }
                    break;
                }

                if (retry_count >= GSM_MGR_InitCmds[gsmCmdIter].retry_count) {
                    QM_LOGE(LOG_TAG, "Gsm Init Error");
                    goto init;
                }
            }
            gsmCmdIter++;

            if (gsmCmdIter >= GSM_MGR_InitCmdsSize) {
                break;
            }
        }
        
        QM_LOGD(LOG_TAG, "Gsm init end");

        //module_network_timer_stop(pppos_ctx);
        //module_network_timer_start(pppos_ctx);

        if(!is_init){

            is_init = 1;

            pppos_ctx->ppp = pppapi_pppos_create(&pppos_ctx->ppp_netif, ppp_output_callback, ppp_status_cb, pppos_ctx);
            QM_LOGD(LOG_TAG, "After pppapi_pppos_create");
            if (pppos_ctx->ppp == NULL) {
                QM_LOGE(LOG_TAG, "Error init pppos");
                return;
            }

            pppapi_set_default(g_pppos_ctx.ppp);

            QM_LOGD(LOG_TAG, "After pppapi_set_default");

            /* Ask the peer for up to 2 DNS server addresses */
            ppp_set_usepeerdns(pppos_ctx->ppp, 1);

            /* Auth configuration */
            pppapi_set_auth(pppos_ctx->ppp, PPPAUTHTYPE_PAP, PPP_User, PPP_Pass);

            QM_LOGD(LOG_TAG, "After pppapi_set_auth");
        }

        pppapi_connect(pppos_ctx->ppp, 0);

        QM_LOGD(LOG_TAG, "After pppapi_connect");

        pppos_ctx->errcode = 0;

        while (1) {
            
            len = pppos_ctx->read(&pppos_ctx->uart_dev, (uint8_t*)pppos_ctx->buffer, PPPOS_BUF_SIZE, 50);
            if (len > 0) {
                //QM_LOGD(LOG_TAG, "PPP rx len %d", len);
                pppos_input_tcpip(pppos_ctx->ppp, (uint8_t*)pppos_ctx->buffer, len);
            }
                    
            if(pppos_ctx->errcode == PPPERR_PEERDEAD || pppos_ctx->errcode == PPPERR_CONNECT){
                QM_LOGD(LOG_TAG, "module error and restart");
                goto init;
            }
        }

    }
}

static int uart_read(void *handle, uint8_t *buf, uint32_t len, uint32_t timeout)
{
    return qm_uart_read((qm_uart_dev_t*)handle, buf, len, timeout);
}

static int uart_write(void *handle, const uint8_t *buf, uint32_t len, uint32_t timeout)
{
    return qm_uart_write((qm_uart_dev_t*)handle, buf, len, timeout);
}

static int module_init(void *handle)
{
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)handle;
#if CONFIG_QM_PPPOS_CLIENT_UART_SUPPORT == 1
    qm_uart_dev_t uart = {
        .config = {
            .baud_rate = CONFIG_QM_PPPOS_CLIENT_UART_BAUDATE,
            .data_bits = QM_UART_DATA_8_BITS,
            .parity    = QM_UART_PARITY_NONE,
            .pins.tx_pin = CONFIG_QM_PPPOS_CLIENT_UART_TX_PIN,
            .pins.rx_pin = CONFIG_QM_PPPOS_CLIENT_UART_RX_PIN,
            .stop_bits = QM_UART_STOP_BITS_1 ,
        },
        .port = CONFIG_QM_PPPOS_CLIENT_UART_PORT,
    };

    memcpy(&pppos_ctx->uart_dev, &uart, sizeof(qm_uart_dev_t));
    qm_uart_init(&pppos_ctx->uart_dev);

    pppos_ctx->read = uart_read;
    pppos_ctx->write = uart_write;
#endif

    pppos_ctx->buffer = (char *) qm_malloc(PPPOS_BUF_SIZE);
    if(g_pppos_ctx.buffer == NULL){
        return -QM_EINVAL;
    }

    pppos_ctx->powerkey_dev.port = QM_PPPOS_MODULE_POWERKEY_PIN;
    pppos_ctx->powerkey_dev.config.mode = QM_GPIO_MODE_OUTPUT;
    pppos_ctx->powerkey_dev.config.pull_en = QM_GPIO_PULLDOWN_ONLY;
    qm_gpio_init(&pppos_ctx->powerkey_dev);

    return QM_EOK;
}

static void module_reset(void *handle)
{
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)handle;
    qm_msleep(500);
    qm_gpio_set_level(&pppos_ctx->powerkey_dev, 1);
    qm_msleep(1500);
    qm_gpio_set_level(&pppos_ctx->powerkey_dev, 0);
    QM_LOGD(LOG_TAG, "4G mudule reset");
}


static void module_network_timer_start(void *handle)
{
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)handle;
    if(!pppos_ctx->is_timer_start){  
        qm_timer_start(&pppos_ctx->network_timer);
        pppos_ctx->is_timer_start = 1;
    }
}

static void module_network_timer_stop(void *handle)
{
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)handle;
    if(pppos_ctx->is_timer_start){  
        qm_timer_stop(&pppos_ctx->network_timer);
        pppos_ctx->is_timer_start = 0;
    }
}

static void module_network_timeout(qm_timer_t *timer, void *arg)
{
    pppos_ctx_t *pppos_ctx = (pppos_ctx_t*)arg;
}

int pppos_client_init(void)
{
    qm_err_t ret = QM_EOK;
    tcpip_adapter_init();

    module_init(&g_pppos_ctx);

    qm_timer_new(&g_pppos_ctx.network_timer, module_network_timeout, &g_pppos_ctx, MODULE_NETWORK_TIMEOUT, 1);
    if(ret != QM_EOK){
        goto __exit;
    }
    qm_task_new(&g_pppos_ctx.task, "pppos", pppos_client_task, &g_pppos_ctx, CONFIG_PPPOS_CLIENT_TASK_SIZE, CONFIG_PPPOS_CLIENT_TASK_PRIO);
    if(ret != QM_EOK){
        goto __exit;
    }
    return QM_EOK;

__exit:
    if(qm_timer_is_valid(&g_pppos_ctx.network_timer)){
        qm_timer_free(&g_pppos_ctx.network_timer);
    }
    return ret;
}


#endif
