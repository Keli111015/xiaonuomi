# TinyCBOR QUMI 组件

本组件提供 QUMI 组件使用的 TinyCBOR 编码器和解析器。它保持上游
`cbor.h` API，同时将实现纳入 QUMI 的组件边界和构建系统。

## 职责

- 提供不依赖平台和 RTOS 的 CBOR 编解码能力。
- 通过组件 include 目录公开 `cbor.h`。
- 通过 QUMI 组件构建系统编译实现源码。

## 非职责

- 不负责 socket、存储、任务、定时器或日志策略。
- 不负责协议字段校验，协议组件负责自己的 schema。
- 不改变 TinyCBOR API 的内存所有权约定。

## 架构

```text
QUMI 组件 / 应用
          |
       cbor.h
          |
 TinyCBOR 编码器 + 解析器
          |
     与平台无关的 C
```

## 配置

由于协议和云组件可以共享该实现，`QM_TINYCBOR_SUPPORT` 默认启用。

## 用法

QUMI 组件直接包含 `cbor.h`，按上游 TinyCBOR 编码器和解析器契约使用。组件不会在
接口调用返回后保留调用方缓冲区，内存所有权和生命周期遵循上游 API 约定。
