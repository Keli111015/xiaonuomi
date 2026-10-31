#include "study_voice_guard.h"

#include <string.h>

bool study_voice_guard_is_pomodoro_cmd(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    bool has_obj = (strstr(text, "番茄钟") != NULL ||
                    strstr(text, "番茄时钟") != NULL ||
                    strstr(text, "番茄") != NULL);
    bool has_act = (strstr(text, "暂停") != NULL ||
                    strstr(text, "恢复") != NULL ||
                    strstr(text, "继续") != NULL ||
                    strstr(text, "退出") != NULL ||
                    strstr(text, "结束") != NULL ||
                    strstr(text, "停止") != NULL ||
                    strstr(text, "关闭") != NULL ||
                    strstr(text, "开启") != NULL ||
                    strstr(text, "开始") != NULL ||
                    strstr(text, "打开") != NULL);

    /* 明确带有 "番茄钟" 或 "番茄时钟" */
    if (strstr(text, "番茄钟") != NULL || strstr(text, "番茄时钟") != NULL) {
        return true;
    }

    /* 带有 "番茄" 且带有控制动作 (如 "暂停番茄", "结束番茄", "退出番茄") */
    if (has_obj && has_act) {
        return true;
    }

    /* 纯短控制动词 (全句仅为 "暂停", "恢复", "继续", "结束", "退出" 等) */
    if (strcmp(text, "暂停") == 0 || strcmp(text, "暂停。") == 0 ||
        strcmp(text, "恢复") == 0 || strcmp(text, "恢复。") == 0 ||
        strcmp(text, "继续") == 0 || strcmp(text, "继续。") == 0 ||
        strcmp(text, "结束") == 0 || strcmp(text, "结束。") == 0 ||
        strcmp(text, "退出") == 0 || strcmp(text, "退出。") == 0) {
        return true;
    }

    return false;
}

bool study_voice_guard_is_status_tts(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return false;
    }
    if (strstr(text, "番茄时钟已") != NULL ||
        strstr(text, "番茄钟已") != NULL) {
        return true;
    }
    return false;
}
