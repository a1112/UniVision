#include "camera_controller.h"
#include <univision/univision.h>
#include <QDateTime>
#include <QMutexLocker>
#include <array>
#include <cmath>

using namespace univision;
struct CaptureState {
    System system;
    std::shared_ptr<Camera> camera;
    std::unique_ptr<Stream> stream;
    QTimer* timer = nullptr;
    bool single = false;
    QMutex mutex;
    QImage latest;
    QVariantList histogram;
    bool fresh = false;
    double fps = 0;
    std::chrono::steady_clock::time_point previous{};
    qulonglong dropped = 0;
};
QImage FrameProvider::requestImage(const QString&, QSize* size, const QSize&) {
    QMutexLocker lock(&mutex_);
    if (size) *size = image_.size();
    return image_;
}
void FrameProvider::setImage(const QImage& image) { QMutexLocker lock(&mutex_); image_ = image; }
CameraController::CameraController(FrameProvider* provider, QObject* parent)
    : QObject(parent), provider_(provider), worker_(new QObject), capture_(std::make_unique<CaptureState>()) {
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_.start();
    dispatch([this] {
        auto& c = *capture_;
        const auto status = c.system.register_adapter(make_simulator_adapter());
        if (!status) { log(QString::fromStdString(status.message()), true); return; }
        c.timer = new QTimer(worker_);
        c.timer->setInterval(1);
        connect(c.timer, &QTimer::timeout, worker_, [this] {
            auto& c = *capture_;
            auto result = c.stream->wait_next(std::chrono::milliseconds(50));
            if (!result) {
                if (result.status().code() != ErrorCode::timeout) {
                    log(QString::fromStdString(result.status().message()), true);
                    c.timer->stop(); c.stream->stop(); syncState();
                }
                return;
            }
            const auto& frame = result.value();
            const auto& d = frame.descriptor;
            if (!d.complete || d.pixel_format != 0x01080001ULL || d.memory_type != MemoryType::host ||
                d.width == 0 || d.height == 0 || d.stride < d.width ||
                frame.buffer.size() < d.stride * d.height) {
                c.timer->stop(); c.stream->stop();
                log(QStringLiteral("当前预览仅支持完整的 Host Mono8 帧"), true); syncState(); return;
            }
            QImage image(reinterpret_cast<const uchar*>(frame.buffer.data()),
                         int(d.width), int(d.height), qsizetype(d.stride), QImage::Format_Grayscale8);
            image = image.copy(); // The SDK frame owner may be released after this callback.
            std::array<int, 256> bins{};
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) ++bins[image.constScanLine(y)[x]];
            QVariantList histogram;
            for (auto count : bins) histogram.append(count);
            const auto now = std::chrono::steady_clock::now();
            const double fps = c.previous.time_since_epoch().count() == 0 ? 0 :
                1.0 / std::chrono::duration<double>(now - c.previous).count();
            c.previous = now;
            {
                QMutexLocker lock(&c.mutex);
                c.latest = image; c.histogram = histogram; c.fresh = true;
                c.fps = fps; c.dropped = c.stream->statistics().frames_dropped;
            }
            if (c.single) { c.timer->stop(); c.stream->stop(); log(QStringLiteral("单帧抓取完成")); syncState(); }
        });
        log(QStringLiteral("模拟适配器已就绪 · 尚未接入真实硬件"));
    });
    refresh_.setInterval(33);
    connect(&refresh_, &QTimer::timeout, this, [this] {
        auto& c = *capture_;
        { QMutexLocker lock(&c.mutex);
          if (!c.fresh) return;
          image_ = c.latest; histogram_ = c.histogram; fps_ = c.fps; dropped_ = c.dropped; c.fresh = false; }
        provider_->setImage(image_); ++revision_; emit frameChanged();
    });
    refresh_.start();
}
CameraController::~CameraController() {
    refresh_.stop();
    QMetaObject::invokeMethod(worker_, [this] {
        if (capture_->timer) capture_->timer->stop();
        if (capture_->stream) { capture_->stream->stop(); capture_->stream.reset(); }
        if (capture_->camera) capture_->camera->close();
    }, Qt::BlockingQueuedConnection);
    thread_.quit(); thread_.wait();
}
void CameraController::dispatch(std::function<void()> action) {
    QMetaObject::invokeMethod(worker_, std::move(action), Qt::QueuedConnection);
}
void CameraController::log(const QString& message, bool error) {
    QMetaObject::invokeMethod(this, [this, message, error] {
        error_ = error ? message : QString{};
        logs_.prepend(QDateTime::currentDateTime().toString("HH:mm:ss") + "   " + message);
        while (logs_.size() > 100) logs_.removeLast();
        emit logsChanged();
    }, Qt::QueuedConnection);
}
void CameraController::syncState() {
    auto& c = *capture_;
    const bool connected = c.camera && c.camera->state() != CameraState::closed;
    const bool running = c.stream && c.stream->running();
    QVariantMap device, parameters;
    if (connected) {
        const auto& d = c.camera->device_info();
        device = {{"model", QString::fromStdString(d.model)}, {"serial", QString::fromStdString(d.serial)},
                  {"vendor", QString::fromStdString(d.vendor)}};
        for (const auto* key : {"ExposureTime", "Gain", "AcquisitionFrameRate"}) {
            const auto value = c.camera->read_feature(key);
            if (value && std::holds_alternative<double>(value.value())) parameters[key] = std::get<double>(value.value());
        }
    }
    QMetaObject::invokeMethod(this, [this, connected, running, device, parameters] {
        connected_ = connected; running_ = running; device_ = device; parameters_ = parameters; emit stateChanged();
    }, Qt::QueuedConnection);
}
void CameraController::connectCamera() { dispatch([this] {
    auto& c = *capture_;
    if (c.camera && c.camera->state() != CameraState::closed) return;
    auto devices = c.system.enumerate_devices();
    if (!devices || devices.value().empty()) { log(QStringLiteral("未发现设备"), true); return; }
    DeviceSelector selector; selector.stable_id = devices.value().front().stable_id;
    auto camera = c.system.create_camera(selector);
    if (!camera) { log(QString::fromStdString(camera.status().message()), true); return; }
    c.camera = camera.value(); const auto status = c.camera->open();
    if (!status) { log(QString::fromStdString(status.message()), true); return; }
    log(QStringLiteral("模拟相机已连接")); syncState();
}); }
void CameraController::disconnectCamera() { dispatch([this] {
    auto& c = *capture_;
    if (c.timer) c.timer->stop();
    if (c.stream) { c.stream->stop(); c.stream.reset(); }
    if (c.camera) c.camera->close();
    log(QStringLiteral("设备已断开")); syncState();
}); }
void CameraController::startCapture(bool single) {
    auto& c = *capture_;
    if (!c.camera || c.camera->state() == CameraState::closed) { log(QStringLiteral("请先连接相机"), true); return; }
    if (c.stream && c.stream->running()) return;
    auto stream = c.camera->create_stream();
    if (!stream) { log(QString::fromStdString(stream.status().message()), true); return; }
    c.stream = std::move(stream).value();
    auto status = c.stream->start();
    if (!status) { log(QString::fromStdString(status.message()), true); return; }
    c.single = single; c.previous = {}; c.timer->start();
    log(single ? QStringLiteral("正在抓取单帧") : QStringLiteral("开始连续采集")); syncState();
}
void CameraController::start() { dispatch([this] { startCapture(false); }); }
void CameraController::snap() { dispatch([this] { startCapture(true); }); }
void CameraController::stop() { dispatch([this] {
    auto& c = *capture_; if (c.timer) c.timer->stop(); if (c.stream) c.stream->stop();
    log(QStringLiteral("采集已停止")); syncState();
}); }
void CameraController::apply(double exposure, double gain, double rate) { dispatch([this, exposure, gain, rate] {
    auto& c = *capture_;
    if (!c.camera || c.camera->state() != CameraState::open) {
        log(QStringLiteral("请连接相机并停止采集后修改参数"), true); return;
    }
    const std::array<std::pair<const char*, double>, 3> values{{{"ExposureTime", exposure}, {"Gain", gain}, {"AcquisitionFrameRate", rate}}};
    // Validate every value before performing any writes.
    for (const auto& [key, value] : values) {
        auto info = c.camera->feature_info(key);
        if (!info || !std::isfinite(value) || (info.value().minimum && value < *info.value().minimum) ||
            (info.value().maximum && value > *info.value().maximum)) {
            log(QStringLiteral("参数超出设备范围：") + key, true); return;
        }
    }
    for (const auto& [key, value] : values) {
        auto status = c.camera->write_feature(key, value);
        if (!status) { log(QString::fromStdString(status.message()), true); syncState(); return; }
    }
    log(QStringLiteral("参数已写入并回读 · 模拟曝光/增益不改变测试图像")); syncState();
}); }
void CameraController::save(const QUrl& url) {
    if (image_.isNull() || !url.isLocalFile()) { log(QStringLiteral("没有可保存的图像或路径无效"), true); return; }
    const QImage image = image_; const QString path = url.toLocalFile();
    dispatch([this, image, path] {
        if (image.save(path, "PNG")) log(QStringLiteral("已保存：") + path);
        else log(QStringLiteral("图像保存失败：") + path, true);
    });
}
QString CameraController::dimensions() const { return image_.isNull() ? QStringLiteral("—") : QString("%1 × %2").arg(image_.width()).arg(image_.height()); }
int CameraController::pixel(int x, int y) const {
    return image_.valid(x, y) ? image_.constScanLine(y)[x] : -1;
}
void CameraController::clearLogs() { logs_.clear(); error_.clear(); emit logsChanged(); }
