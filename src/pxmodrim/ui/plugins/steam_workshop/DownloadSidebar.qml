import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: root
    color: Theme.elevate2
    property bool downloadEnabled: true

    // downloadQueueModel is a context property; it can transiently be null
    // while the underlying model is being torn down. Every read of it below
    // goes through these guarded aliases instead of scattering `?.`/ternary
    // checks at each call site.
    readonly property int queueTotal: downloadQueueModel ? downloadQueueModel.progress_total : 0
    readonly property int queueCompleted: downloadQueueModel ? downloadQueueModel.progress_completed : 0
    readonly property real queueBytesDone: downloadQueueModel ? downloadQueueModel.bytes_done : 0
    readonly property real queueBytesTotal: downloadQueueModel ? downloadQueueModel.bytes_total : 0

    function formatMegabytes(bytes) {
        return (bytes / 1048576).toFixed(1)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        Text {
            id: titleLabel
            text: "Download queue" + (listView.count > 0 ? " (" + listView.count + ")" : "")
            color: Theme.textMain
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            font.weight: Font.Bold
            Layout.bottomMargin: 8
        }

        ColumnLayout {
            visible: root.queueTotal > 0
            Layout.bottomMargin: 8
            spacing: 4

            Text {
                text: "Downloading " + root.queueCompleted + "/" + root.queueTotal
                    + (root.queueBytesTotal > 0
                        ? " \u00B7 " + root.formatMegabytes(root.queueBytesDone)
                          + " / " + root.formatMegabytes(root.queueBytesTotal) + " MB"
                        : "")
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            PxProgressBar {
                id: progressBar
                from: 0
                to: root.queueBytesTotal > 0 ? root.queueBytesTotal : Math.max(root.queueTotal, 1)
                value: root.queueBytesTotal > 0 ? root.queueBytesDone : root.queueCompleted
                thickness: 6
                Layout.fillWidth: true
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: 8
            clip: true

            EmptyState {
                anchors.fill: parent
                visible: listView.count === 0
                title: "Download queue is empty"
                detail: "Click + on a mod to add it"
            }

            ListView {
                id: listView
                objectName: "downloadList"
                anchors.fill: parent
                visible: listView.count > 0
                spacing: 2
                model: downloadQueueModel

                ScrollBar.vertical: PxScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    width: listView.width
                    height: 28
                    radius: Theme.radiusMd
                    color: model.status === "downloading"
                        ? Theme.elevate3 : removeArea.containsMouse
                        ? Theme.elevate3 : "transparent"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 4
                        clip: true

                        Text {
                            Layout.fillWidth: true
                            text: model.display
                            color: Theme.textMain
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        Image {
                            readonly property string iconName: model.status === "downloading" ? "refresh"
                                : model.status === "success" ? "check"
                                : model.status === "error" ? "close" : ""
                            visible: iconName.length > 0
                            sourceSize.width: 12
                            sourceSize.height: 12
                            source: visible ? "image://icons/" + iconName + "?color="
                                + encodeURIComponent(model.status === "error" ? Theme.danger
                                    : model.status === "success" ? Theme.success
                                    : Theme.textDim) : ""
                        }

                        Image {
                            visible: removeArea.containsMouse && removeArea.enabled
                            sourceSize.width: 12
                            sourceSize.height: 12
                            source: visible ? "image://icons/close?color=" + encodeURIComponent(Theme.textDim) : ""
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        width: parent.width * model.progress
                        height: 2
                        radius: Theme.radiusXs
                        color: Theme.primary
                        visible: model.status === "downloading"
                        opacity: 0.5
                    }

                    MouseArea {
                        id: removeArea
                        anchors.fill: parent
                        enabled: model.status !== "queued" && model.status !== "downloading"
                        hoverEnabled: true
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: downloadSidebar.removeItem(model.id)
                    }
                }
            }
        }

        PxButton {
            id: clearButton
            Layout.fillWidth: true
            variant: "danger"
            text: "Clear queue"
            visible: listView.count > 0 && root.downloadEnabled
            onClicked: downloadSidebar.clearQueue()
        }

        PxButton {
            id: downloadButton
            Layout.fillWidth: true
            Layout.topMargin: visible ? 4 : 0
            variant: "success"
            text: "Download all"
            visible: root.downloadEnabled
            enabled: listView.count > 0
            onClicked: downloadSidebar.downloadRequested()
        }

        PxButton {
            id: stopButton
            Layout.fillWidth: true
            Layout.topMargin: 4
            variant: "dangerSolid"
            text: "Stop"
            visible: !root.downloadEnabled
            onClicked: downloadSidebar.stopRequested()
        }
    }
}
