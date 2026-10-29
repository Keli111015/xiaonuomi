
#include "wk2114.h"
#include "qm_log.h"
#include "qm_uart.h"
#include "qm_gpio.h"
#include "qm_kernel.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

#define LOG_TAG "wk2114"

#ifndef CONFIG_WK2114_TASK_PRO
#define CONFIG_WK2114_TASK_PRO       (18)
#endif

#ifndef CONFIG_WK2114_TASK_SIZE
#define CONFIG_WK2114_TASK_SIZE      (4096)
#endif

#define WK2114_QUEUE_SIZE           (16)

#ifndef CONFIG_QM_WK2114_MAX_NUM
#define CONFIG_QM_WK2114_MAX_NUM    (2)
#endif

typedef struct{
    int port;
    int baud;
    int buffer_size;
    uint8_t *rx_buf;
    uint8_t *tx_buf;
    qm_mutex_t rx_mutex;
    qm_ringbuf_t rx_ringbuf;
    qm_mutex_t tx_mutex;
    qm_ringbuf_t tx_ringbuf;
    qm_sem_t sem;
}qm_uart_ext_config_t;

typedef struct{
    int is_used;
    qm_mutex_t lock;
    qm_uart_dev_t *uart_dev;
    qm_gpio_dev_t isr_dev;
    qm_gpio_dev_t rst_dev;
    qm_queue_t queue_tx;
    qm_queue_t queue_rx;
    qm_uart_ext_config_t uart_ext_config[QM_WK2114_UART_MAX];
}qm_wk2114_handle_t;

static qm_wk2114_handle_t g_wk2114_handle[CONFIG_QM_WK2114_MAX_NUM] = {0};

static int uart_send_bytes(qm_uart_dev_t *uart_dev, uint8_t *data, int size)
{
    return qm_uart_write(uart_dev, data, size, 0);
}

static int uart_rec_bytes(qm_uart_dev_t *uart_dev, uint8_t *data, int size)
{
    return qm_uart_read(uart_dev, data, size, 1000);
}

/****************WkWriteGReg***********************************/
//函数功能：写全局寄存器函数（前提是该寄存器可写，
//某些寄存器如果你写1，可能会自动置1，具体见数据手册)
//参数：
//      greg:为全局寄存器的地址
//      dat:为写入寄存器的数据
//*************************************************************/
void wk_write_greg(qm_uart_dev_t *uart_dev, uint8_t greg, uint8_t dat)
{
    uint8_t cmd;
    cmd = 0 | greg;
    uart_send_bytes(uart_dev, &cmd, 1); //写指令，对于指令的构成见数据手册
    uart_send_bytes(uart_dev, &dat, 1); //写数据
}
/****************WkReadGReg***********************************/
//函数功能：读全局寄存器
//参数：
//      greg:为全局寄存器的地址
//      rec:返回的寄存器值
//************************************************************/
uint8_t wk_read_greg(qm_uart_dev_t *uart_dev, uint8_t greg)
{
    uint8_t cmd = 0, rec = 0;
    cmd = 0x40 | greg;
    uart_send_bytes(uart_dev, &cmd, 1); //写指令，对于指令的构成见数据手册
    uart_rec_bytes(uart_dev, &rec, 1);  //接收返回的寄存器值
    return rec;
}

/**************WkWriteSReg***********************************/
//函数功能:
//参数：port:为子串口
//      sreg:为子串口寄存器
//      dat:为写入寄存器的数据
//注意：在子串口被打通的情况下，向FDAT写入的数据会通过TX引脚输出
//***********************************************************/
void wk_write_sreg(qm_uart_dev_t *uart_dev, uint8_t port, uint8_t sreg, uint8_t dat)
{
    uint8_t cmd;
    cmd = 0x0 | ((port - 1) << 4) | sreg;
    uart_send_bytes(uart_dev, &cmd, 1); //写指令，对于指令的构成见数据手册
    uart_send_bytes(uart_dev, &dat, 1); //写数据
}

