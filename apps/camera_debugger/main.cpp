#include "camera_controller.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QFileInfo>
#include <QDebug>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("UniVision Camera Debugger");
    app.setOrganizationName("UniVision");
    QQuickStyle::setStyle("Fusion");
    auto* provider = new FrameProvider;
    CameraController controller(provider);
    bool qmlWarnings = false;
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                     [&](const QList<QQmlError>&) { qmlWarnings = true; });
    engine.addImageProvider("frames", provider);
    engine.rootContext()->setContextProperty("camera", &controller);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("UniVision.Debugger", "Main");
    if (app.arguments().contains("--smoke-test")) {
        // Exercise the actual threaded core bridge and render the real QML scene.
        const QString directory = qEnvironmentVariable("UNIVISION_SMOKE_DIR", ".");
        controller.connectCamera();
        QTimer::singleShot(300, &controller, [&] { controller.apply(5000, 6, 25); controller.start(); });
        QTimer::singleShot(1600, &controller, [&] { controller.stop(); });
        QTimer::singleShot(1900, &controller, [&] { controller.snap(); });
        QTimer::singleShot(2100, &controller, [&] { controller.apply(5000, -1, 25); });
        QTimer::singleShot(2300, &controller, [&, directory] {
            controller.save(QUrl::fromLocalFile(directory + "/capture.png"));
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
            if (window) window->grabWindow().save(directory + "/interface.png");
        });
        QTimer::singleShot(2800, &controller, [&, directory] {
            double total = 0; for (const auto& bin : controller.histogram()) total += bin.toInt();
            auto* root = engine.rootObjects().value(0);
            auto* viewport = root ? root->findChild<QQuickItem*>("previewViewport") : nullptr;
            const bool ok = !qmlWarnings && viewport && viewport->height() > 200 && viewport->width() > 300 &&
                controller.connected() && !controller.running() && controller.revision() > 10 &&
                controller.parameters().value("ExposureTime").toDouble() == 5000 &&
                controller.parameters().value("Gain").toDouble() == 6 &&
                total == 640 * 480 && controller.pixel(10, 10) >= 0 &&
                QFileInfo(directory + "/capture.png").size() > 0 && QFileInfo(directory + "/interface.png").size() > 0;
            qInfo() << "GUI smoke:" << ok << "frames:" << controller.revision() << "histogram:" << total;
            controller.disconnectCamera();
            QTimer::singleShot(200, &app, [&, ok] { app.exit(ok && !controller.connected() ? 0 : 2); });
        });
    }
    return app.exec();
}
