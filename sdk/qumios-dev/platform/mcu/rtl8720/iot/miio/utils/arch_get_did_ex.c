/**
 * Copyright (C) 2013-2015
 *
 * @file   arch_get_did_ex.c
 *
 * @remark
 *
 */

#include "arch_chip.h"
#include "arch_get_did_ex.h"

uint64_t arch_get_did_ex(void)
{
    uint64_t did = 0;
    arch_get_did(&did);
    return did;
}
