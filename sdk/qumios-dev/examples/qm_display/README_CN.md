# QUMI 显示协议示例

本目录提供显示控制器协议 V1.0 的应用侧示例：

协议正文位于
[`components/qm_display_client/PROTOCOL.md`](../../components/qm_display_client/PROTOCOL.md)。

- `qm_display_client_example.c`：通过 `qm_uart` 创建主控侧客户端并查询屏端固件版本。
- `qm_display_server_example.c`：通过 `qm_uart` 创建屏端服务端并提供单请求处理入口。

在目标示例或应用配置中启用 `QM_DISPLAY_CLIENT_EXAMPLE` 或
`QM_DISPLAY_SERVER_EXAMPLE`。对于使用显式源码列表的目标，还需加入所选示例源码和
对应组件的源码、头文件路径。

服务端示例刻意将素材存储、格式校验和屏幕显示回调留给产品代码实现；回调约定见
`components/qm_display_server/README_CN.md`。
