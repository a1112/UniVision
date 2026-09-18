# Qt Quick 相机调试工作台

桌面界面直接调用 `UniVision::Core`。当前仅注册 Simulator，支持连接/断开、连续采集、单帧抓取、曝光/增益/帧率读写、PNG 原始图像保存、灰度直方图、像素取值、缩放/平移、十字线与显示 ROI。

## 构建与启动

安装 Qt 6.5+，包含 Quick、QuickControls2、Qt Quick Dialogs，并使用与 Qt 套件一致的 C++20 编译器。

```powershell
cmake -S . -B build/gui -G Ninja -DUNIVISION_BUILD_GUI=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.11.0/mingw_64"
cmake --build build/gui --parallel
ctest --test-dir build/gui --output-on-failure
./build/gui/apps/camera_debugger/univision_camera_debugger.exe
```

Windows 下，将对应 Qt 的 `bin` 及编译器 `bin` 加入 PATH。亦可在 Qt Creator 中打开根目录 CMakeLists.txt，选择 Qt 6 桌面 Kit，在 CMake 配置中启用 `UNIVISION_BUILD_GUI=ON`。

部署可使用 `cmake --install build/gui --prefix "${PWD}/out/camera-debugger"`（使用绝对路径），安装规则调用 Qt 的 QML 部署脚本收集运行库。GUI 默认关闭，不影响无 Qt 环境中的核心库构建。

## 操作

1. 点击“连接设备”。
2. 点击“开始采集”查看动态 Mono8 测试图像，或点击“单帧抓取”。
3. 停止采集后修改参数，点击“应用参数”写入并回读。“默认值”仅重置输入，需点击应用。
4. 点击“保存图像”选择 PNG 路径。保存的是原始灰度帧，不含十字线或选区。
5. 放大后拖动画面平移；开启 ROI 后拖动绘制显示选区；“适应”重置缩放和平移。

## 实现边界

- 相机和 Stream 只在一个工作线程中访问。50ms 有界等待让停止和关闭请求可以被处理。
- 工作线程复制 SDK 帧并计算直方图，将最新结果写入单帧邮箱；UI 每 33ms 拉取，防止高帧率导致事件队列积压。UI 显示帧数与流丢帧统计是不同指标。
- 图像提供器持有已脱离 SDK 生命周期的 QImage，由 QML Image 显示。当前实现为 CPU 拷贝路径，不承诺零拷贝。
- 当前显示器仅支持 Host Mono8；图像尺寸固定为模拟器默认 640×480。曝光、增益在模拟器中只保存数值，不改变图像。
- ROI 仅为显示叠加，未写入相机；单帧为启动流、读取一帧并停止，不代表硬件触发。没有伪造硬件触发、自动曝光、USB 状态或延迟读数。
- 接入真实设备时需注册真实 Adapter，并将固定的设备信息、尺寸和参数范围改为按设备能力动态生成。

## 运行冒烟验证

```powershell
New-Item -ItemType Directory -Force build/gui/smoke
$env:UNIVISION_SMOKE_DIR = (Resolve-Path build/gui/smoke).Path
./build/gui/apps/camera_debugger/univision_camera_debugger.exe --smoke-test
```

此模式自动验证连接、参数写入及回读、连续采集、停止、单帧、直方图总像素数、像素读取、PNG 保存、QML 窗口截图和断开。成功退出码为 0；产出 `capture.png` 和 `interface.png`。需要可用的桌面图形环境。

