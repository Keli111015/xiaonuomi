/*
 * Copyright (c) 2014-2016 Alibaba Group. All rights reserved.
 * License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "util_net.h"
#include "qm_config.h"
#include "qm_types.h"
#include "qm_tcp.h"
#include "qm_errno.h"
#include "qm_log.h"
#if CONFIG_QM_NETWORK_TLS_SUPPORT
#include "qm_tls.h"
#endif

#define LOG_TAG "util net"

/*** TCP connection ***/
static int read_tcp(util_network_pt pNetwork, char *buffer, uint32_t len, uint32_t timeout_ms)
{
    return qm_tcp_read((int)pNetwork->handle, buffer, len, timeout_ms);
}


static int write_tcp(util_network_pt pNetwork, const char *buffer, uint32_t len, uint32_t timeout_ms)
{
    return qm_tcp_write((int)pNetwork->handle, buffer, len, timeout_ms);
}

static int disconnect_tcp(util_network_pt pNetwork)
{
    if (NULL == pNetwork || (int)pNetwork->handle < 0) {
        return -QM_EINVAL;
    }

    qm_tcp_destroy((int)pNetwork->handle);
    pNetwork->handle = (void*)-1;
    return QM_EOK;
}

static int connect_tcp(util_network_pt pNetwork)
{
    if (NULL == pNetwork) {
        return -QM_EINVAL;
    }

    pNetwork->handle = (void*)qm_tcp_establish(pNetwork->pHostAddress, pNetwork->port);
    if ((int)pNetwork->handle < 0) {
        return -QM_EIO;
    }

    return QM_EOK;
}

/*** SSL connection ***/
#if CONFIG_QM_NETWORK_TLS_SUPPORT
static int read_tls(util_network_pt pNetwork, char *buffer, uint32_t len, uint32_t timeout_ms)
{
    if (NULL == pNetwork) {
        return -QM_EINVAL;
    }

    return qm_tls_read(pNetwork->handle, buffer, len, timeout_ms);
}

static int write_tls(util_network_pt pNetwork, const char *buffer, uint32_t len, uint32_t timeout_ms)
{
    if (NULL == pNetwork) {
        return -QM_EINVAL;
    }

    return qm_tls_write(pNetwork->handle, buffer, len, timeout_ms);
}

static int disconnect_tls(util_network_pt pNetwork)
{
    if (NULL == pNetwork) {
        return -QM_EINVAL;
    }

    qm_tls_destroy(pNetwork->handle);
    pNetwork->handle = NULL;

    return QM_EOK;
}

static int connect_tls(util_network_pt pNetwork)
{
    qm_tls_cfg_t tls_cfg = {0};

    if (NULL == pNetwork) {
        return -QM_EINVAL;
    }

    tls_cfg.ca_crt = pNetwork->ca_crt;
    tls_cfg.ca_crt_len = pNetwork->ca_crt_len;
    tls_cfg.client_crt = pNetwork->client_crt;
    tls_cfg.client_crt_len = pNetwork->client_crt_len;
    tls_cfg.client_key = pNetwork->client_key;
    tls_cfg.client_key_len = pNetwork->client_key_len;

    if (NULL != (pNetwork->handle = qm_tls_establish(pNetwork->pHostAddress, pNetwork->port, &tls_cfg))) {
        return QM_EOK;
    } else {
        /* TODO SHOLUD not remove this handle space */
        /* The space will be freed by calling disconnect_ssl() */
        /* util_memory_free((void *)pNetwork->handle); */
        return -QM_EIO;
    }

    return -QM_EIO;
}
#endif  /* #if CONFIG_QM_NETWORK_TLS_SUPPORT */

/****** network interface ******/
int util_net_read(util_network_pt pNetwork, char *buffer, uint32_t len, uint32_t timeout_ms)
{
    int ret = 0;

    if (NULL == pNetwork->ca_crt) {
        ret = read_tcp(pNetwork, buffer, len, timeout_ms);
#if CONFIG_QM_NETWORK_TLS_SUPPORT 
    } else {
        ret = read_tls(pNetwork, buffer, len, timeout_ms);
#endif
    }

    return ret;
}

