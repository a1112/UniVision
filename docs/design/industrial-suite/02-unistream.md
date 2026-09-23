# UniStream 传输模块设计

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 08  UniStream：范围与交付模型

### 产品目标

把 UniVision 得到的不可变帧交给同机或跨机消费者，并让每个接收结果可解释。关注序号、内容完整性、缓冲、重连和交付证据，不接管采集参数与真实触发系统。

| 模式 | 首版策略 | 保证边界 |
| --- | --- | --- |
| preview-latest | 每流最多保留 2 个完整候选帧，旧帧整帧淘汰 | 优先新鲜度；明确统计丢弃，不声称每帧送达 |
| inspection-bounded | 有界队列，逐帧校验与确认；满时显式返回压力 | 不静默丢弃已接受数据；资源耗尽时可以失败 |
| record-durable | 经磁盘队列/接收端提交后返回 durable 确认 | 保证范围受持久化介质、保留期与接收端承诺限制 |

### 数据通路

```text
Core Frame → admission budget → OwnedFrame
 → encoding queue → TLS transport → reassembly
 → hash/layout validation → consumer commit → receipt
```

publish 返回值必须区分 rejected、accepted-memory、accepted-spooled。业务需要发送端崩溃后恢复时，只能依赖 accepted-spooled，而不能把内存接收成功当作持久化保证。

消费者互相隔离。预览、算法、录制分别配置优先级和容量；慢预览可以断开，必需录制失败则会话降级/失败并通知采集拥有者。UniStream 本身不尝试写 PLC 或安全绕过现场控制。

### 不在首版

公网穿透、云账号、远程桌面、无界广播、任意远端执行、自动远程修改 Feature、多机一致性数据库，以及无条件 exactly-once 承诺均不纳入。

## 09  UniStream：传输与消息封装

### 传输决策

首个可替换后端采用分帧 TCP + TLS 1.3，控制与数据连接隔离，数据连接按流/优先级建立有限池。TCP 提供有序字节流而不是应用帧，也不代表接收端已保存或处理。[S3][S4]

QUIC 保留为后续后端，复用同一应用确认与数据契约；多流能力不自动解决磁盘提交、业务幂等或无限上游生产。[S5] 不同时维护两套首发协议。

| 偏移 | 长度 | UVS1 固定头字段 |
| --- | --- | --- |
| 0 | 4 | magic = ASCII UVS1 |
| 4 | 2 | protocol_major，无符号大端 |
| 6 | 2 | message_type，无符号大端 |
| 8 | 4 | metadata_length，UTF-8 JSON 长度 |
| 12 | 8 | payload_length，本消息二进制块长度 |
| 20 | 8 | correlation_id，连接内关联值，不是业务幂等键 |
| 28 | 4 | reserved，首版发送/接收均要求为 0 |

固定头后为 metadata 与 raw payload。实验上限：metadata 64 KiB、每块 1 MiB、单帧解码后 256 MiB；并发预留也受总内存预算限制。任何溢出、越界、重复冲突块或解压超预算在消费前拒绝。

### 消息与信任

HELLO/CAPABILITIES → OPEN_STREAM → FRAME_BEGIN → FRAME_CHUNK → FRAME_COMMIT → RECEIPT；辅助消息包含 CREDIT、GAP、RESUME、CANCEL、CLOSE 与 ERROR。

非 loopback 连接要求双向认证、预先信任的证书/身份与流级授权；不因发现设备就信任。数据连接必须绑定已认证的控制会话。禁用有副作用请求的 0-RTT，不自研加密算法；证书错误不得静默退回明文。[S4]

## 10  UniStream：确认、背压与恢复

| 确认级别 | 含义 | 不得外推为 |
| --- | --- | --- |
| received | 完整重组并通过长度、格式和载荷哈希校验 | 已保存到稳定存储 |
| durable | 接收端按声明的提交策略保存载荷和账本 | 算法完成或生产合格 |
| processed | 特定消费者提交了结果；带 consumer_id/run_id/result_ref | 其他消费者也完成 |

建议语义为“至少一次传输 + 指定作用域内幂等提交”。幂等键为 consumer_id + session_id + stream_id + sequence。相同键但摘要不同必须报冲突，不能覆盖；跨订阅者不是一个全局 exactly-once 事务。

### 流量控制

接收端以字节 credit 限制在途数据；发送端另限制帧数、编码任务与本地磁盘队列。慢消费者不能占用全部相机缓冲。必需消费者在预算耗尽后显式失败并报告缺口；不可无限重试或假定外部触发源能暂停。

建议初始 OwnedFrame 总预算 512 MiB、磁盘队列上限 32 GiB、预览每流 2 帧。这些是配置起点，不是性能承诺；硬上限由接收端验证，不能让对端任意扩大。

### 重连与幂等

RESUME 携带会话身份、消费者身份、协议版本和已提交区间。只能在仍被保留的磁盘队列/原始数据内补传；已淘汰帧返回 GAP。单一最大 sequence 不足以表达中间空洞，需连续前缀与有限缺失区间。

