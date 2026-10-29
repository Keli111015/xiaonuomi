#include "qm.h"
#include "qm_types.h"
#include "qm_sprintf.h"
#include "qm_log.h"

#define LOG_TAG  "sprintf"

void qm_application_start(void)
{
    int ret = 0;
    char buf[64] = {0};

    ret = qm_snprintf(buf, 64, "%d,%s,%.3f", 12, "hello", 1.23);
    QM_LOGD(LOG_TAG, "ret: %d, %s", ret, buf);
}