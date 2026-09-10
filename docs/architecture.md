# UniVision 基础架构

## 运行时边界

```mermaid
flowchart TB
    App["AOI / Robotics / Recording"] --> API["C++ API / C ABI"]
    API --> Core["System / Identity / Capability"]
    Core --> Stream["Buffer / Frame / Time / Statistics"]
    Core --> Adapter["Adapter Interface"]
    Adapter --> GenTL["Generic GenTL"]
    Adapter --> Native["Certified Native SDK"]
    Adapter --> Sim["Simulator"]
```

Core 不直接包含任何厂商头文件。每个真实 Adapter 负责将厂商错误、生命周期、Feature 和 Buffer 转换为 UniVision 公共模型。

## 对象与生命周期

| 对象 | 职责 | 关键约束 |
|---|---|---|
| `System` | Adapter 注册、设备枚举、去重和选择 | Adapter 调用不在注册锁内执行 |
| `Adapter` | 一个 Producer 或厂商 SDK 的边界 | ID 唯一；返回统一 `DeviceInfo` |
| `Camera` | 打开、关闭、恢复和 Feature | 状态机显式；创建流前必须打开 |
| `Stream` | 采集启动、等待帧和统计 | 背压策略显式配置 |
| `Frame` | 描述符、Buffer 与 Chunk Metadata | Buffer 所有权随 Frame，禁止隐式像素转换 |

## 设备身份与 Adapter 选择

Adapter 应尽可能给出跨枚举稳定的 `stable_id`。真实相机建议由 transport identifier、MAC/serial、vendor/model 组合生成。多个 Adapter 报告相同 `stable_id` 时，Core 只保留最高优先级入口。

建议优先级顺序：

1. 已认证 Native Adapter；
2. 已认证 GenTL Producer；
3. Generic GenTL 或 Aravis best-effort；
4. 不支持。

## ABI 策略

公共 C++ API 用于本地集成，`include/univision/c/univision.h` 是语言绑定的稳定底座。C 结构首字段是 `struct_size`，允许未来在尾部追加字段。跨 ABI 边界不传递 STL 对象或异常；帧通过 opaque handle 持有 Buffer 生命周期。

基础版本先稳定应用侧 C ABI。动态 Adapter 插件 ABI 会在 Generic GenTL 路径跑通并明确隔离要求后冻结，避免过早固化错误的 Buffer/事件语义。

## Generic GenTL 路径

`make_gentl_adapter()` 只加载调用方明确指定的一个 `.cti`。运行时解析 GenTL 1.5 基线符号；GenTL 1.6 Producer 保持向后兼容。当前实现覆盖：

1. `GCInitLib → TLOpen` 与 Producer 元数据；
2. Interface/Device update、枚举与稳定设备身份；
3. Control access 打开设备及第一个 DataStream；
4. Payload 查询、外部 Buffer announce、queue 和 New Buffer event；
5. Frame 释放时自动 requeue，停止时 flush/revoke，最后按 `DS → Device → Interface → TL` 逆序关闭。
6. 从 Remote Device Port 的 `Local:` URL 读取 XML，映射基础 GenApi NodeMap；
7. 启动时先启动 DataStream 再执行 `AcquisitionStart`，停止时反向执行。

Frame 直接引用已 announce 的 Buffer，没有图像 `memcpy`。Frame 的共享 owner 是一张 Buffer lease；上层持有 Frame 时该 Buffer 不会被重新排队。若上层长期持有超过 Buffer pool 容量的 Frame，Producer underrun 会进入 `frames_dropped` 统计。

NodeMap 当前支持 `Integer/IntReg`、`Float/FloatReg`、`Boolean`、`Enumeration/EnumEntry`、
`String/StringReg` 与 `Command`，包括大小端寄存器、范围、步进、单位、枚举项和访问权限。
标准节点通过 SFNC 名称或 XML `NameSpace="Standard"` 标记，厂商节点仍以原始名称暴露。

首个实现刻意只接受未压缩 `Local:` XML。ZIP/HTTP URL、MaskedIntReg、
SwissKnife/Converter、`pIsAvailable`/`pIsWritable` 等表达式尚未实现；遇到这些能力时返回
明确的 `unsupported` 或解析错误，不静默猜测节点语义。

## 线程与错误模型

`System` 的 Adapter 注册表受互斥锁保护，枚举前复制快照，避免执行外部 SDK 代码时持锁。每个 Adapter 必须声明并内部满足其厂商 SDK 的线程限制。

可预期失败通过 `Status`/`Result<T>` 返回；Adapter 不允许让厂商异常越过公共边界。C ABI 另提供线程局部 `uv_last_error_message()`，返回最近一次调用的诊断文本。
