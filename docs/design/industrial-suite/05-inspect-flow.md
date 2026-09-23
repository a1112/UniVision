# InspectFlow 离线流程设计

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## 21  InspectFlow：类型化离线流程

### 首版目标

把文件/合成输入、预处理、测量或算法、规则判断和结果归档组成可保存、可复现的离线有向无环图。编辑器是表现层，图验证与执行器可在 CLI/headless 下独立使用。

| 节点类别 | 首版类型/端口 | 边界 |
| --- | --- | --- |
| Source | SyntheticImage、RecordingSource、ProfileFile | 只读已授权输入，禁止 LiveCamera/PLC 节点 |
| Transform | Crop、Gray、Threshold、ProfileFilter | 明确是否改变坐标、单位、掩码与 lineage |
| Measure | CircleFitDiameter | 调用 UniMeasure；不存在就显示缺失能力 |
| Judge | VerificationErrorRule | 消费数值/不确定度/有效性，不能把 invalid 转 pass |
| Sink | Record、ResultArchive、Report | 同一 recording 实现；持久化完成才提交运行 |

### 最小流程示例

```text
ProfileFile → CircleFitDiameter → VerificationRule
                                      │
                                 ResultArchive
```

常规图像链可为 RecordingSource → Crop → Gray → Threshold → Record。检测模型节点属于后续适配项，不在首版通过下载未知模型临时补齐能力。

### 类型系统

端口区分 Image、DepthRaw、DepthMetric、MetricProfile、MeasurementResult、Decision 与 ArtifactRef。mm 与 px 不可隐式连接；强度图不是深度图；转换节点必须保留坐标变换/标定引用。

> 首版的安全边界由节点注册表和执行器共同保证，不是仅在 UI 隐藏“真实设备”按钮。

## 22  InspectFlow：执行与状态机

```text
queued → preparing → running → draining → finalizing
                                         └→ succeeded
任何活动阶段 → cancelling → cancelled
错误/输入不完整 → failed 或 partial（不得伪装成功）
```

preparing 冻结图、输入清单、节点/模型/标定版本与资源限制；running 不接受原地参数修改。编辑后生成新 revision，当前 run 仍使用原版本。

### 调度与背压

按拓扑排序调度；每条边有帧数与字节双上限。Source 只有获得下游预算才读取下一项。多路合并必须指定配对键、时间窗及未匹配处理，不能靠“最近帧”隐式拼接。

首版优先 stateless 节点与单运行可控并行。状态节点必须声明分区键、状态快照/恢复和顺序要求，否则随机 seek 与重复执行不被支持。

### 取消、失败与完成

取消后停止新输入，向节点发协作取消；有界等待后终止隔离工作进程并清理临时制品。只有声明为纯函数/幂等的节点允许自动重试；Sink 提交用 run/node/output 幂等键。

任一必需 Sink 未完成 durable 提交，整个 run 不得 succeeded。检测得到 fail/NG 可以是计算运行 succeeded，但 Decision 必须保留 fail；“任务成功”和“工件合格”分别显示。

### 缓存

缓存键包含输入摘要、图 revision、节点版本、参数、标定、模型、Provider/精度模式及影响结果的环境。非确定性或状态不透明节点默认禁用结果缓存。命中缓存仍记录来源 run，不能冒充新计算。

运行清单保存每节点起止时间、队列峰值、输出摘要、错误、重试和取消状态；最终报告不依赖 UI 当时是否在线。

## 23  InspectFlow：图验证与扩展治理

| 验证阶段 | 拒绝条件 |
| --- | --- |
| 语法 | schema major 未知、ID 重复、参数不匹配、整数溢出 |
| 结构 | 循环、悬空端口、必需输入未连、没有必需 Sink、不可达危险/无效节点 |
| 语义 | 单位/类型/坐标不匹配、原始深度缺转换、缺少标定或掩码 |
| 能力 | 节点未注册、版本/模型摘要缺失、所需 Provider 不可用 |
| 资源 | 内存/解码/并发预算不足、输出空间不足、单项尺寸超限 |
| 权限 | 外部路径越界、任意命令/脚本、网络下载、真实设备动作 |

### 注册表，而非任意脚本执行器

首版只允许随版本发布的显式注册节点；UI 不能提交 shell/Python 文本让服务执行。后续插件需要独立包清单、来源信任、版本锁定、输入/输出 Schema、能力和资源声明。

需要 Python 推理的后续节点放在独立受控 Worker，使用固定环境与受限制品引用；不在运行中 pip install，也不将任意用户文件当作插件加载。受限工作进程不是天然安全沙箱，必须说明操作系统隔离能力。

### 图与运行文件

FlowDefinition 只描述工作，不存机器绝对路径、密钥或二进制内容。工作区配置在本机解析授权 URI。冻结后的 UTF-8 定义文件按实际字节计算 SHA-256；任何编辑都形成新的 revision，避免“内容变了版本没变”。

导入未知节点时可以只读展示，不自动替换为近似节点执行。版本迁移生成新图并保留差异及旧图；禁止在后台静默升级结果语义。

> 图验证失败必须定位到节点、端口或参数，并提供明确原因。不能只显示“运行错误”，也不能把缺节点当作旁路后报告全流程成功。

## 24  InspectFlow：编辑器交互与验收

| 区域 | 具体行为 |
| --- | --- |
| 左侧节点库 | 按类型分类；不可用节点说明依赖，不允许拖入后偷偷下载 |
| 中间画布 | 连接端口时做类型提示；缩放/框选/撤销/重做只影响草稿 |
| 右侧属性 | Schema 驱动输入、单位显示、合法范围和默认值来源 |
| 下方运行区 | 预检、启动、取消、逐节点状态、队列与证据入口 |
| 版本栏 | 草稿/冻结 revision/当前 run 分开；明确未保存变更 |

### 一次完整操作

新建离线图 → 选择已导入的工作区输入 → 添加节点 → 预检通过 → 冻结版本 → 启动 → 查看逐节点结果 → 从输出跳到 VisionReplay/UniMeasure 证据。

运行中点击改参数时进入新草稿，而不是改变正在处理的后半批数据。关闭窗口后再次打开应能看到实际运行状态。取消按钮说明会保留哪些已提交结果和哪些临时项会清理。

### 首版验收

循环/单位错误图运行前被拒绝；相同冻结图在 CLI 与 UI 路径输出同契约结果；消费者变慢时内存不无限增长；取消不产生伪成功；未知/缺失节点不会静默跳过；运行结果可追溯到准确 revision。

合法图中产生 invalid measurement 时，后续规则保留 indeterminate/not-evaluated，而非默认 pass。必需归档失败时 run 不显示全成功。

### 非目标

不做通用办公自动化，不做 Node-RED 式任意现场集成，不直接替代生产项目自己的触发、安全联锁或设备管理。未来实时节点需要新的权限模型、调度语义和独立现场评审。


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
