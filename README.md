# UniVision

UniVision 是面向工业相机的跨厂商图像采集运行时。项目目标不是简单统一函数名，而是建立可认证、可诊断、可扩展的设备发现、特性访问、流采集、帧生命周期和异常恢复基础设施。

当前版本为 **0.3 GenApi Feature Milestone**，已包含：

- C++20 Core 与可安装的 CMake package；
- `System → Adapter → Camera → Stream → Frame` 分层接口；
- SFNC/PFNC 友好的通用 Feature 模型，同时保留原始节点访问入口；
- 稳定设备身份、Adapter 优先级和重复设备去重；
- 显式 Buffer ownership、Memory Type、时间戳、元数据和流统计；
- 带边界检查的 Simulator Adapter，可在无硬件环境完成端到端采集；
- 显式 `.cti` 路径加载、Producer/Interface/Device 枚举与错误归一化；
- GenTL DataStream announce/queue/event/revoke 生命周期和零额外拷贝 Frame；
- Remote Device Port 的本地 XML 加载，以及 `Int/Float/Enum/Bool/String/Command`
  NodeMap 映射；
- SFNC 标准节点标记、原始节点名访问、范围/步进/单位/枚举项/访问权限；
- DataStream 与远端 `AcquisitionStart/Stop` 命令的正确启停顺序；
- Fake CTI Producer，持续验证 Windows/Linux 动态加载与采集 ABI；
- 版本化 C ABI，覆盖 Feature 枚举与六类值访问、流与帧生命周期；
- 无第三方测试依赖的 C++/C 测试，以及 Windows/Linux CI。

> 当前 NodeMap 是可测试的 GenApi 基础子集，支持未压缩 `Local:` XML 和直接
> Register/Value 节点。ZIP XML、SwissKnife/Converter、动态可用性表达式和真实厂商
> 兼容认证仍属于后续工作，因此当前 Generic GenTL 路径仍标记为 best-effort。

## 快速开始

需要 CMake 3.24+ 和支持 C++20 的编译器。

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/univision_list_devices
```

也可以不使用 preset：

```bash
cmake -S . -B build -DUNIVISION_BUILD_TESTS=ON -DUNIVISION_BUILD_EXAMPLES=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 最小 C++ 示例

```cpp
#include <univision/univision.h>

univision::System system;
system.register_adapter(univision::make_simulator_adapter());

auto devices = system.enumerate_devices();
univision::DeviceSelector selector;
selector.stable_id = devices.value().front().stable_id;

auto camera = system.create_camera(selector).value();
camera->open();
auto stream = camera->create_stream().value();
stream->start();
auto frame = stream->wait_next(std::chrono::seconds(1)).value();
```

## 加载 GenTL Producer

UniVision 不读取全局 `GENICAM_GENTL*_PATH`，必须显式指定 `.cti`，从而避免多个厂商 Producer 与依赖 DLL/SO 冲突：

```cpp
univision::GenTLAdapterOptions options;
options.cti_path = R"(C:\Program Files\Vendor\Runtime\producer.cti)";

auto adapter = univision::make_gentl_adapter(options);
if (!adapter) {
  throw std::runtime_error(adapter.status().message());
}
system.register_adapter(std::move(adapter).value());
```

纯 C 调用可使用 `uv_system_register_gentl(system, cti_path)`，并通过
`uv_camera_get_feature_info`、类型化 get/set 与 `uv_camera_execute_command` 访问节点。

完整示例见 `examples/list_devices.cpp`，纯 C 调用路径见 `tests/c_api_tests.c`。

## 架构原则

- **GenTL-first, Native-when-needed**：标准 Producer 是横向兼容底座，认证 Native Adapter 提供性能和厂商特性增强。
- **Capability negotiation**：应用查询功能是否存在、类型、范围和访问权限，而不是假设所有相机一致。
- **零拷贝优先**：`FrameBuffer` 明确持有者和 Memory Type，不默认转换像素格式。
- **可观测失败**：超时、掉线、缓冲耗尽和 Adapter 故障使用统一状态码表达。
- **认证优先选择**：同一设备被多个入口发现时，优先选择更高优先级的认证 Adapter。

详细边界与近期实施顺序见 [docs/architecture.md](docs/architecture.md) 和 [docs/roadmap.md](docs/roadmap.md)。

## 项目状态

该仓库仍处在基础设施阶段。下一里程碑是扩展 GenApi 兼容面（ZIP XML、引用范围、
Converter/SwissKnife 与动态可用性），补齐 ROI/Trigger 的规范化便捷 API，并使用真实
厂商 `.cti` 在 Windows 11 x64 与 Ubuntu x64 做硬件在环验证。

## 可选工业视觉扩展

`cmake --preset industrial && cmake --build --preset industrial && ctest --preset industrial`
会构建共享契约、UniStream 本机有界交付、MCAP 录制、VisionReplay、
UniMeasure、InspectFlow 和无界面的合成演示 CLI。Windows 下可运行
`build\industrial\univision_uv_cli.exe demo build\demo-01`，输出录制会话和
合成测量运行制品。各扩展选项默认关闭；启用录制时才获取锁定版本的 MCAP
与 nlohmann/json 源码。
已有工作区可用 `univision_uv_cli run-synthetic-flow <workspace> <flow-definition.json> <run-id>`
读取保存的离线图并运行合成截面；该命令显式将输入标为 synthetic，不输出生产判定。

设计包与逐模块已实现/待验证边界见
[工业视觉扩展状态](docs/design/industrial-suite/status.md)。

## Qt Quick 相机调试界面

可选的 C++ / QML 桌面工作台已经接入 Simulator 采集链路，支持实时预览、参数读写、图像保存、直方图和显示 ROI。启用 `UNIVISION_BUILD_GUI=ON` 构建；完整启动和验证步骤见 [相机调试界面](docs/camera-debugger.md)。需要 Qt 6.5+，核心库本身仍无 Qt 依赖。