/*****************WkReadSReg***********************************/
//函数功能：读子串口寄存器
//参数：port为子串口端口号
//      sreg:为子串口寄存器地址
//      rec:返回的寄存器值
//************************ ***********************************/
uint8_t wk_read_sreg(qm_uart_dev_t *uart_dev, uint8_t port, uint8_t sreg)
{
    int ret = 0;
    uint8_t cmd = 0, rec = 0;
    cmd = 0x40 | ((port - 1) << 4) | sreg;
    uart_send_bytes(uart_dev, &cmd, 1); //写指令，对于指令的构成见数据手册
    ret = uart_rec_bytes(uart_dev, &rec, 1);  //接收返回的寄存器值
    //printf("wk_read_sreg: %d\n", ret);
    return rec;
}
/**************WkWriteSFifo***********************************/
//函数功能:向子串口fifo写入需要发送的数据
//参数：port:为子串口
//      *dat:写入数据
//      num：为写入数据的个数，单次不超过16个字节
//注意：通过该方式写入的数据，被直接写入子串口的缓存FIFO，然后被发送
//************************************************************/
void wk_write_sfifo(qm_uart_dev_t *uart_dev, uint8_t port, uint8_t *dat, uint8_t num)
{
    uint8_t cmd = 0;
    cmd = 0x80 | ((port - 1) << 4) | (num - 1);
    uart_send_bytes(uart_dev, &cmd, 1);  //写指令,对于指令构成见数据手册
    uart_send_bytes(uart_dev, dat, num); //写数据
}

/**************************************WkReadSFifo***********************************/
//函数功能:从子串口的fifo中读出接收到的数据
//参数：port:为子串口
//      *rec：接收到的数据
//      num：读出的数据个数。
//注意：通过该方式读出子串口缓存中的数据。单次不能超过16个。
//***************************************************************************************/
void wk_read_sfifo(qm_uart_dev_t *uart_dev, uint8_t port, uint8_t *rec, uint8_t num)
{
    uint8_t cmd = 0;
    cmd = 0xc0 | ((port - 1) << 4) | (num - 1);
    uart_send_bytes(uart_dev, &cmd, 1);
    uart_rec_bytes(uart_dev, rec, num);
}

/**********************Wk_BaudAdaptive*******************************************/
//函数功能:主串口波特率匹配
/*********************************************************************************/
void wk_baud_adaptive(qm_wk2114_handle_t *wk2114_handle)
{
    uint8_t cmd = 0x55;
    /*初始化复位引脚为高电平*/
    qm_gpio_set_level(&wk2114_handle->rst_dev, 1);
    qm_msleep(10);
    /*拉低RST引脚10ms，复位WK芯片*/
    qm_gpio_set_level(&wk2114_handle->rst_dev, 0);
    qm_msleep(10);
    /*拉高RST引脚，完成复位，wk进入正常工作状态*/
    qm_gpio_set_level(&wk2114_handle->rst_dev, 1);
    qm_msleep(20);
    /*复位完成以后，发送0x55，实现主串口波特率匹配*/
    uart_send_bytes(wk2114_handle->uart_dev, &cmd, 1);
    qm_msleep(100);
}

