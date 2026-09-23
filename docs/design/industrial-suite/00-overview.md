# 总体架构与仓库落地

版本：0.1.0-draft  |  日期：2026-09-23  |  状态：拟议设计

目标：在 `a1112/UniVision` 单仓库内作为可选扩展落地。本文不代表已实现或已验收。

## UniVision 工业视觉扩展设计

UniStream · VisionReplay · UniMeasure · InspectFlow

> 一个仓库，四个可选模块，一套数据契约。
> 采集核心保持轻量；先建立离线工程闭环，再验证跨机传输。

产品与技术设计总册  |  v0.1  |  待评审设计基线

| 交付定位 | 说明 |
| --- | --- |
| 目标仓库 | a1112/UniVision；不新建四个独立产品仓库。 |
| 本次范围 | 架构、模块职责、数据契约、界面交互、实现分期与验收设计。 |
| 交付形式 | 可编辑 Word、PDF、四个模块 PDF 分册、可入库 Markdown、契约草案和合成样例。 |
| 不代表 | 功能已实现、相机已认证、吞吐已达标、毫米精度已验收，或已向 GitHub 提交代码。 |

### 核心决策

UniVision Core 继续负责相机与帧生命周期。UniStream 交付帧；VisionReplay 重现过程；UniMeasure 验证测量；InspectFlow 编排离线流程。共享录制组件仅负责数据持久化，不把整个工作台装进采集核心。

本文中的目录、导出目标、接口名称、协议和性能门槛均为拟议设计。实施前必须对真实 HEAD 做基线复核。

## 01  阅读指南与设计口径

### 本册怎么使用

| 部分 | 设计内容 | 对应仓库文档 |
| --- | --- | --- |
| 总体与共享契约 | 职责、仓库结构、构建与数据边界 | 00-overview / 01-contracts |
| UniStream | 传输、确认、背压、容量与安全 | 02-unistream |
| VisionReplay | 录制、时钟、回放与差异比较 | 03-vision-replay |
| UniMeasure | 截面直径验证、标定与不确定度 | 04-unimeasure |
| InspectFlow | 离线有向无环图与执行器 | 05-inspect-flow |
| 实施与验证 | 工作台、分期、验收、风险、来源 | 06-workbench ～ 10-references |

### 证据层级

【已知基线】仅指本会话此前读取的 UniVision README：0.3 GenApi Feature Milestone、C++20、CMake package、版本化 C ABI、Simulator 与 Generic GenTL 路径。[S1]

【设计决策】是本次建议，不是现有实现。表中的数值预算是初始工程配置或验收建议；未经实测不得写成产品指标。

【待复核】本轮 GitHub 连接未启用，公开页面与原始文件抓取未成功，因此未取得最新 HEAD、目录树、实际导出符号或许可证全文。源代码核验是 M0 的必需输入。

> README 的 Git blob SHA：898303b541ad7466370957a1582a4b0492d40144。
> 这是文件内容标识，不是仓库提交 SHA，不可用它替代发布基线。

资料包只包含设计、自建合成样例和验证脚本；不包含厂商 SDK、生产凭据、客户数据或其他私有仓库源码。

## 02  总体边界：扩展核心，而非重写核心

| 组件 | 拥有的职责 | 不拥有的职责 |
| --- | --- | --- |
| UniVision Core | 设备发现、Feature 访问、SDK 接入、Stream/Frame 生命周期 | 网络重试、历史工作台、测量判定、产线流程 |
| UniStream | 帧发布/接收、确认、缓冲、完整性、重连与交付统计 | 更改相机曝光、控制 PLC、承诺上游永不丢帧 |
| 共享 Recording | 录制段、清单、内容哈希、崩溃恢复与索引 | 时间轴 UI、算法比较、改写历史输入 |
| VisionReplay | 会话时间轴、检索/定位、重放、结果比较与证据导出 | 采集 SDK、现场设备操作、测量结论审批 |
| UniMeasure | 测量方法、标定关联、误差/不确定度与验证报告 | 自动修改设备标定、把原始码冒充毫米 |
| InspectFlow | 离线图验证、节点编排、资源预算、运行清单 | 首版真实触发/PLC控制、任意脚本执行 |
| Workbench | 统一工作区、查看与配置、运行反馈 | 持有 SDK Buffer、充当唯一业务运行位置 |

