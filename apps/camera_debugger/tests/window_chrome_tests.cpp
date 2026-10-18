#include <QtTest/QtTest>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include "effectspolicy.h"
#include "windoweffects.h"

// Deliberately not CameraController: no simulator, worker, SDK or device starts.
class FakeCamera final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected MEMBER connected NOTIFY stateChanged)
    Q_PROPERTY(bool running MEMBER running NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY stateChanged)
    Q_PROPERTY(QStringList logs READ logs NOTIFY logsChanged)
    Q_PROPERTY(QString error READ error NOTIFY logsChanged)
    Q_PROPERTY(QVariantList histogram READ histogram NOTIFY frameChanged)
    Q_PROPERTY(int revision READ revision NOTIFY frameChanged)
    Q_PROPERTY(double fps READ fps NOTIFY frameChanged)
    Q_PROPERTY(int dropped READ dropped NOTIFY frameChanged)
    Q_PROPERTY(QString dimensions READ dimensions NOTIFY frameChanged)
public:
    bool connected = false;
    bool running = false;
    int calls = 0;
    QVariantMap parameters() const { return {{"ExposureTime", 10000}, {"Gain", 0}, {"AcquisitionFrameRate", 30}}; }
    QStringList logs() const { return {}; }
    QString error() const { return {}; }
    QVariantList histogram() const { return {}; }
    int revision() const { return 0; }
    double fps() const { return 0; }
    int dropped() const { return 0; }
    QString dimensions() const { return "640 × 480"; }
    void setState(bool isConnected, bool isRunning) { connected = isConnected; running = isRunning; emit stateChanged(); }
    Q_INVOKABLE void connectCamera() { ++calls; }
    Q_INVOKABLE void disconnectCamera() { ++calls; }
    Q_INVOKABLE void start() { ++calls; }
    Q_INVOKABLE void stop() { ++calls; }
    Q_INVOKABLE void snap() { ++calls; }
    Q_INVOKABLE void apply(double, double, double) { ++calls; }
    Q_INVOKABLE void save(const QUrl&) { ++calls; }
    Q_INVOKABLE int pixel(int, int) { return -1; }
    Q_INVOKABLE void clearLogs() { ++calls; }
signals:
    void stateChanged();
    void frameChanged();
    void logsChanged();
};

struct Scene {
    FakeCamera camera;
    WindowEffects effects;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    Scene() {
        engine.rootContext()->setContextProperty("camera", &camera);
        engine.rootContext()->setContextProperty("windowEffects", &effects);
        engine.load(QUrl::fromLocalFile(QStringLiteral(UNIVISION_QML_SOURCE)));
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));
        if (window) { effects.setWindow(window); window->show(); }
    }
    QQuickItem* item(const char* name) { return window ? window->findChild<QQuickItem*>(QString::fromLatin1(name)) : nullptr; }
};

class WindowChromeTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QGuiApplication::setQuitOnLastWindowClosed(false);
        QTest::failOnWarning(QRegularExpression(".*(Binding loop|Only binding to one of multiple|ReferenceError|TypeError).*"));
    }
    void accessibilityPolicyAndStateCorners() {
        for (bool enabled : {false, true})
            for (bool reduced : {false, true})
                for (bool contrast : {false, true})
                    QCOMPARE(WindowEffectsPolicy::materialAllowed(enabled, reduced, contrast), enabled && !reduced && !contrast);
        QCOMPARE(WindowEffectsPolicy::cornerRadius(false, false), 10);
        QCOMPARE(WindowEffectsPolicy::cornerRadius(true, false), 0);
        QCOMPARE(WindowEffectsPolicy::cornerRadius(false, true), 0);
    }
    void effectOffPersistsAndGeometryDoesNotRebuildMaterial() {
        QQuickWindow window;
        WindowEffects effects;
        effects.setWindow(&window);
        effects.setEnabled(false);
        QVERIFY(!effects.active());
        WindowEffects reopened;
        QVERIFY(!reopened.enabled());
        reopened.setWindow(&window);
        QSignalSpy changes(&reopened, &WindowEffects::changed);
        for (int i = 0; i < 100; ++i) { window.resize(500 + i, 400 + i); window.setPosition(i, i); }
        QCoreApplication::processEvents();
        QCOMPARE(changes.count(), 0);
        window.setWindowState(Qt::WindowMaximized);
        QCOMPARE(reopened.cornerRadius(), 0);
        window.setWindowState(Qt::WindowFullScreen);
        QCOMPARE(reopened.cornerRadius(), 0);
        window.setWindowState(Qt::WindowNoState);
        QCOMPARE(reopened.cornerRadius(), 10);
        reopened.setEnabled(true);
    }
    void realSceneKeepsTitleControlsOutsidePreviewAndDrag() {
        Scene scene;
        QVERIFY(scene.window);
        auto* drag = scene.item("windowTitleDragArea");
        auto* preview = scene.item("previewViewport");
        auto* handles = scene.item("windowResizeHandles");
        QVERIFY(drag && preview && handles);
        scene.window->setMinimumSize(QSize(1, 1));
        for (QSize size : {QSize(640, 480), QSize(1100, 720), QSize(1600, 900)}) {
            scene.window->resize(size);
            QTest::qWait(20);
            const QRectF dragRect = drag->mapRectToScene(drag->boundingRect());
            const QRectF previewRect = preview->mapRectToScene(preview->boundingRect());
            QVERIFY(!dragRect.intersects(previewRect));
            QCOMPARE(handles->mapRectToScene(handles->boundingRect()), QRectF(QPointF(0, 0), size));
            for (const char* name : {"windowEffectsButton", "windowFullscreenButton", "windowMinimizeButton", "windowMaximizeButton", "windowCloseButton"}) {
                auto* button = scene.item(name);
                QVERIFY(button);
                QVERIFY(button->property("focusPolicy").toInt() & Qt::TabFocus);
                QQmlExpression nameExpression(qmlContext(button), button, "Accessible.name");
                QVERIFY(!nameExpression.evaluate().toString().isEmpty());
                const QRectF rect = button->mapRectToScene(button->boundingRect());
                QVERIFY(QRectF(QPointF(0, 0), size).contains(rect));
                QVERIFY(!dragRect.intersects(rect));
            }
        }
        QCOMPARE(scene.camera.calls, 0);
    }
    void titleActionsRetainWindowCloseAndFullscreenSemantics() {
        Scene scene;
        QVERIFY(scene.window);
        auto* full = scene.item("windowFullscreenButton");
        auto* zoom = scene.item("windowMaximizeButton");
        auto* minimize = scene.item("windowMinimizeButton");
        auto* close = scene.item("windowCloseButton");
        QVERIFY(full && zoom && minimize && close);
        QVERIFY(QMetaObject::invokeMethod(full, "clicked"));
        QCOMPARE(scene.window->visibility(), QWindow::FullScreen);
        QCOMPARE(scene.effects.cornerRadius(), 0);
        QVERIFY(QMetaObject::invokeMethod(zoom, "clicked"));
        QCOMPARE(scene.window->visibility(), QWindow::Windowed);
        QCOMPARE(scene.effects.cornerRadius(), 10);
        QVERIFY(QMetaObject::invokeMethod(zoom, "clicked"));
        QCOMPARE(scene.window->visibility(), QWindow::Maximized);
        QVERIFY(QMetaObject::invokeMethod(zoom, "clicked"));
        QCOMPARE(scene.window->visibility(), QWindow::Windowed);
        minimize->forceActiveFocus();
        QTest::keyClick(scene.window, Qt::Key_Space);
        QCOMPARE(scene.window->visibility(), QWindow::Minimized);
        scene.window->showNormal();
        scene.window->setMinimumSize(QSize(1, 1));
        scene.window->resize(720, 510);
        scene.window->setPosition(20, 30);
        const QVariant normal = scene.window->property("lastNormalGeometry");
        QVERIFY(normal.isValid());
        QVERIFY(QMetaObject::invokeMethod(close, "clicked"));
        QVERIFY(!scene.window->isVisible());
        auto* saved = scene.window->findChild<QObject*>("startupGeometrySettings");
        QVERIFY(saved);
        QCOMPARE(saved->property("savedWidth").toInt(), 720);
        QCOMPARE(saved->property("savedHeight").toInt(), 510);
        QCOMPARE(saved->property("savedX").toInt(), 20);
        QCOMPARE(saved->property("savedY").toInt(), 30);
        QCOMPARE(scene.camera.calls, 0);
    }
    void acquisitionBusyStateAndPreviewInputRemainIndependent() {
        Scene scene;
        QVERIFY(scene.window);
        auto* apply = scene.item("applyParametersButton");
        auto* exposure = scene.item("exposureParameter");
        auto* roi = scene.item("previewRoiArea");
        QVERIFY(apply && exposure && roi);
        QVERIFY(!apply->isEnabled());
        scene.camera.setState(true, false);
        QVERIFY(apply->isEnabled() && exposure->isEnabled());
        scene.camera.setState(true, true);
        QVERIFY(!apply->isEnabled() && !exposure->isEnabled());
        QCOMPARE(roi->property("acceptedButtons").toInt(), int(Qt::NoButton));
        scene.window->setProperty("roiEnabled", true);
        QCOMPARE(roi->property("acceptedButtons").toInt(), int(Qt::LeftButton));
        QVERIFY(scene.item("windowCloseButton")->isEnabled());
        QVERIFY(scene.item("windowMinimizeButton")->isEnabled());
        QCOMPARE(scene.camera.calls, 0);
    }
    void compactWindowCanScrollToParametersAndEffectsOffIsOpaque() {
        Scene scene;
        QVERIFY(scene.window);
        scene.window->setMinimumSize(QSize(1, 1));
        scene.window->resize(640, 480);
        QTest::qWait(20);
        auto* scroll = scene.item("workspaceScroll");
        auto* apply = scene.item("applyParametersButton");
        auto* surface = scene.item("windowSurface");
        auto* toggle = scene.item("windowEffectsButton");
        QVERIFY(scroll && apply && surface && toggle);
        auto* content = scroll->property("contentItem").value<QQuickItem*>();
        QVERIFY(content);
        content->setProperty("contentX", scroll->property("contentWidth").toDouble() - scroll->property("availableWidth").toDouble());
        content->setProperty("contentY", scroll->property("contentHeight").toDouble() - scroll->property("availableHeight").toDouble());
        QTest::qWait(20);
        QVERIFY(QRectF(0, 0, 640, 480).contains(apply->mapRectToScene(apply->boundingRect())));
        scene.effects.setEnabled(true);
        QVERIFY(QMetaObject::invokeMethod(toggle, "clicked"));
        QVERIFY(!scene.effects.enabled());
        QVERIFY(!scene.effects.active());
        QCOMPARE(surface->property("color").value<QColor>().alpha(), 255);
        QCOMPARE(scene.camera.calls, 0);
        scene.effects.setEnabled(true);
    }
    void allEightResizeHandlesAreBoundedAndDisableForSpecialStates() {
        Scene scene;
        QVERIFY(scene.window);
        auto* handles = scene.item("windowResizeHandles");
        QVERIFY(handles);
        // Exercise nonmac hit regions synthetically; AppKit native edges need hardware.
        handles->setProperty("nativeFrameResize", false);
        QTRY_COMPARE(handles->width(), qreal(scene.window->width()));
        QTRY_COMPARE(handles->height(), qreal(scene.window->height()));
        // Repeater delegates are visual children, not necessarily QObject children.
        QList<QQuickItem*> edges;
        for (auto* child : handles->childItems())
            if (child->objectName() == "windowResizeHandle") edges.append(child);
        QCOMPARE(edges.size(), 8);
        QSet<int> directions;
        for (auto* edge : edges) {
            directions.insert(edge->property("modelData").toInt());
            const QRectF edgeRect = edge->mapRectToItem(handles, edge->boundingRect());
            QVERIFY2(handles->boundingRect().contains(edgeRect), qPrintable(QString("edge %1: %2,%3 %4x%5 in %6x%7")
                .arg(edge->property("modelData").toInt()).arg(edgeRect.x()).arg(edgeRect.y())
                .arg(edgeRect.width()).arg(edgeRect.height()).arg(handles->width()).arg(handles->height())));
            QVERIFY(edge->width() == 8 || edge->height() == 8);
        }
        QCOMPARE(directions.size(), 8);
        QVERIFY(handles->isEnabled());
        scene.window->showMaximized();
        QVERIFY(!handles->isEnabled());
        scene.window->showFullScreen();
        QVERIFY(!handles->isEnabled());
        scene.window->showNormal();
        QVERIFY(handles->isEnabled());
        QCOMPARE(scene.camera.calls, 0);
    }
};

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QTemporaryDir profile;
    if (!profile.isValid()) return 2;
    qputenv("UNIVISION_WINDOW_GEOMETRY_FILE", (profile.path() + "/effects.ini").toUtf8());
    QByteArray settingsArgument = ("--window-geometry-settings=" + profile.path() + "/qml.ini").toUtf8();
    char* arguments[] = {argv[0], settingsArgument.data(), nullptr};
    int argumentCount = 2;
    QGuiApplication app(argumentCount, arguments);
    app.setOrganizationName("UniVisionSyntheticTests");
    app.setApplicationName("WindowChromeTests");
    QQuickStyle::setStyle("Fusion");
    WindowChromeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "window_chrome_tests.moc"