/******************************WkInit*******************************************/
//函数功能：初始化子串口F
/*********************************************************************************/
void wk_init(qm_uart_dev_t *uart_dev, uint8_t port)
{
    uint8_t gena = 0, grst = 0, gier = 0, sier = 0, scr = 0;
    //使能子串口时钟
    gena = wk_read_greg(uart_dev, WK2XXX_GENA);
    QM_LOGI(LOG_TAG, "gena = 0x%x.\n", gena);
    gena = gena | (1 << (port - 1));
    wk_write_greg(uart_dev, WK2XXX_GENA, gena);
    //软件复位子串口
    grst = wk_read_greg(uart_dev, WK2XXX_GRST);
    QM_LOGI(LOG_TAG, "grst = 0x%x.\n", grst);
    grst = grst | (1 << (port - 1));
    wk_write_greg(uart_dev, WK2XXX_GRST, grst);
    //使能子串口总中断
    gier = wk_read_greg(uart_dev, WK2XXX_GIER);
    QM_LOGI(LOG_TAG, "gier = 0x%x.\n", gier);
    gier = gier | (1 << (port - 1));
    wk_write_greg(uart_dev, WK2XXX_GIER, gier);
    //使能子串口接收触点中断和超时中断
    sier = wk_read_sreg(uart_dev, port, WK2XXX_SIER);
    QM_LOGI(LOG_TAG, "sier = 0x%x.\n", sier);
    sier |= WK2XXX_RFTRIG_IEN | WK2XXX_RXOUT_IEN;
    wk_write_sreg(uart_dev, port, WK2XXX_SIER, sier);
    //初始化FIFO和设置固定中断触点
    wk_write_sreg(uart_dev, port, WK2XXX_FCR, 0XFF);
    //设置任意中断触点，如果下面的设置有效，那么上面FCR寄存器中断的固定中断触点将失效
    wk_write_sreg(uart_dev, port, WK2XXX_SPAGE, 1);   //切换到page1
    wk_write_sreg(uart_dev, port, WK2XXX_RFTL, 0X40); //设置接收触点为64个字节
    wk_write_sreg(uart_dev, port, WK2XXX_TFTL, 0X10); //设置发送触点为16个字节
    wk_write_sreg(uart_dev, port, WK2XXX_SPAGE, 0);   //切换到page0
    //使能子串口的发送和接收使能
    scr = wk_read_sreg(uart_dev, port, WK2XXX_SCR);
    QM_LOGI(LOG_TAG, "scr = 0x%x.\n", scr);
    scr |= WK2XXX_TXEN | WK2XXX_RXEN;
    wk_write_sreg(uart_dev, port, WK2XXX_SCR, scr);
}

/******************************Wk_DeInit*******************************************/
//函数功能：初始化子串口
/*********************************************************************************/
void wk_deinit(qm_uart_dev_t *uart_dev, uint8_t port)
{
    uint8_t gena, grst, gier;
    //关闭子串口总时钟
    gena = wk_read_greg(uart_dev, WK2XXX_GENA);
    gena = gena & (~(1 << (port - 1)));
    wk_write_greg(uart_dev, WK2XXX_GENA, gena);
    //使能子串口总中断
    gier = wk_read_greg(uart_dev, WK2XXX_GIER);
    gier = gier & (~(1 << (port - 1)));
    wk_write_greg(uart_dev, WK2XXX_GIER, gier);
    //软件复位子串口
    grst = wk_read_greg(uart_dev, WK2XXX_GRST);
    grst = grst | (1 << (port - 1));
    wk_write_greg(uart_dev, WK2XXX_GRST, grst);
}

/**************************Wk_SetBaud*******************************************************/
//函数功能：设置子串口波特率函数、此函数中波特率的匹配值是根据11.0592Mhz下的外部晶振计算的
// port:子串口号
// baud:波特率大小.波特率表示方式，
/**************************Wk2114SetBaud*******************************************************/
uint8_t wk_setbaud(qm_uart_dev_t *uart_dev, uint8_t port, uint32_t baudrate)
{
    uint32_t temp, freq;
    uint8_t scr;
    uint8_t baud1, baud0, pres;
    freq = 24000000; /*芯片外部时钟频率*/
    if (freq >= (baudrate * 16))
    {
        temp = (freq) / (baudrate * 16);
        temp = temp - 1;
        baud1 = (uint8_t)((temp >> 8) & 0xff);
        baud0 = (uint8_t)(temp & 0xff);
        temp = (((freq % (baudrate * 16)) * 100) / (baudrate));
        pres = (temp + 100 / 2) / 100;
        QM_LOGI(LOG_TAG,"Wk_SetBaud---freq:%d, baudrate:%d\n", freq, baudrate);
        QM_LOGI(LOG_TAG,"Wk_SetBaud---baud1:%x, baud0:%x, pres:%x\n", baud1, baud0, pres);
        //关掉子串口收发使能
        scr = wk_read_sreg(uart_dev, port, WK2XXX_SCR);
        wk_write_sreg(uart_dev, port, WK2XXX_SCR, 0);
        //设置波特率相关寄存器
        wk_write_sreg(uart_dev, port, WK2XXX_SPAGE, 1); //切换到page1
        wk_write_sreg(uart_dev, port, WK2XXX_BAUD1, baud1);
        wk_write_sreg(uart_dev, port, WK2XXX_BAUD0, baud0);
        wk_write_sreg(uart_dev, port, WK2XXX_PRES, pres);
        wk_write_sreg(uart_dev, port, WK2XXX_SPAGE, 0); //切换到page0
        //使能子串口收发使能
        wk_write_sreg(uart_dev, port, WK2XXX_SCR, scr);
        return 0;
    }
    else
    {
        QM_LOGE(LOG_TAG,"Wk_SetBaud error！！！！\n");
        return 1;
    }
}

