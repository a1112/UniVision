# UniVision

UniVision 是面向工业相机的跨厂商图像采集运行时。项目目标不是简单统一函数名，而是建立可认证、可诊断、可扩展的设备发现、特性访问、流采集、帧生命周期和异常恢复基础设施。

当前版本为 **0.1 基础框架**，已包含：

- C++20 Core 与可安装的 CMake package；
- `System → Adapter → Camera → Stream → Frame` 分层接口；
- SFNC/PFNC 友好的通用 Feature 模型，同时保留原始节点访问入口；
- 稳定设备身份、Adapter 优先级和重复设备去重；
- 显式 Buffer ownership、Memory Type、时间戳、元数据和流统计；
- 带边界检查的 Simulator Adapter，可在无硬件环境完成端到端采集；
- 版本化 C ABI，覆盖枚举、开关相机、浮点 Feature、流与帧生命周期；
- 无第三方测试依赖的 C++/C 测试，以及 Windows/Linux CI。

> 当前尚未接入真实 GenTL Producer、pylon、MVS 或 Spinnaker。Simulator 用于验证核心语义，不代表硬件适配已经完成。

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

完整示例见 `examples/list_devices.cpp`，纯 C 调用路径见 `tests/c_api_tests.c`。

## 架构原则

- **GenTL-first, Native-when-needed**：标准 Producer 是横向兼容底座，认证 Native Adapter 提供性能和厂商特性增强。
- **Capability negotiation**：应用查询功能是否存在、类型、范围和访问权限，而不是假设所有相机一致。
- **零拷贝优先**：`FrameBuffer` 明确持有者和 Memory Type，不默认转换像素格式。
- **可观测失败**：超时、掉线、缓冲耗尽和 Adapter 故障使用统一状态码表达。
- **认证优先选择**：同一设备被多个入口发现时，优先选择更高优先级的认证 Adapter。

详细边界与近期实施顺序见 [docs/architecture.md](docs/architecture.md) 和 [docs/roadmap.md](docs/roadmap.md)。

## 项目状态

该仓库仍处在基础设施阶段。下一里程碑是 Generic GenTL Adapter，并在 Windows 11 x64 与 Ubuntu x64 上接入真实 `.cti` Producer 完成枚举、Feature 与连续采集闭环。
