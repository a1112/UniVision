#pragma once
#include <QObject>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QUrl>
#include <functional>
#include <memory>

class FrameProvider final : public QQuickImageProvider {
public:
    FrameProvider() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override;
    void setImage(const QImage& image);
private:
    QMutex mutex_;
    QImage image_;
};
struct CaptureState;
class CameraController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap device READ device NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY stateChanged)
    Q_PROPERTY(QStringList logs READ logs NOTIFY logsChanged)
    Q_PROPERTY(QString error READ error NOTIFY logsChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY frameChanged)
    Q_PROPERTY(int revision READ revision NOTIFY frameChanged)
    Q_PROPERTY(double fps READ fps NOTIFY frameChanged)
    Q_PROPERTY(qulonglong dropped READ dropped NOTIFY frameChanged)
    Q_PROPERTY(QString dimensions READ dimensions NOTIFY frameChanged)
public:
    explicit CameraController(FrameProvider* provider, QObject* parent = nullptr);
    ~CameraController() override;
    bool connected() const { return connected_; }
    bool running() const { return running_; }
    QVariantMap device() const { return device_; }
    QVariantMap parameters() const { return parameters_; }
    QStringList logs() const { return logs_; }
    QString error() const { return error_; }
    QVariantList histogram() const { return histogram_; }
    int revision() const { return revision_; }
    double fps() const { return fps_; }
    qulonglong dropped() const { return dropped_; }
    QString dimensions() const;
    Q_INVOKABLE void connectCamera();
    Q_INVOKABLE void disconnectCamera();
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void snap();
    Q_INVOKABLE void apply(double exposure, double gain, double rate);
    Q_INVOKABLE void save(const QUrl& url);
    Q_INVOKABLE int pixel(int x, int y) const;
    Q_INVOKABLE void clearLogs();
signals:
    void stateChanged();
    void frameChanged();
    void logsChanged();
private:
    void dispatch(std::function<void()> action);
    void syncState();
    void log(const QString& message, bool error = false);
    void startCapture(bool single);
    FrameProvider* provider_;
    QThread thread_;
    QObject* worker_;
    std::unique_ptr<CaptureState> capture_;
    QTimer refresh_;
    bool connected_ = false, running_ = false;
    QVariantMap device_, parameters_;
    QStringList logs_;
    QString error_;
    QVariantList histogram_;
    QImage image_;
    int revision_ = 0;
    double fps_ = 0;
    qulonglong dropped_ = 0;
};