/**************************WK_RS485*******************************************************/
//函数功能:设置子串口RS485的收发转换函数，使用RTS引脚控制485电平转换芯片的收发
// port:子串口号
//
//注意：只有WK2168/WK2204支持该功能
/**************************WK_RS485*******************************************************/
void wk_rs485(qm_uart_dev_t *uart_dev, uint8_t port)
{
    wk_write_sreg(uart_dev, port, WK2XXX_RS485, 0x02); //
    wk_write_sreg(uart_dev, port, WK2XXX_SPAGE, 1);    //切换到page1
    wk_write_sreg(uart_dev, port, WK2XXX_RTSDLY, 0X10);
    wk_write_sreg(uart_dev, port, WK2XXX_SPAGE, 0); //切换到page0
}

/**************************WK_TxLen*******************************************/
//函数功能:获取子串口发送FIFO剩余空间长度
// port:端口号
// 返回值：发送FIFO剩余空间长度
/**************************WK_Len********************************************/
int wk_txlen(qm_uart_dev_t *uart_dev, uint8_t port)
{
    uint8_t fsr, tfcnt;
    int len = 0;
    fsr = wk_read_sreg(uart_dev, port, WK2XXX_FSR);
    tfcnt = wk_read_sreg(uart_dev, port, WK2XXX_TFCNT);
    if (fsr & WK2XXX_TFULL)
    {
        len = 0;
    }
    else
    {
        len = 256 - tfcnt;
    }
    return len;
}

/**************************WK_TxChars*******************************************/
//函数功能:通过子串口发送固定长度数据
// port:端口号
// len:单次发送长度不超过256
//
/**************************WK_TxChars********************************************/
void wk_txchars(qm_uart_dev_t *uart_dev, uint8_t port, int len, uint8_t *sendbuf)
{
    int slen;
    while (len) //发送len长度的数据。
    {
        slen = len >= 16 ? 16 : len;
        len = len - slen;
        wk_write_sfifo(uart_dev, port, sendbuf, slen); //通过fifo方式发送数据
        sendbuf = sendbuf + slen;
    }
}

