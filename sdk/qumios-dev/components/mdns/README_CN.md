# mDNS 组件

`mdns` 通过 QUMI UDP HAL 提供同步 DNS-SD 服务发现。它向
`224.0.0.251:5353` 发送带 mDNS 单播应答位的 PTR 查询，聚合一个或多个应答包中的
PTR/SRV/TXT/A 记录，将 SRV 目标解析为 IPv4 地址（无 A 记录时回退为目标
主机名），并通过回调逐个报告服务实例；回调同步执行，每个实例调用一次。

组件只负责数据包编解码和解析，不负责任务、缓存、重试策略或组播网卡选择。
目标 UDP HAL 必须支持 IPv4 组播发送，以及临时端口上的单播应答接收；不具备这些能力的目标应保持关闭 `MDNS_SUPPORT`。

单次查询最多保留 4 个服务实例、4 条 IPv4 A 记录、3 个响应包，以及每个服务
192 字节的 TXT RDATA。当前 QUMI 网络地址接口只支持 IPv4，因此暂不解析 AAAA
记录。服务的 TXT 超过该上限时会将 `txt_truncated` 置为非零，保留的 TXT
数据不能再视为完整内容。

```text
mdns_query -> qm_udp HAL -> mDNS 响应端
      |                         |
      +------ mdns_service_t ---+
```

## 用法

```c
#include "mdns.h"
#include "qm_errno.h"

static int on_service(const mdns_service_t *service, void *user)
{
    (void)user;
    /* 回调返回后数据失效，需要继续使用时在这里复制。 */
    return service->port != 0U ? QM_EOK : -QM_EINVAL;
}

int discover_printers(void)
{
    return mdns_query("_ipp._tcp.local", 1000U, on_service, NULL);
}
```

回调是同步调用，每个发现的实例调用一次；服务字符串为借用数据，必须在回调
返回前复制。回调返回任意非零值都会停止发现，并由 `mdns_query()` 原样
透传。`mdns_query()` 在至少发现一个实例且回调返回 `QM_EOK` 时返回
`QM_EOK`；没有接受任何有效应答时会透传 HAL 错误码（如首包等待超时的
`-QM_ETIMEOUT`）；已收到有效应答但未形成完整服务实例时返回
`-QM_ENOENT`。
任务调度、重试、缓存以及 socket/网卡配置仍由应用和 HAL 负责。
