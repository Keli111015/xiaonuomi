#include "qm_errno.h"
#include "qm_log.h"
#include "qm_kernel.h"

#include "qm_gpio.h"
#include "lcd_bu9792.h"

#define LOG_TAG  "lcd_bu9792"

#ifdef CONFIG_LCD_BU9792_SUPPORT

/**
 * cfg
 *  4*Com 36*Seg   144bit 1/4 duty 1/2 or 1/3
 *  
 * init time series
 *  powon -> 100us -> stop -> start -> slave_addr -> ICSET -> DISCTL -> ADSET -> Displaydata -> stop
 * 
 * display on time series
 *  start -> slave_addr -> DISCTL -> BLKCTL -> APCTL -> MODESET -> stop
 * 
 * write ram time series
 *  start -> slave_addr -> DISCTL -> BLKCTL -> APCTL -> MODESET -> ADSET -> Displaydata -> stop
 * 
 * display off time series
 *  start -> slave_addr -> MODESET -> stop
 * 
 * 命令格式
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   x   x   x   x   x   x   x      
 *      C   0: next byte ram data
 *          1: next byte cmd
 * 
 *  MODESET
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   1   0   *   D   BS  *   *
 *      D3  0：display off  初始默认值
 *          1：display on
 *      BS  0：1/3 bias     初始默认值
 *          1：1/2 bias
 * 
 *  ADSET
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   0   0   AD4 AD3 AD2 AD1 AD0
 *      AD 是00000～10011(2)  
 * 
 *  ICSET
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   1   1   0   1   AD5 RST OSC
 *      AD5 仅仅设置01 不设置地址
 *      RST 0：不执行
 *          1：执行
 *      OSC 0：内部RC   初始默认值
 *          1：外部输入
 * 
 *  DISCTL
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   0   1   FR1 FR0 FM  SR1 SR0
 *      FR   : Power save mode FR
 *          0：normal   初始默认值
 *          1：mode 1    
 *          2：mode 2    
 *          3：mode 3    
 *          工作电流 Normal > mode1 > mode2 > mode3
 *      FM  0: LINE 翻转    初始默认值
 *          1: FRAME 翻转
 *      SR   : Power save mode FR
 *          0：mode 1                           
 *          1：mode 2    
 *          0：normal   初始默认值
 *          3：high mode    需要VDD - VLCD >= 3.0V
 *          工作电流 high mode > Normal > mode2 > mode1
 *                      1.8       1.0     0.67    0.5
 *      三者需结合硬件测试配置
 *      
 *  BLKCTL  闪烁模式
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   1   1   1   0   *   BK1 BK0
 *      BK  0：OFF      初始默认值
 *          1：0.5      
 *          2：1      
 *          3：2      
 * 
 *  APCTL 全屏像素控制 0/1 与ram数据无关 全1时优先OFF
 *      D7  D6  D5  D4  D3  D2  D1  D0
 *      C   1   1   1   1   1   AP  APN
 *      AP  0：normal      初始默认值
 *          1：all on      
 *      APN 0：normal      初始默认值
 *          1：all off      
 * 
*/

typedef struct
{
    uint8_t D7 : 1;
    uint8_t D6 : 1;
    uint8_t D5 : 1;
    uint8_t D4 : 1; 
    uint8_t D3 : 1; 
    uint8_t D2 : 1; 
    uint8_t D1 : 1; 
    uint8_t D0 : 1; 
} bu9792_bits;

typedef union 
{
    uint8_t data;
    bu9792_bits bits;
} bu9792_byte;

bu9792_byte bu9792_mode = {
    // .bits.D7 = 0,   // C 下一个数据 命令/数据控制 0数据 1命令
    .bits.D6 = 1,   // 固定
    .bits.D5 = 0,   // 固定
    // .bits.D4 = 0,   // 任意值
    .bits.D3 = 0,   // D 0 off 1 on
    .bits.D2 = 0,   // BS 0 1/3 1 1/2
    // .bits.D1 = 0,   // 任意值
    // .bits.D0 = 0,   // 任意值
};

#endif // CONFIG_LCD_BU9792_SUPPORT