/**************************WK_RxChars*******************************************/
//函数功能:读取子串口fifo中的数据
// port:端口号
// recbuf:接收到的数据
// 返回值：接收数据的长度
/**************************WK_RxChars********************************************/
int wk_rxchars(qm_uart_dev_t *uart_dev, uint8_t port, uint8_t *recbuf)
{
    uint8_t fsr, rfcnt, rfcnt2, sifr;
    int offset = 0;
    int len = 0, num = 0;
    sifr = wk_read_sreg(uart_dev, port, WK2XXX_SIFR);
    if ((sifr & WK2XXX_RFTRIG_INT) || (sifr & WK2XXX_RXOVT_INT)) //有接收中断和接收超时中断
    {
        fsr = wk_read_sreg(uart_dev, port, WK2XXX_FSR);
        rfcnt = wk_read_sreg(uart_dev, port, WK2XXX_RFCNT);
        rfcnt2 = wk_read_sreg(uart_dev, port, WK2XXX_RFCNT);
        /*判断fifo中数据个数*/
        if (fsr & WK2XXX_RDAT) //判断是否为空，0为空，1不为空
        {
            if (!(rfcnt2 >= rfcnt))
            {
                rfcnt = rfcnt2;
            }
            len = (rfcnt == 0) ? 256 : rfcnt;
        }
        num = len;
        /*读取接收fifo中的数据*/
        while (len)
        {
            if (len >= 16)
            {
                wk_read_sfifo(uart_dev, port, recbuf + offset, 16);
                offset = offset + 16;
                len = len - 16;
            }
            else
            {
                wk_read_sfifo(uart_dev, port, recbuf + offset, len);
                len = 0;
            }
        }
    }
    else
    {
        num = 0;
    }
    return num;
}

static void ext_isr_handler(void *arg)
{
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)arg;
    qm_queue_send_from_isr(&wk2114_handle->queue_rx, wk2114_handle, sizeof(int));
}

static int uart_ext_ringbuf_send(qm_wk2114_handle_t *wk2114_handle, qm_wk2114_ext_port_t ext_port, uint8_t* buf, uint32_t size)
{
    qm_uart_ext_config_t *ext_config = NULL;
    if((wk2114_handle == NULL) || (ext_port >= QM_WK2114_UART_MAX) || (buf == NULL) || (size == 0)){
        return -QM_EINVAL;
    }

    ext_config = &wk2114_handle->uart_ext_config[ext_port];

    qm_mutex_lock(&ext_config->rx_mutex, QM_WAIT_FOREVER);
    qm_ringbuf_push(&ext_config->rx_ringbuf, buf, size);
    qm_mutex_unlock(&ext_config->rx_mutex);

    qm_sem_signal(&ext_config->sem);

    return QM_EOK;
}

void wk2114_ext_tx_task(void *arg)
{
    #define SEND_MAX_LEN    (256)

    int ext_port = 0;
    uint32_t recv_len = 0;
    int send_len = 0;
    int isempty = 0;
    int allow_send_len = 0;
    uint8_t send_data[SEND_MAX_LEN] = {0};
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t *)arg;
    qm_uart_ext_config_t *ext_config = NULL;

    while(1){
        if(qm_queue_recv(&wk2114_handle->queue_tx, &ext_port, &recv_len, QM_WAIT_FOREVER) < 0){
            continue;
        }

        ext_config = &wk2114_handle->uart_ext_config[ext_port];

        while(1){

#if 0
            qm_mutex_lock(&wk2114_handle->lock, QM_WAIT_FOREVER);
            allow_send_len = wk_txlen(wk2114_handle->uart_dev, ext_config->port + 1); 
            qm_mutex_unlock(&wk2114_handle->lock);
            printf("allow_send_len: %d\n", allow_send_len);
            if(allow_send_len == 0){
                qm_msleep(100);
                continue;
            }
#endif
            qm_mutex_lock(&ext_config->tx_mutex, QM_WAIT_FOREVER);
            send_len = qm_ringbuf_pop(&ext_config->tx_ringbuf, send_data, SEND_MAX_LEN);
            qm_mutex_unlock(&ext_config->tx_mutex);
            
            qm_mutex_lock(&wk2114_handle->lock, QM_WAIT_FOREVER);
            //qm_gpio_disable_irq(&wk2114_handle->isr_dev);
            wk_txchars(wk2114_handle->uart_dev, ext_config->port + 1, send_len, send_data);
            //qm_gpio_enable_irq(&wk2114_handle->isr_dev, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void*)wk2114_handle);
            qm_mutex_unlock(&wk2114_handle->lock);
            
            qm_mutex_lock(&ext_config->tx_mutex, QM_WAIT_FOREVER);
            isempty = qm_ringbuf_isempty(&ext_config->tx_ringbuf);
            qm_mutex_unlock(&ext_config->tx_mutex);
            if(isempty){
                //printf("send complete\n");
                break;
            }
        }
        memset(send_data, 0, SEND_MAX_LEN);
    }
}

