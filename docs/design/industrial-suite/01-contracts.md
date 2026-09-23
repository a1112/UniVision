# 共享数据与服务契约

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 05  共享契约：身份、时间与因果关系

| 字段/对象 | 必须表达的含义 |
| --- | --- |
| session_id | 一次采集或导入会话的唯一标识；重启/重新开始不能悄悄复用 |
| stream_id / device_id | 会话内流与稳定设备身份；模拟源必须标明 synthetic |
| sequence | 每流单调递增的 uint64；JSON 用十进制字符串，不能转成 JS Number |
| event_id / parent_ids | 事件身份与派生关系；衍生帧不冒用原始帧 ID |
| capture_time | 原始设备 tick/时钟域；有映射时另附 ns 与 mapping_id |
| ingest_time / record_time | 接收与记录时刻，明确 clock_id、单位和映射质量 |
| trigger / line_range | 触发批次、首末行序号与可选编码器范围；缺失需显式标记 |
| calibration_ref / transform_ref | 不可变标定版本与坐标变换，禁止只存“当前标定” |

### 时钟不是一个全局数字

区分设备时钟、主机单调时钟和 UTC。没有经验证的映射时，不计算跨机单向延迟，也不把两个设备时间戳相等视为同步采集。跨机延迟可先报告 RTT 与本机各阶段耗时。

映射记录包含原时钟、目标时钟、偏移/漂移模型、有效区间、误差估计和版本。时钟跳变时生成新 mapping epoch，保留旧值；严禁重写历史时间来制造连续性。

### 线扫帧的额外要求

一幅 2048 行图像对应一段采集时间，而不是单一瞬时曝光。记录 first_line_tick、last_line_tick、line_index_start、line_count；变速场景可附每行编码器位置或采样表引用。缺行、重复脉冲、计数回卷与重启必须可区分。

> 数据完整性 ≠ 时间同步 ≠ 测量有效性。三者分别给出状态，不能用一个绿色“在线”标识代替。

## 06  共享契约：帧、格式与内存

### FrameEnvelope v0.1 草案

一帧由身份、时间、类型、平面描述、编码信息、坐标/单位和载荷引用组成。元数据不嵌入大块 base64。所有长度在分配前做溢出与预算检查；plane 偏移、步长与总长度必须相容。

| 层次 | 关键内容 |
| --- | --- |
| 格式 | width、height、planes、pixel_format、endianness、有效位数与 packing；优先保留 PFNC 名称/值，不假定所有厂商格式已被认证。[S2] |
| 平面 | plane role、offset、row_stride、plane_size、dtype；强度、深度、置信度、有效掩码分开 |
| 单位 | raw-device-code、px、mm、m、unitless 明确区分；深度零值是否无效由掩码和格式说明决定 |
| 编码 | none / zstd / jpeg；JPEG 仅为经过批准的强度派生流，不用于保持原始深度/测量码值 |
| 完整性 | encoded_sha256 校验传输载荷；需要逐字节还原时额外校验 decoded_sha256 |
| 有效性 | complete / invalid / synthetic；缺口用独立 gap 事件表示，不以全黑图伪装成有效输入 |

### 缓冲区所有权

Core 的 Frame 是采集租约，不能被慢消费者永久占用。适配器要么在有界池中创建 OwnedFrame，要么使用计时、可审计的零额外拷贝租约；超时后不是偷偷回收仍在读取的内存，而是拒绝新的订阅工作或完成安全复制。

GPU Buffer/句柄只在声明的设备与进程范围内有效，不写入可移植录制文件，也不跨机传输裸指针。编码输出、传输重组和预览解码分别计入预算。

> 首版先保证归属、限流和错误传播正确；“零拷贝”只描述已测量的具体路径，不作为端到端宣传承诺。

## 07  共享契约：制品、提交与版本

| 对象 | 职责与规则 |
| --- | --- |
| ArtifactRef | 相对路径、大小、SHA-256、媒体类型和 schema version；限定工作区根目录，禁止绝对路径、路径穿越及外部符号链接逃逸 |
| RecordingManifest | 源、分段清单、时钟映射、缺口与关闭原因；可区分完整录制和恢复出的不完整前缀 |
| CalibrationProfile | 设备/几何/ROI/单位/变换/有效条件/证据；新版本不可覆盖旧版 |
| MeasurementResult | 被测量、结果、单位、输入/标定版本、不确定度与判定规则 |
| FlowDefinition | 节点版本、端口类型、边、参数、资源限制和输出策略 |
| RunManifest | 输入/图/模型/算法/环境摘要、运行状态、输出清单与异常 |

### 文件与数据库不是同一个事务

建议提交顺序：临时制品写入 → 校验 → 文件 flush/fsync → 同卷原子发布 → 持久化清单/索引提交 → 返回 durable 确认。目录项落盘和存储设备掉电行为需要平台测试；SQLite 的事务不能自动覆盖外部文件。[S8]

崩溃后扫描临时项、已发布未登记项与已登记缺失项：前两者可隔离/重新登记，第三类必须标记损坏并禁止用于正式结论。恢复是新的审计事件，不能静默补成“完整”。

### 版本兼容与错误

草案 schema_version 为 0.1.0-draft；网络 major 暂设 1 供实验协商，不代表已发布协议。未知 major 拒绝，minor 能力需协商。JSON Schema 只做结构验证；语义验证另检查单位、长度、引用、图无环与权限。[S9]

统一错误分类：SCHEMA_MISMATCH、INVALID_LAYOUT、BACKPRESSURE、INTEGRITY_FAILED、CLOCK_UNMAPPED、CALIBRATION_INVALID、CANCELLED、PARTIAL、UNSUPPORTED_CAPABILITY。错误携带上下文与恢复建议，不记录载荷正文或凭据。


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
