#pragma once

#include "qm_log.h"

#define QM_LINUX_STUB_WARN(tag, func) \
    QM_LOGW(tag, "[linux stub] %s not implemented", func)