static void wk2114_ext_rx_task(void *arg)
{
    #define RECV_BUF_LEN    (256)
    uint8_t gifr = 0;
    uint32_t rxlen_1 = 0, rxlen_2 = 0, rxlen_3 = 0, rxlen_4 = 0;
    int recv = 0;
    uint32_t recv_len = 0;
    uint8_t recvbuf[RECV_BUF_LEN] = {0};
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)arg;
    qm_uart_dev_t *uart_dev = wk2114_handle->uart_dev;

    while (1)
    {
        qm_queue_recv(&wk2114_handle->queue_rx, &recv, &recv_len, QM_WAIT_FOREVER);

        //qm_gpio_disable_irq(&wk2114_handle->isr_dev);

        qm_mutex_lock(&wk2114_handle->lock, QM_WAIT_FOREVER);

        gifr = wk_read_greg(uart_dev, WK2XXX_GIFR);
        do{
            if (gifr & WK2XXX_UT1INT) {                     
                rxlen_1 = wk_rxchars(uart_dev, 1, recvbuf); 
                uart_ext_ringbuf_send(wk2114_handle, QM_WK2114_UART_1, recvbuf, rxlen_1);
            }
            if (gifr & WK2XXX_UT2INT) {
                rxlen_2 = wk_rxchars(uart_dev, 2, recvbuf); 
                uart_ext_ringbuf_send(wk2114_handle, QM_WK2114_UART_2, recvbuf, rxlen_2); 
            }
            if (gifr & WK2XXX_UT3INT) {     
                rxlen_3 = wk_rxchars(uart_dev, 3, recvbuf); 
                uart_ext_ringbuf_send(wk2114_handle, QM_WK2114_UART_3, recvbuf, rxlen_3);
            }
            if (gifr & WK2XXX_UT4INT) {  
                rxlen_4 = wk_rxchars(uart_dev, 4, recvbuf);
                uart_ext_ringbuf_send(wk2114_handle, QM_WK2114_UART_4, recvbuf, rxlen_4);
            }
            gifr = wk_read_greg(uart_dev, WK2XXX_GIFR);
        } while (gifr & 0x0f);

        qm_mutex_unlock(&wk2114_handle->lock);

        memset(recvbuf, 0, RECV_BUF_LEN);
        //qm_gpio_enable_irq(&wk2114_handle->isr_dev, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void*)wk2114_handle);
    }
}

static qm_wk2114_handle_t *wk2114_handle_free_get(void)
{
    int index = 0;
    for(index = 0; index < CONFIG_QM_WK2114_MAX_NUM; index++){
        if(g_wk2114_handle[index].is_used){
            continue;
        }
        return &g_wk2114_handle[index];
    }
    return NULL;
}