重连不复用旧认证会话；重发内容摘要必须相同。发送端只有收到所需级别的确认、且其他必需消费者也满足保留规则后，才回收对应队列项。

> 永不承诺“无限断网仍不丢帧”。承诺应写成：在指定数据率、缓存容量、断网时长及存储条件下，哪些已接受帧可以恢复。

## 11  UniStream：四路线扫容量预算

计算示例沿用此前讨论的场景：4 路、每行 2048 像素、最高 100,000 行/秒、每幅 2048 行、单条 10 Gb/s 链路。假设四路同时达到上限且无行间空闲；不代表现场实际持续速率。

```text
每路帧率 = 100000 / 2048 = 48.828125 帧/秒
总帧率   = 195.3125 帧/秒
组帧时间 = 2048 / 100000 = 20.48 ms
原始速率 = 路数 × 行宽 × 行频 × 每像素存储字节数
```

| 存储格式假设 | 四路有效载荷 | 仅载荷比特率 | 链路判断 |
| --- | --- | --- | --- |
| Mono8，1 B/px | 819.2 MB/s | 6.5536 Gb/s | 理论未超 10G；仍需计开销与实测 |
| Mono16，2 B/px | 1,638.4 MB/s | 13.1072 Gb/s | 原始载荷已超 10G |
| RGB8，3 B/px | 2,457.6 MB/s | 19.6608 Gb/s | 原始载荷已超 10G |
| JPEG 平均 1 MiB/帧（假设） | 204.8 MB/s | 1.6384 Gb/s | 必须以真实数据测平均/峰值 |

这里 Mono16 指按 16 位存储；10/12 位打包格式必须按实际步长计算。深度和强度同时发送需相加。MB 使用 10^6，MiB 使用 2^20；单幅 Mono8 为 4 MiB。

### 预算揭示的限制

Mono8 满速下，512 MiB 内存只覆盖约 0.655 秒；32 GiB 磁盘队列只覆盖约 41.94 秒。原始录制每小时约 2.949 TB，必须事先规划带宽、磁盘与保留期。

首行到完整帧天然包含 20.48 ms 组帧时间；不能把“完整帧端到端 <10 ms”当成这个场景的可实现目标。可以单独测“末行采集完→接收端可用”的传输处理延迟。

> JPEG 先限定为预览/经验证的算法输入。测量原始深度、有效掩码和必要元数据保留无损；不能为了过网而先破坏测量信息。

## 12  UniStream：接口、观测与首版验收

| 拟议接口 | 语义 |
| --- | --- |
| Publisher.open(config) | 协商格式、身份、授权、确认等级与预算 |
| Publisher.publish(frame) | 返回接纳级别和 ticket；不阻塞 SDK 回调等待网络 |
| Publisher.close(drain_policy) | 有界排空；超时后留下明确未交付清单 |
| Subscriber.accept(descriptor) | 先完成预算/布局检查，再允许载荷接收 |
| Subscriber.commit(ticket, level) | 提交指定级别证据；durable 与 processed 分开 |
| Metrics.snapshot() | 按流/消费者返回速率、积压、缺口、重试和拒绝原因 |

### 运行视图

工作台显示每流原始/编码速率、端到端状态、credit、内存/磁盘占用、最后提交序号、缺口与重连次数。received、durable、processed 各用明确文字；收到数据不能显示成“检测通过”。

### 首版通过条件

用合成/回放数据先完成：分片/粘包恢复、乱序重组防护、重复帧幂等、摘要冲突拒绝、慢消费者隔离、断线后保留区间补传、过期缺口报告、格式与单位保持、无效证书拒绝。

性能报告须绑定提交、编译器、系统、CPU/GPU、网卡/驱动、链路、TLS/压缩设置、输入哈希及数据率分布。吞吐记录 payload 与 wire 两个口径；一向延迟仅在时钟映射已验证时使用。

### 最小迁移路径

先增加 Core Frame 只读适配器和本机 loopback 测试；再验证两台电脑；最后才由采集应用显式选择真实相机流。接收端不获得任意 Feature 写权限。旧采集使用者不改代码即可继续 Core-only 构建。

有关精度/吞吐的所有目标均为待验收条件。任何压力测试失败都应产出失败报告，而不是自动降低数据质量后继续显示原计划通过。


## 关联文档

[文档索引](README.md) · [共享契约](01-contracts.md) · [验收矩阵](08-acceptance.md) · [参考来源](10-references.md)

[S1]: https://github.com/a1112/UniVision/blob/main/README.md
[S2]: https://www.emva.org/standards-technology/genicam/introduction-new/
[S3]: https://www.rfc-editor.org/rfc/rfc9293.html
[S4]: https://www.rfc-editor.org/rfc/rfc8446.html
[S5]: https://www.rfc-editor.org/info/rfc9000/
[S6]: https://mcap.dev/spec
[S7]: https://www.nist.gov/pml/nist-technical-note-1297
[S8]: https://sqlite.org/pragma.html
[S9]: https://json-schema.org/draft/2020-12
