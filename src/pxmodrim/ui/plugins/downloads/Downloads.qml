import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: root
    color: Theme.elevate0

    readonly property var stateLabels: ({
        queued: "Queued",
        checking: "Checking",
        downloading: "Downloading",
        updated: "Updated",
        unchanged: "Up to date",
        failed: "Failed",
        cancelled: "Cancelled"
    })

    function stateColor(state) {
        if (state === "updated") return Theme.success
        if (state === "failed") return Theme.danger
        if (state === "downloading" || state === "checking") return Theme.primary
        return Theme.textDim
    }

    function summary() {
        var m = downloadsModel
        if (m.total === 0)
            return "No downloads yet"
        var parts = [m.updated + " updated", m.unchanged + " up to date"]
        if (m.failed > 0)
            parts.push(m.failed + " failed")
        return (m.busy ? "Working: " : "Last run: ") + m.finished + " / " + m.total
            + " · " + parts.join(" · ")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                spacing: 2

                Text {
                    text: "Downloads"
                    color: Theme.textMain
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLg
                    font.weight: Font.Bold
                }
                Text {
                    text: root.summary()
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            Item { Layout.fillWidth: true }

            PxButton {
                visible: downloadsModel.busy
                variant: "danger"
                iconName: "close"
                text: "Stop"
                onClicked: downloadsPanel.stop()
            }
            PxButton {
                visible: !downloadsModel.busy && downloadsModel.failed > 0
                text: "Retry failed (" + downloadsModel.failed + ")"
                iconName: "refresh"
                onClicked: downloadsPanel.retryFailed()
            }
            PxButton {
                visible: !downloadsModel.busy && downloadsModel.total > 0
                variant: "ghost"
                text: "Clear"
                onClicked: downloadsModel.clear()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            visible: downloadsModel.total > 0
            radius: Theme.radiusMd
            color: Theme.elevate2

            function formatBytes(n) {
                if (n >= 1073741824) return (n / 1073741824).toFixed(2) + " GB"
                if (n >= 1048576) return (n / 1048576).toFixed(1) + " MB"
                return Math.round(n / 1024) + " KB"
            }

            Canvas {
                id: graph
                anchors.fill: parent
                anchors.margins: 1
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    var h = downloadsModel.speedHistory
                    if (h.length < 2) return
                    var peak = Math.max.apply(null, h.concat([1]))
                    var stepX = width / (90 - 1)
                    var x0 = width - (h.length - 1) * stepX
                    ctx.beginPath()
                    ctx.moveTo(x0, height)
                    for (var i = 0; i < h.length; i++)
                        ctx.lineTo(x0 + i * stepX, height - (h[i] / peak) * (height - 28) - 2)
                    ctx.lineTo(width, height)
                    ctx.closePath()
                    ctx.fillStyle = Qt.alpha(Theme.primary, 0.25)
                    ctx.fill()
                    ctx.beginPath()
                    for (var j = 0; j < h.length; j++) {
                        var y = height - (h[j] / peak) * (height - 28) - 2
                        if (j === 0) ctx.moveTo(x0, y)
                        else ctx.lineTo(x0 + j * stepX, y)
                    }
                    ctx.strokeStyle = Theme.primary
                    ctx.lineWidth = 2
                    ctx.stroke()
                }
                Connections {
                    target: downloadsModel
                    function onSpeed_changed() { graph.requestPaint() }
                }
            }

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 10
                text: parent.formatBytes(downloadsModel.speed) + "/s"
                color: Theme.textMain
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeLg
                font.weight: Font.Bold
            }
            Text {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 10
                visible: text.length > 0
                text: downloadsModel.phase === "login" ? "Logging in to Steam\u2026"
                    : downloadsModel.phase === "query"
                        ? "Querying mod info from Steam\u2026 " + downloadsModel.resolved + " / " + downloadsModel.total
                        : ""
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 10
                text: "Downloaded " + parent.formatBytes(downloadsModel.bytesDone)
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
        }

        PxProgressBar {
            Layout.fillWidth: true
            visible: downloadsModel.total > 0
            thickness: 6
            from: 0
            to: Math.max(downloadsModel.total, 1)
            value: downloadsModel.finished
        }

        RowLayout {
            Layout.fillWidth: true
            visible: downloadsModel.total > 0
            spacing: 6

            Repeater {
                model: [
                    { key: "all", label: "All", count: downloadsModel.total },
                    { key: "active", label: "Active", count: downloadsModel.active },
                    { key: "updated", label: "Updated", count: downloadsModel.updated },
                    { key: "failed", label: "Failed", count: downloadsModel.failed }
                ]
                PxButton {
                    required property var modelData
                    variant: downloadsModel.filter === modelData.key ? "primary" : "ghost"
                    text: modelData.label + " " + modelData.count
                    implicitHeight: 28
                    onClicked: downloadsModel.setFilter(modelData.key)
                }
            }
            Item { Layout.fillWidth: true }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            EmptyState {
                anchors.fill: parent
                visible: listView.count === 0
                title: downloadsModel.total === 0 ? "No downloads yet" : "Nothing in this filter"
                detail: downloadsModel.total === 0
                    ? "Updates from the mod list, organizer, header, auto-update and the Steam Workshop tab show up here."
                    : ""
            }

            ListView {
                id: listView
                objectName: "downloadsList"
                anchors.fill: parent
                spacing: 2
                model: downloadsModel
                visible: count > 0
                ScrollBar.vertical: PxScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Rectangle {
                    width: listView.width
                    height: 36
                    radius: Theme.radiusMd
                    color: model.state === "downloading" ? Theme.elevate3 : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 12
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: Theme.textMain
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeMd
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: model.state === "failed" && model.error.length > 0
                            Layout.maximumWidth: 320
                            text: model.error
                            color: Theme.textDim
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.preferredWidth: 90
                            horizontalAlignment: Text.AlignRight
                            text: root.stateLabels[model.state] || model.state
                            color: root.stateColor(model.state)
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        width: parent.width * model.progress
                        height: 2
                        radius: Theme.radiusXs
                        color: Theme.primary
                        visible: model.state === "downloading"
                    }
                }
            }
        }
    }
}
