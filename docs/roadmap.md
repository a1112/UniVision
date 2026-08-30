# 实施路线

## M0：Core Foundation（当前）

- [x] CMake/C++20 项目与安装导出
- [x] Status/Result、Device、Feature、Stream、Frame 数据模型
- [x] Adapter 注册、优先级选择与稳定身份去重
- [x] Simulator 端到端采集
- [x] C ABI 与 C/C++ 自动测试
- [x] Windows/Linux CI

## M1：Generic GenTL

- [ ] 显式 `.cti` 路径加载，不依赖全局 `GENICAM_GENTL64_PATH`
- [ ] System/Interface/Device/DataStream 枚举与生命周期
- [ ] GenApi NodeMap 到 `FeatureInfo`/`FeatureValue` 映射
- [ ] 外部 Buffer pool、announce/queue/revoke 与零额外拷贝
- [ ] Chunk Data、事件、incomplete buffer 和统计映射
- [ ] Producer 指纹、依赖诊断和重复设备识别

验收：使用至少两家 `.cti` Producer，在 Windows 11 x64 与 Ubuntu x64 完成连续采集、Feature、ROI、触发和断线错误闭环。

## M2：Vendor Pack A

- [ ] Basler pylon Adapter
- [ ] Hikrobot MVS Adapter
- [ ] FLIR Spinnaker Adapter
- [ ] 统一 Reconnect 状态机与故障注入
- [ ] Native 与 UniVision FPS/CPU/jitter/copy-count benchmark

验收：代表性 GigE/USB3 相机吞吐达到 Native baseline 的 95% 以上，支持路径不增加图像 copy，72 小时无 crash/deadlock。

## M3：Bindings 与生产硬化

- [ ] Python 与 C# binding
- [ ] Adapter/厂商 SDK 进程隔离选项
- [ ] Compatibility manifest 和 HIL runner
- [ ] PTP/Action Command
- [ ] ARM64/Jetson 包
- [ ] 安装、升级、健康观察与自动回退