### 数据方向

```text
Core / Simulator / File
        │ immutable frame + metadata
        ├── UniStream ── remote consumer
        └── Recording ── VisionReplay
                             │
                       InspectFlow
                             │
                        UniMeasure ── result artifacts
```

网络可以完全关闭；回放、测量与流程编辑仍可离线使用。任何上层模块都不得反向成为 Core 的链接依赖。多个订阅者使用独立预算，不因预览卡顿而无限持有采集缓冲区。

## 03  单仓库结构与依赖规则

以下是新增路径提案，不是对当前目录树的陈述。原有 Core、C ABI、示例和测试位置保持不动；只有完成 M0 映射后才决定是否调整名称。

```text
docs/design/industrial-suite/  # 本资料包可放入的位置
modules/contracts/             # 帧、结果、标定与版本契约
modules/stream/                # UniStream
modules/recording/             # 唯一录制实现
modules/replay/                # VisionReplay 引擎
modules/measure/               # UniMeasure 引擎
modules/flow/                  # InspectFlow 执行器
apps/uv-runtime/               # 可选 C++ 服务宿主
apps/uv-cli/                   # 可选命令行入口
apps/uv-workbench/             # 可选桌面薄壳
tests/industrial-suite/        # 合成、契约与集成验证
```

| 拟议模块 | 允许的直接依赖 |
| --- | --- |
| contracts | 基础类型与版本校验，不依赖 SDK、GUI 或网络 |
| stream | contracts、Core 适配器、可选 TLS/压缩依赖 |
| recording / replay | contracts、MCAP；replay 复用 recording reader |
| measure | contracts、经过锁定的数学实现；无需联网 |
| flow | contracts、注册节点接口；按需接入 replay/measure/recording |
| workbench | 类型化本机服务接口；不直接链接厂商采集 SDK |

> 禁止：四套帧结构、两套录制格式、UI 直接操作相机指针、把“全部扩展”变成默认必选依赖。

modules/contracts 提供新契约到现有 Frame 的适配层，不强行更改现有公开结构布局。新模块采用独立版本与能力发现；产品总版本不能替代各协议版本。

## 04  构建、部署与进程职责

### 技术选型建议

核心及计算模块优先 C++20，延续已知 Core 路线。扩展 C ABI 使用独立命名空间/前缀，禁止 C++ 异常越过 ABI；缓冲区归属与释放者写入接口契约。Python/Rust 绑定按需添加，不成为采集包的编译前提。

桌面工作台候选为 Tauri + React/TypeScript；Tauri 侧仅作受限本机桥接。uv-runtime 是独立 C++ 进程，CLI 和 UI 消费同一接口。先完成 headless 基线，再验证 GUI 技术选型与像素显示性能。

| 拟议 CMake 选项 | 默认/依赖规则 |
| --- | --- |
| UNIVISION_ENABLE_STREAM | OFF；不启用则不查找网络和 TLS 扩展依赖 |
| UNIVISION_ENABLE_RECORDING | OFF；启用才引入 MCAP |
| UNIVISION_ENABLE_REPLAY | OFF；需要 recording reader |
| UNIVISION_ENABLE_MEASURE | OFF；仅增加测量依赖 |
| UNIVISION_ENABLE_FLOW | OFF；缺少节点能力时拒绝执行相应图 |
| UNIVISION_BUILD_WORKBENCH | OFF；单独打包，不进入默认 Core package |

所有新增依赖锁版本、来源和摘要，按 target 链接。Core-only 构建不得因扩展引入网络下载、GUI、数据库或新的系统服务；现有安装/消费测试继续通过。

### 本机服务边界

UI 使用当前用户的命名管道或 Unix socket，由平台权限校验身份；不依靠 appName 自报身份。图像走受控只读制品引用或限尺寸预览通道，不通过 JSON base64 搬运原始大帧。服务重启后 UI 重新发现能力，运行状态从清单恢复，不伪造成功。

首版图执行无真实设备写入能力；关闭 UI 不能无提示地终止录制/计算。退出时显示活动任务，并提供显式取消或保留 headless 任务的选项。


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