void* qm_wk2114_init(qm_uart_dev_t *uart_dev, int gpio_isr, int gpio_rst)
{
    qm_task_t wk2114_task_rx = {0};
    qm_task_t wk2114_task_tx = {0};
    uint8_t dat = 0;
    qm_wk2114_handle_t *wk2114_handle = NULL;

    if(uart_dev == NULL){
        return NULL;
    }

    wk2114_handle = wk2114_handle_free_get();
    if(wk2114_handle == NULL){
        return NULL;
    }

    qm_uart_init(uart_dev);

    wk2114_handle->uart_dev = uart_dev;

    qm_mutex_new(&wk2114_handle->lock);

    qm_queue_new(&wk2114_handle->queue_tx, sizeof(int), WK2114_QUEUE_SIZE);
    qm_queue_new(&wk2114_handle->queue_rx, sizeof(int), WK2114_QUEUE_SIZE);

    wk2114_handle->rst_dev.port = gpio_rst;
    wk2114_handle->rst_dev.config.mode = QM_GPIO_MODE_OUTPUT;
    wk2114_handle->rst_dev.config.pull_en = QM_GPIO_PULLUP_ONLY;
    qm_gpio_init(&wk2114_handle->rst_dev);
    qm_gpio_set_level(&wk2114_handle->rst_dev, 1);

    wk2114_handle->isr_dev.port = gpio_isr;
    wk2114_handle->isr_dev.config.mode = QM_GPIO_MODE_INPUT;
    wk2114_handle->isr_dev.config.pull_en = QM_GPIO_PULLUP_ONLY;
    qm_gpio_init(&wk2114_handle->isr_dev);
    qm_gpio_enable_irq(&wk2114_handle->isr_dev, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, wk2114_handle);

    wk_baud_adaptive(wk2114_handle);
    qm_msleep(100);
    dat = wk_read_greg(uart_dev, WK2XXX_GENA);
    QM_LOGI(LOG_TAG,"gena = 0x%x.\n", dat);

    qm_task_new(&wk2114_task_rx, "wk2114 rx", wk2114_ext_rx_task, (void*)wk2114_handle, CONFIG_WK2114_TASK_SIZE, CONFIG_WK2114_TASK_PRO);
    qm_task_new(&wk2114_task_tx, "wk2114 tx", wk2114_ext_tx_task, (void*)wk2114_handle, CONFIG_WK2114_TASK_SIZE, CONFIG_WK2114_TASK_PRO);

    wk2114_handle->is_used = 1;

    return (void*)wk2114_handle;
}

