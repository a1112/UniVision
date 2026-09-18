import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    width: 1520; height: 920
    minimumWidth: 1120; minimumHeight: 720
    visible: true
    title: "UniVision · 相机调试"
    color: "#0d131c"
    font.family: Qt.platform.os === "windows" ? "Microsoft YaHei UI" : "Noto Sans CJK SC"
    font.pixelSize: 13
    palette.window: "#151e2a"
    palette.windowText: "#e5edf7"
    palette.base: "#101823"
    palette.text: "#e5edf7"
    palette.button: "#233144"
    palette.buttonText: "#e5edf7"
    palette.highlight: "#3885ff"
    palette.highlightedText: "#ffffff"
    property color muted: "#8a9bb2"
    property real zoom: 1
    property bool crosshair: true
    property bool roiEnabled: false
    property rect roi: Qt.rect(0, 0, 0, 0)
    property string pixelText: "移动鼠标查看像素"

    component Panel: Rectangle {
        color: "#151e2a"; border.color: "#29374a"; radius: 8
    }
    component Caption: Label { color: window.muted; font.pixelSize: 12 }
    component Action: Button {
        id: action
        property bool primary: false
        implicitHeight: 36
        leftPadding: 16; rightPadding: 16
        background: Rectangle {
            radius: 5
            color: !action.enabled ? "#1a2432" : action.down ? "#235cb1" : action.primary ? "#2878ee" : action.hovered ? "#30435c" : "#223044"
            border.color: action.primary ? "#468fff" : "#3a4b63"
            opacity: action.enabled ? 1 : 0.45
        }
        contentItem: Text {
            text: action.text; font: action.font; color: action.enabled ? "#edf4ff" : "#627086"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        }
    }
    component Parameter: ColumnLayout {
        id: parameter
        property string label
        property string unit
        property real minimum: 0
        property real maximum: 100
        property real step: 1
        property alias value: slider.value
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { text: parameter.label; Layout.fillWidth: true }
            TextField {
                id: number
                Layout.preferredWidth: 96
                text: slider.value.toFixed(parameter.step < 1 ? 1 : 0)
                horizontalAlignment: Text.AlignRight
                validator: DoubleValidator { bottom: parameter.minimum; top: parameter.maximum; locale: "C" }
                onEditingFinished: {
                    if (acceptableInput) slider.value = Number(text)
                    text = Qt.binding(function() { return slider.value.toFixed(parameter.step < 1 ? 1 : 0) })
                }
            }
            Caption { text: parameter.unit; Layout.preferredWidth: 24 }
        }
        Slider { id: slider; Layout.fillWidth: true; from: parameter.minimum; to: parameter.maximum; stepSize: parameter.step }
        RowLayout {
            Caption { text: parameter.minimum }
            Item { Layout.fillWidth: true }
            Caption { text: parameter.maximum }
        }
    }
    FileDialog {
        id: saveDialog
        title: "保存原始 Mono8 图像"
        fileMode: FileDialog.SaveFile
        nameFilters: ["PNG 图像 (*.png)"]
        defaultSuffix: "png"
        onAccepted: camera.save(selectedFile)
    }
    Connections {
        target: camera
        function onStateChanged() {
            if (camera.connected) {
                exposure.value = camera.parameters.ExposureTime
                gain.value = camera.parameters.Gain
                rate.value = camera.parameters.AcquisitionFrameRate
            }
        }
        function onFrameChanged() { histogram.requestPaint() }
    }
    header: Rectangle {
        height: 76; color: "#151f2c"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#2a384b" }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 22; anchors.rightMargin: 22; spacing: 16
            Rectangle {
                width: 38; height: 38; radius: 10; color: "#246dd0"
                Label { anchors.centerIn: parent; text: "◎"; font.pixelSize: 30; color: "white" }
            }
            ColumnLayout {
                spacing: 2
                Label { text: "相机调试"; font.pixelSize: 21; font.weight: Font.DemiBold }
                Caption { text: "UNIVISION  /  CAMERA WORKSPACE"; font.pixelSize: 9; font.letterSpacing: 1 }
            }
            Rectangle { Layout.leftMargin: 14; width: 1; height: 30; color: "#344154" }
            Label { text: "●  " + (camera.connected ? "模拟相机已连接" : "设备未连接"); color: camera.connected ? "#52d5a0" : window.muted }
            Item { Layout.fillWidth: true }
            Caption { text: camera.dimensions + "   ·   " + (camera.running ? camera.fps.toFixed(1) : "0.0") + " FPS" }
            Action { text: camera.running ? "■  停止采集" : "▶  开始采集"; primary: true; enabled: camera.connected; onClicked: camera.running ? camera.stop() : camera.start() }
            Action { text: "单帧抓取"; enabled: camera.connected && !camera.running; onClicked: camera.snap() }
            Action { text: "保存图像"; enabled: camera.revision > 0; onClicked: saveDialog.open() }
        }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 34; radius: 5
            color: camera.error.length ? "#452630" : "#172d3e"
            Label {
                anchors.fill: parent; anchors.leftMargin: 12
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                color: camera.error.length ? "#ffb3ba" : "#8ebee3"
                text: camera.error.length ? camera.error : "模拟模式  ·  图像来自 UniVision Simulator；真实相机适配器尚未接入。"
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            Panel {
                Layout.preferredWidth: 224; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 18
                    RowLayout {
                        Label { text: "设备列表"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        Caption { text: "01" }
                    }
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: 94; radius: 6
                        color: "#1c304b"; border.color: "#3b79c8"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 12; spacing: 6
                            Label { text: "▣   模拟相机 01"; font.weight: Font.DemiBold }
                            Caption { text: "     SIM-0001" }
                            Label { text: "     ●  " + (camera.connected ? "已连接" : "可连接"); color: camera.connected ? "#51d19b" : "#8aa9ce"; font.pixelSize: 11 }
                        }
                    }
                    Action { Layout.fillWidth: true; text: camera.connected ? "断开连接" : "连接设备"; onClicked: camera.connected ? camera.disconnectCamera() : camera.connectCamera() }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2a3749" }
                    Label { text: "设备信息"; font.weight: Font.DemiBold }
                    Repeater {
                        model: [ ["厂商", "UniVision"], ["型号", "Virtual Camera"], ["序列号", "SIM-0001"], ["接口类型", "Simulator"], ["像素格式", "Mono8"], ["图像尺寸", "640 × 480"] ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Caption { text: modelData[0] }
                            Item { Layout.fillWidth: true }
                            Label { text: modelData[1]; font.pixelSize: 11; color: "#c4d1e3" }
                        }
                    }
                    Item { Layout.fillHeight: true }
                    Rectangle {
                        Layout.fillWidth: true; implicitHeight: 94; radius: 5; color: "#101924"
                        Caption { anchors.fill: parent; anchors.margins: 12; wrapMode: Text.WordWrap; text: "测试图像为动态灰度条纹。\n曝光和增益可读写，但不改变模拟图像。"; lineHeight: 1.4 }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
                Panel {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 12; spacing: 10
                        RowLayout {
                            Label { text: "实时预览"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            Label { text: camera.running ? "● LIVE" : camera.revision > 0 ? "● 已暂停" : "● 待采集"; color: camera.running ? "#57d4aa" : window.muted; font.pixelSize: 11 }
                        }
                        Rectangle {
                            id: viewport; objectName: "previewViewport"
                            Layout.fillWidth: true; Layout.fillHeight: true; color: "#090e15"; clip: true
                            Flickable {
                                id: flick
                                anchors.fill: parent; clip: true
                                contentWidth: Math.max(width, frame.width); contentHeight: Math.max(height, frame.height)
                                interactive: !window.roiEnabled
                                Item {
                                    id: frame
                                    x: Math.max(0, (flick.width - width) / 2); y: Math.max(0, (flick.height - height) / 2)
                                    width: Math.min(flick.width / 640, flick.height / 480) * 640 * window.zoom
                                    height: width * 480 / 640
                                    Image {
                                        anchors.fill: parent; cache: false; smooth: false
                                        source: camera.revision > 0 ? "image://frames/" + camera.revision : ""
                                    }
                                    Item {
                                        anchors.fill: parent; visible: camera.revision > 0 && window.crosshair
                                        Rectangle { anchors.centerIn: parent; width: 30; height: 1; color: "#42dce7" }
                                        Rectangle { anchors.centerIn: parent; width: 1; height: 30; color: "#42dce7" }
                                    }
                                    Rectangle {
                                        visible: camera.revision > 0 && window.roiEnabled && window.roi.width > 0
                                        x: window.roi.x * frame.width; y: window.roi.y * frame.height
                                        width: window.roi.width * frame.width; height: window.roi.height * frame.height
                                        color: "#1242dce7"; border.color: "#42dce7"; border.width: 1
                                    }
                                    MouseArea {
                                        anchors.fill: parent; hoverEnabled: true
                                        acceptedButtons: window.roiEnabled ? Qt.LeftButton : Qt.NoButton
                                        property real startX: 0
                                        property real startY: 0
                                        onPressed: function(mouse) { startX = mouse.x / width; startY = mouse.y / height }
                                        onPositionChanged: function(mouse) {
                                            const nx = Math.max(0, Math.min(1, mouse.x / width))
                                            const ny = Math.max(0, Math.min(1, mouse.y / height))
                                            const px = Math.min(639, Math.floor(nx * 640))
                                            const py = Math.min(479, Math.floor(ny * 480))
                                            const value = camera.pixel(px, py)
                                            window.pixelText = value < 0 ? "暂无图像" : "X: " + px + "   Y: " + py + "   灰度: " + value
                                            if (pressed && window.roiEnabled) window.roi = Qt.rect(Math.min(startX, nx), Math.min(startY, ny), Math.abs(nx - startX), Math.abs(ny - startY))
                                        }
                                    }
                                }
                                ScrollBar.horizontal: ScrollBar {}
                                ScrollBar.vertical: ScrollBar {}
                            }
                            Column {
                                anchors.centerIn: parent; spacing: 12; visible: camera.revision === 0
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "◎"; font.pixelSize: 54; color: "#3a4e67" }
                                Label { anchors.horizontalCenter: parent.horizontalCenter; text: "等待图像"; font.pixelSize: 18; color: "#a2b2c7" }
                                Caption { text: "连接设备后，开始采集或抓取单帧" }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            Action { text: "−"; implicitWidth: 32; onClicked: window.zoom = Math.max(0.5, window.zoom / 1.25) }
                            Caption { text: window.zoom.toFixed(2) + "×"; Layout.preferredWidth: 38; horizontalAlignment: Text.AlignHCenter }
                            Action { text: "+"; implicitWidth: 32; onClicked: window.zoom = Math.min(8, window.zoom * 1.25) }
                            Action { text: "适应"; onClicked: { window.zoom = 1; flick.contentX = 0; flick.contentY = 0 } }
                            Action { text: "十字线"; primary: window.crosshair; onClicked: window.crosshair = !window.crosshair }
                            Action { text: "ROI"; primary: window.roiEnabled; onClicked: window.roiEnabled = !window.roiEnabled }
                            Item { Layout.fillWidth: true }
                            Caption { text: window.pixelText; font.pixelSize: 10 }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.minimumHeight: 180; Layout.preferredHeight: 180; Layout.maximumHeight: 180; spacing: 12
                    Panel {
                        Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14
                            Label { text: "灰度直方图"; font.weight: Font.DemiBold }
                            Canvas {
                                id: histogram
                                Layout.fillWidth: true; Layout.fillHeight: true
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d"); ctx.reset()
                                    ctx.strokeStyle = "#29384a"; ctx.lineWidth = 1
                                    for (let i = 0; i < 5; ++i) { ctx.beginPath(); ctx.moveTo(0, i * height / 4); ctx.lineTo(width, i * height / 4); ctx.stroke() }
                                    const bins = camera.histogram
                                    if (bins.length === 0) return
                                    let max = 1; for (let i = 0; i < bins.length; ++i) max = Math.max(max, bins[i])
                                    ctx.fillStyle = "#8dadd4"
                                    for (let i = 0; i < bins.length; ++i) { const h = bins[i] / max * (height - 8); ctx.fillRect(i * width / 256, height - h, Math.max(1, width / 256), h) }
                                }
                            }
                            RowLayout { Caption { text: "0" } Item { Layout.fillWidth: true } Caption { text: "128" } Item { Layout.fillWidth: true } Caption { text: "255" } }
                        }
                    }
                    Panel {
                        Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.fillHeight: true
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 14
                            RowLayout {
                                Label { text: "采集日志"; font.weight: Font.DemiBold; Layout.fillWidth: true }
                                ToolButton { text: "清空"; implicitHeight: 24; onClicked: camera.clearLogs() }
                            }
                            ListView {
                                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                                model: camera.logs
                                delegate: Label { required property string modelData; width: ListView.view.width; text: modelData; color: "#a8b9d0"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere }
                                ScrollBar.vertical: ScrollBar {}
                            }
                        }
                    }
                }
            }
            Panel {
                Layout.preferredWidth: 294; Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 18; spacing: 16
                    Label { text: "采集参数"; font.weight: Font.DemiBold; font.pixelSize: 16 }
                    Rectangle { Layout.fillWidth: true; height: 2; color: "#347eee" }
                    ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        contentWidth: availableWidth; clip: true
                        ColumnLayout {
                            width: parent.width; spacing: 18
                            Parameter { id: exposure; Layout.fillWidth: true; label: "曝光时间"; unit: "μs"; minimum: 1; maximum: 1000000; value: 10000; enabled: camera.connected && !camera.running }
                            Parameter { id: gain; Layout.fillWidth: true; label: "增益"; unit: "dB"; minimum: 0; maximum: 24; step: 0.1; value: 0; enabled: camera.connected && !camera.running }
                            Parameter { id: rate; Layout.fillWidth: true; label: "帧率"; unit: "fps"; minimum: 0.1; maximum: 1000; step: 0.1; value: 30; enabled: camera.connected && !camera.running }
                            Rectangle { Layout.fillWidth: true; height: 1; color: "#29374a" }
                            RowLayout { Layout.fillWidth: true; Caption { text: "像素格式" } Item { Layout.fillWidth: true } Label { text: "Mono8" } }
                            RowLayout { Layout.fillWidth: true; Caption { text: "图像尺寸" } Item { Layout.fillWidth: true } Label { text: "640 × 480" } }
                            RowLayout { Layout.fillWidth: true; Caption { text: "采集方式" } Item { Layout.fillWidth: true } Label { text: "连续 / 单帧" } }
                            Caption { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "停止采集后可修改参数。单帧抓取由软件启动一次采集，不等同于硬件触发。"; lineHeight: 1.4 }
                            Caption { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: window.roiEnabled ? "ROI 为显示选区，不修改相机采集区域。拖动预览画面绘制选区。" : "放大后可拖动画面。开启 ROI 可框选图像区域。"; lineHeight: 1.4 }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Action { text: "默认值"; enabled: camera.connected && !camera.running; onClicked: { exposure.value = 10000; gain.value = 0; rate.value = 30 } }
                        Action { text: "应用参数"; Layout.fillWidth: true; primary: true; enabled: camera.connected && !camera.running; onClicked: camera.apply(exposure.value, gain.value, rate.value) }
                    }
                }
            }
        }
    }
    footer: Rectangle {
        height: 34; color: "#121b27"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 20
            Caption { text: "UniVision 0.1   /   Qt Quick" }
            Item { Layout.fillWidth: true }
            Caption { text: "已显示 " + camera.revision + " 帧     |     流丢帧 " + camera.dropped + "     |     Mono8     |     Simulator" }
            Label { Layout.leftMargin: 16; text: "●  " + (camera.running ? "采集中" : "就绪"); color: "#55cfa1"; font.pixelSize: 11 }
        }
    }
}


