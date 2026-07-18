import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: Theme.elevate2
    clip: true

    property var sourceData: null
    property bool showDonut: true

    readonly property bool hasData: sourceData !== null && sourceData !== undefined

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 20

        // ── Empty state ──
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !hasData
            color: "transparent"

            Column {
                anchors.centerIn: parent
                spacing: 12

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "⏱"
                    font.pixelSize: 48
                    color: Theme.textDim
                    opacity: 0.5
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "No timing data available"
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeMd
                }
            }
        }

        // ── Content ──
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: hasData
            contentHeight: contentCol.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            ColumnLayout {
                id: contentCol
                width: parent.width
                spacing: 20

                // ── Total estimated time header ──
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    visible: hasData

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Estimated load time"
                        font.pixelSize: Theme.fontSizeXs
                        color: Theme.textDim
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: hasData ? sourceData.estimated_total : ""
                        font.pixelSize: 22
                        font.weight: Font.Bold
                        color: Theme.textMain
                    }
                }

                // ══════════════════════════════════
                // SECTION A — Horizontal bar
                // ══════════════════════════════════
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "METRICS"
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Bold
                        color: Theme.textMuted
                        font.letterSpacing: 0.5
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Theme.radiusMd
                        color: Theme.elevate1
                        clip: true

                        Row {
                            anchors.fill: parent
                            spacing: 0

                            Repeater {
                                model: hasData ? sourceData.segments : []

                                delegate: Rectangle {
                                    id: segRect
                                    width: parent.width * modelData.fraction
                                    height: parent.height
                                    color: modelData.color

                                    property real hoverAmount: 0

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        font.pixelSize: Theme.fontSizeSm
                                        font.weight: Font.Bold
                                        color: "white"
                                        visible: parent.width >= implicitWidth
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "white"
                                        opacity: segRect.hoverAmount * 0.15
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onEntered: {
                                            hoverAnim.to = 1
                                            hoverAnim.start()
                                            segTip.show(segTip.text)
                                        }
                                        onExited: {
                                            hoverAnim.to = 0
                                            hoverAnim.start()
                                            segTip.hide()
                                        }
                                    }

                                    NumberAnimation {
                                        id: hoverAnim
                                        target: segRect
                                        property: "hoverAmount"
                                        duration: 120
                                        easing.type: Easing.InOutQuad
                                    }

                                    ToolTip {
                                        id: segTip
                                        text: modelData.label + ": " + modelData.value
                                        visible: false; delay: 0
                                    }
                                }
                            }
                        }
                    }
                }

                // ══════════════════════════════════
                // SECTION B/C — Toggle
                // ══════════════════════════════════
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // Toggle header
                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            text: "COMPARISON VIEW"
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Bold
                            color: Theme.textMuted
                            font.letterSpacing: 0.5
                        }

                        Item { Layout.fillWidth: true }

                        RowLayout {
                            spacing: 4

                            Rectangle {
                                width: 28; height: 28; radius: Theme.radiusSm
                                color: root.showDonut ? Theme.elevate3 : "transparent"
                                border.color: Theme.border; border.width: 1
                                Text {
                                    anchors.centerIn: parent; text: "◉"; font.pixelSize: 16
                                    color: root.showDonut ? Theme.primary : Theme.textMuted
                                }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.showDonut = true
                                }
                            }

                            Rectangle {
                                width: 28; height: 28; radius: Theme.radiusSm
                                color: !root.showDonut ? Theme.elevate3 : "transparent"
                                border.color: Theme.border; border.width: 1
                                Text {
                                    anchors.centerIn: parent; text: "▮"; font.pixelSize: 16
                                    color: !root.showDonut ? Theme.primary : Theme.textMuted
                                }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: root.showDonut = false
                                }
                            }
                        }
                    }

                    // ── Variant B — Donut ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 20
                        visible: root.showDonut

                        Item {
                            Layout.preferredWidth: 140
                            Layout.preferredHeight: 140

                            Canvas {
                                id: donut
                                anchors.fill: parent

                                property real bg: hasData ? sourceData.donut_bg : 0
                                property real own: hasData ? sourceData.donut_own : 0
                                property real oth: hasData ? sourceData.donut_other : 0

                                onBgChanged: requestPaint()
                                onOwnChanged: requestPaint()
                                onOthChanged: requestPaint()
                                Component.onCompleted: requestPaint()

                                onPaint: {
                                    var ctx = getContext("2d")
                                    if (!ctx) return
                                    ctx.clearRect(0, 0, width, height)

                                    var cx = width / 2, cy = height / 2
                                    var r = Math.min(width, height) / 2 - 10
                                    var lw = 20
                                    ctx.lineWidth = lw
                                    ctx.lineCap = "butt"

                                    var start = -Math.PI / 2

                                    if (bg > 0.001) {
                                        var a1 = bg * 2 * Math.PI
                                        ctx.beginPath()
                                        ctx.arc(cx, cy, r, start, start + a1)
                                        ctx.strokeStyle = hasData ? sourceData.bg_color : "#6b7280"
                                        ctx.stroke()
                                        start += a1
                                    }
                                    if (own > 0.001) {
                                        var a2 = own * 2 * Math.PI
                                        ctx.beginPath()
                                        ctx.arc(cx, cy, r, start, start + a2)
                                        ctx.strokeStyle = hasData ? sourceData.own_color : "#5cb85c"
                                        ctx.stroke()
                                        start += a2
                                    }
                                    if (oth > 0.001) {
                                        var a3 = oth * 2 * Math.PI
                                        ctx.beginPath()
                                        ctx.arc(cx, cy, r, start, start + a3)
                                        ctx.strokeStyle = hasData ? sourceData.other_color : "#6b7280"
                                        ctx.stroke()
                                    }
                                }
                            }

                            Column {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: hasData ? sourceData.own_impact : ""
                                    font.pixelSize: Theme.fontSizeXl
                                    font.weight: Font.Bold
                                    color: hasData ? sourceData.own_color : Theme.textMain
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Time for this mod"; font.pixelSize: 9
                                    color: Theme.textDim
                                }
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 24; height: 1; color: Theme.border
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: hasData ? sourceData.other_time : ""
                                    font.pixelSize: Theme.fontSizeSm
                                    font.weight: Font.Bold
                                    color: Theme.textMuted
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Without this mod"; font.pixelSize: 9
                                    color: Theme.textDim
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            Repeater {
                                model: hasData ? sourceData.donut_legend : []

                                delegate: RowLayout {
                                    Layout.fillWidth: true; spacing: 8
                                    Rectangle { width: 14; height: 14; radius: 2; color: modelData.color }
                                    Text { Layout.fillWidth: true; text: modelData.label; font.pixelSize: Theme.fontSizeMd; color: Theme.textMain; elide: Text.ElideRight }
                                    Text { text: modelData.value; font.pixelSize: Theme.fontSizeMd; font.weight: Font.Bold; color: Theme.textMain; font.family: "monospace" }
                                }
                            }
                        }
                    }

                    // ── Variant C — Vertical bars ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: !root.showDonut

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 200
                            spacing: 6

                            Repeater {
                                model: hasData ? sourceData.top5 : []

                                delegate: ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 4

                                    Item {
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true

                                        Rectangle {
                                            anchors.bottom: parent.bottom
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            width: Math.min(parent.width, 50)
                                            height: Math.max(4, (parent.height - 20) * modelData.fraction)
                                            radius: Theme.radiusSm
                                            color: modelData.color
                                            opacity: modelData.is_current ? 1.0 : 0.6

                                            Text {
                                                anchors.bottom: parent.top
                                                anchors.bottomMargin: 4
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: modelData.value
                                                font.pixelSize: Theme.fontSizeXs
                                                font.weight: Font.Bold; color: Theme.textMain
                                            }
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        font.pixelSize: Theme.fontSizeXs
                                        color: modelData.is_current ? Theme.primary : Theme.textMuted
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideRight
                                        font.weight: modelData.is_current ? Font.Bold : Font.Normal
                                    }
                                }
                            }
                        }

                        Text {
                            text: hasData ? sourceData.top5_label : ""
                            font.pixelSize: Theme.fontSizeXs
                            color: Theme.textDim
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }
                }

                // ── Footer ──
                Rectangle {
                    Layout.fillWidth: true
                    height: childrenRect.height
                    color: "transparent"

                    RowLayout {
                        width: parent.width
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        Text {
                            text: hasData && sourceData.timestamp ? "Generated: " + sourceData.timestamp : ""
                            font.pixelSize: Theme.fontSizeXs
                            color: Theme.textDim
                            visible: hasData && sourceData.timestamp ? true : false
                        }
                    }
                }
            }
        }
    }
}