int qm_wk2114_uart_init(void *handle, qm_wk2114_ext_port_t ext_port, int slave_baud, int buffer_size)
{
    qm_err_t ret = QM_EOK;
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)handle;
    uint8_t *rx_buf = NULL;
    uint8_t *tx_buf = NULL;

    qm_uart_ext_config_t *ext_config = NULL;

    if((wk2114_handle == NULL) || (ext_port >= QM_WK2114_UART_MAX)){
        return -QM_EINVAL;
    }

    ext_config = &wk2114_handle->uart_ext_config[ext_port];

    if((ext_config->port == ext_port) && 
        (ext_config->baud == slave_baud) &&
        (ext_config->buffer_size == buffer_size)){
        QM_LOGD(LOG_TAG,"Configuration not updated");
        return QM_EOK;
    }

    ext_config->buffer_size = buffer_size;

    ext_config->rx_buf = (uint8_t*)qm_malloc(buffer_size);
    if(ext_config->rx_buf == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    ext_config->tx_buf = (uint8_t*)qm_malloc(buffer_size);
    if(ext_config->tx_buf == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    ext_config->port = ext_port;
    ext_config->baud = slave_baud;

    memset(ext_config->rx_buf, 0, buffer_size);
    memset(ext_config->tx_buf, 0, buffer_size);

    if(qm_sem_new(&ext_config->sem, 0) != 0){
        ret = -QM_ERROR;
        goto __exit;
    }
    
    if(qm_mutex_new(&ext_config->tx_mutex) != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    if(qm_mutex_new(&ext_config->rx_mutex) != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    qm_ringbuf_init(&ext_config->rx_ringbuf, ext_config->rx_buf, buffer_size);
    qm_ringbuf_init(&ext_config->tx_ringbuf, ext_config->tx_buf, buffer_size);

    wk_init(wk2114_handle->uart_dev, ext_port + 1);
    wk_setbaud(wk2114_handle->uart_dev, ext_port + 1, slave_baud);

    return QM_EOK;


__exit:

    QM_LOGE(LOG_TAG,"wk2114 init fail");

    if(tx_buf == NULL){
        qm_free(tx_buf);
        tx_buf = NULL;
    }

    if(rx_buf == NULL){
        qm_free(rx_buf);
        rx_buf = NULL;
    }

    if(qm_mutex_is_valid(&ext_config->tx_mutex)){
        qm_mutex_free(&ext_config->tx_mutex);
    }
    if(qm_mutex_is_valid(&ext_config->rx_mutex)){
        qm_mutex_free(&ext_config->rx_mutex);
    }

    if(qm_sem_is_valid(&ext_config->sem)){
        qm_sem_free(&ext_config->sem);
    }

    return ret;
}

int qm_wk2114_uart_deinit(void *handle)
{
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)handle;

    if(handle == NULL){
        return -QM_EINVAL;
    }
    //TODO
    
    return QM_EOK;
}

int qm_wk2114_uart_write(void *handle, qm_wk2114_ext_port_t ext_port, uint8_t* buf, uint32_t length)
{
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)handle;
    qm_uart_ext_config_t *ext_config = NULL;

    if((wk2114_handle == NULL) || (ext_port >= QM_WK2114_UART_MAX) || (buf == NULL) || (length == 0)){
        return -QM_EINVAL;
    }

    ext_config = &wk2114_handle->uart_ext_config[ext_port];

    qm_mutex_lock(&ext_config->tx_mutex, QM_WAIT_FOREVER);
    qm_ringbuf_push(&ext_config->tx_ringbuf, buf, (int)length);
    qm_mutex_unlock(&ext_config->tx_mutex);

    qm_queue_send(&wk2114_handle->queue_tx, &ext_port, sizeof(int));

    return QM_EOK;
}

static uint32_t _platform_time_left(uint32_t t_end, uint32_t t_now)
{
    uint32_t res = 0;
    if(t_now - t_end < (UINT32_MAX/2)){
      return 0;
    }
    else{
      res = t_end-t_now;
      return res;
    }  
} 

int qm_wk2114_uart_read(void *handle, qm_wk2114_ext_port_t ext_port, uint8_t* buf, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    int isempty = 0;
    uint32_t t_left = 0;
    qm_utils_time_t time = {0}; 
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)handle;
    qm_uart_ext_config_t *ext_config = NULL;

    if((wk2114_handle == NULL) || (ext_port >= QM_WK2114_UART_MAX) || (buf == NULL) || (expect_size == 0)){
        return -QM_EINVAL;
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);

    ext_config = &wk2114_handle->uart_ext_config[ext_port];

     do{
        t_left = qm_utils_time_left(&time);

        qm_mutex_lock(&ext_config->rx_mutex, QM_WAIT_FOREVER);
        isempty = qm_ringbuf_isempty(&ext_config->rx_ringbuf);
        qm_mutex_unlock(&ext_config->rx_mutex);
        if(!isempty){
            qm_mutex_lock(&ext_config->rx_mutex, QM_WAIT_FOREVER);
            len = qm_ringbuf_pop(&ext_config->rx_ringbuf, buf + recv_len, expect_size - recv_len);
            qm_mutex_unlock(&ext_config->rx_mutex);
            if(len > 0){
                recv_len += len;
            }else{
                ret = -QM_ERROR;
                break;
            }
        }else{
            if(t_left == 0){
                break;
            }
            ret = qm_sem_wait(&ext_config->sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret;
}

int qm_wk2114_uart_clear(void *handle, qm_wk2114_ext_port_t ext_port)
{
    qm_wk2114_handle_t *wk2114_handle = (qm_wk2114_handle_t*)handle;
    qm_uart_ext_config_t *ext_config = NULL;
    if((wk2114_handle == NULL) || (ext_port >= QM_WK2114_UART_MAX)){
        return -QM_EINVAL;
    }

    ext_config = &wk2114_handle->uart_ext_config[ext_port];

    qm_mutex_lock(&ext_config->tx_mutex, QM_WAIT_FOREVER);
    qm_ringbuf_flush(&ext_config->tx_ringbuf);
    qm_mutex_unlock(&ext_config->tx_mutex);

    qm_mutex_lock(&ext_config->rx_mutex, QM_WAIT_FOREVER);
    qm_ringbuf_flush(&ext_config->rx_ringbuf);
    qm_mutex_unlock(&ext_config->rx_mutex);

    return QM_EOK;
}