int util_net_write(util_network_pt pNetwork, const char *buffer, uint32_t len, uint32_t timeout_ms)
{
    int     ret = 0;

    if (NULL == pNetwork->ca_crt) {
        ret = write_tcp(pNetwork, buffer, len, timeout_ms);
#if CONFIG_QM_NETWORK_TLS_SUPPORT
    } else {
        ret = write_tls(pNetwork, buffer, len, timeout_ms);
#endif
#if !CONFIG_QM_NETWORK_TLS_SUPPORT
    } else {
        QM_LOGE(LOG_TAG,
                "tls write requested but CONFIG_QM_NETWORK_TLS_SUPPORT is disabled host=%s port=%u",
                pNetwork->pHostAddress ? pNetwork->pHostAddress : "",
                (unsigned int)pNetwork->port);
        ret = -QM_ENOSYS;
#endif
    }
    return ret;
}

int util_net_disconnect(util_network_pt pNetwork)
{
    int     ret = 0;

    if (NULL == pNetwork->ca_crt) {
        ret = disconnect_tcp(pNetwork);
#if CONFIG_QM_NETWORK_TLS_SUPPORT
    } else {
       ret =  disconnect_tls(pNetwork);
#endif
#if !CONFIG_QM_NETWORK_TLS_SUPPORT
    } else {
        QM_LOGE(LOG_TAG,
                "tls disconnect requested but CONFIG_QM_NETWORK_TLS_SUPPORT is disabled host=%s port=%u",
                pNetwork->pHostAddress ? pNetwork->pHostAddress : "",
                (unsigned int)pNetwork->port);
        ret = -QM_ENOSYS;
#endif
    }
    return  ret;
}

int util_net_connect(util_network_pt pNetwork)
{
    int  ret = 0;

    if (!pNetwork) {
        return -QM_EINVAL;
    }

    if (NULL == pNetwork->ca_crt) {
        ret = connect_tcp(pNetwork);
#if CONFIG_QM_NETWORK_TLS_SUPPORT
    } else {
        ret = connect_tls(pNetwork);
#endif
#if !CONFIG_QM_NETWORK_TLS_SUPPORT
    } else {
        QM_LOGE(LOG_TAG,
                "tls connect requested but CONFIG_QM_NETWORK_TLS_SUPPORT is disabled host=%s port=%u ca_len=%d client_crt_len=%d client_key_len=%d",
                pNetwork->pHostAddress ? pNetwork->pHostAddress : "",
                (unsigned int)pNetwork->port,
                pNetwork->ca_crt_len,
                pNetwork->client_crt_len,
                pNetwork->client_key_len);
        ret = -QM_ENOSYS;
#endif
    }

    return ret;
}

int util_net_init(util_network_pt pNetwork, const char *host, uint16_t port, const char *ca_crt)
{
    if (!pNetwork || !host) {
        return -QM_EINVAL;
    }
    pNetwork->pHostAddress = host;
    pNetwork->port = port;
    pNetwork->ca_crt = ca_crt;

    if (NULL == ca_crt) {
        pNetwork->ca_crt_len = 0;
    } else {
        pNetwork->ca_crt_len = strlen(ca_crt);
    }

    pNetwork->handle = 0;
    pNetwork->read = util_net_read;
    pNetwork->write = util_net_write;
    pNetwork->disconnect = util_net_disconnect;
    pNetwork->connect = util_net_connect;

    return QM_EOK;
}

int util_net_set_client_cert_data(util_network_pt pNetwork, const char *client_crt, int len)
{
    if (!pNetwork) {
        return -QM_EINVAL;
    }

    pNetwork->client_crt = client_crt;
    pNetwork->client_crt_len = len;

    return QM_EOK;
}

int util_net_set_client_key_data(util_network_pt pNetwork, const char *client_key, int len)
{
    if (!pNetwork) {
        return -QM_EINVAL;
    }

    pNetwork->client_key = client_key;
    pNetwork->client_key_len = len;

    return QM_EOK;
}
