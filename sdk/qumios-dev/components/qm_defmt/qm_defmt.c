#include "qm.h"
#include "qm_defmt.h"
#include "syslog.h"

// 声明外部依赖
extern int qm_printf(const char * fmt, ...);

#define QM_LOG_PREFIX "``" 
#define QM_LOG_SEP    ","

// 定义缓冲区大小
// 128字节通常足够容纳 1个ID + 16个32位参数(16*9字符) + 分隔符
// 如果栈空间非常紧张，可以适当减小，比如 64
#define LOG_LINE_BUF_SIZE 64
static char buf[LOG_LINE_BUF_SIZE];

void qm_defmt_send_packet(uint32_t id, uint8_t arg_count, ...) {
    int offset = 0;
	memset(buf, 0, LOG_LINE_BUF_SIZE);

    // 1. 格式化帧头和 ID
    // snprintf 会自动处理缓冲区边界，防止溢出
    offset += snprintf(buf + offset, LOG_LINE_BUF_SIZE - offset, "%s%x", QM_LOG_PREFIX, id);

    // 2. 格式化可变参数
    va_list args;
    va_start(args, arg_count);

    for (int i = 0; i < arg_count; i++) {
        // 安全检查：如果缓冲区快满了，就停止追加，防止溢出 crash
        if (offset >= LOG_LINE_BUF_SIZE - 10) { 
            break; 
        }

        uint32_t val = va_arg(args, uint32_t);
        
        // 追加 ",val" 到缓冲区
        offset += snprintf(buf + offset, LOG_LINE_BUF_SIZE - offset, "%s%x", QM_LOG_SEP, val);
    }

    va_end(args);

    // 3. 追加换行符 (如果还有空间)
    if (offset < LOG_LINE_BUF_SIZE - 2) {
        buf[offset++] = '\n';
        buf[offset] = '\0';
    } else {
        // 强制在最后一个字符截断并换行，保证格式正确
        buf[LOG_LINE_BUF_SIZE - 2] = '\n';
        buf[LOG_LINE_BUF_SIZE - 1] = '\0';
    }

    // 4. 原子性输出
    // 此时 buf 是一个完整的字符串，例如 "``a,ce4,ffff\n"
    // 调用一次 qm_printf，只要底层串口发送是原子的或者有锁，就不会被打断
    qm_printf("%s", buf);
}
