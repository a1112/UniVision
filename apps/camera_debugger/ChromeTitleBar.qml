import QtQuick
import QtQuick.Window
import QtQuick.Controls

Rectangle {
    id: bar
    property var targetWindow
    height: 48
    radius: windowEffects.cornerRadius
    color: windowEffects.highContrast ? systemPalette.window
        : windowEffects.active ? "#b3151f2c" : "#151f2c"
    SystemPalette { id: systemPalette }
    Rectangle { y: parent.height / 2; width: parent.width; height: parent.height / 2; color: parent.color }

    function toggleMaximize() {
        if (targetWindow.visibility === Window.Maximized || targetWindow.visibility === Window.FullScreen)
            targetWindow.showNormal()
        else targetWindow.showMaximized()
    }
    function toggleFullscreen() {
        if (targetWindow.visibility === Window.FullScreen) targetWindow.showNormal()
        else targetWindow.showFullScreen()
    }

    // Only this empty title region starts native movement. Acquisition controls,
    // menus, preview pan and ROI retain their own pointer handling.
    Item {
        id: dragArea
        objectName: "windowTitleDragArea"
        anchors.left: parent.left
        anchors.right: controls.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 8
        Label {
            anchors.fill: parent
            anchors.leftMargin: 8
            text: bar.targetWindow.title
            color: windowEffects.highContrast ? systemPalette.windowText : "#e5edf7"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onDoubleTapped: bar.toggleMaximize()
        }
        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: if (active) bar.targetWindow.startSystemMove()
        }
    }
    component ChromeButton: Button {
        width: 34; height: 32
        focusPolicy: Qt.StrongFocus
        Accessible.name: ToolTip.text
        ToolTip.visible: hovered
        palette.buttonText: windowEffects.highContrast ? systemPalette.buttonText : "#e5edf7"
        palette.button: windowEffects.highContrast ? systemPalette.button : "#233144"
        palette.highlight: systemPalette.highlight
    }
    Row {
        id: controls
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        ChromeButton {
            objectName: "windowEffectsButton"
            width: 64
            text: "材质"
            checkable: true
            checked: windowEffects.enabled
            ToolTip.text: windowEffects.enabled ? "关闭背景材质" : "启用背景材质"
            onClicked: windowEffects.setEnabled(!windowEffects.enabled)
        }
        ChromeButton {
            objectName: "windowFullscreenButton"
            text: bar.targetWindow.visibility === Window.FullScreen ? "↙" : "⛶"
            ToolTip.text: bar.targetWindow.visibility === Window.FullScreen ? "退出全屏" : "全屏"
            onClicked: bar.toggleFullscreen()
        }
        ChromeButton {
            objectName: "windowMinimizeButton"
            text: "−"
            ToolTip.text: "最小化"
            onClicked: bar.targetWindow.showMinimized()
        }
        ChromeButton {
            objectName: "windowMaximizeButton"
            text: bar.targetWindow.visibility === Window.Maximized || bar.targetWindow.visibility === Window.FullScreen ? "↙" : "□"
            ToolTip.text: bar.targetWindow.visibility === Window.Maximized || bar.targetWindow.visibility === Window.FullScreen ? "还原" : "最大化"
            onClicked: bar.toggleMaximize()
        }
        ChromeButton {
            objectName: "windowCloseButton"
            text: "×"
            ToolTip.text: "关闭窗口"
            // Preserve Main.qml's onClosing geometry save and normal app shutdown.
            onClicked: bar.targetWindow.close()
        }
    }
}
