import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: Theme.elevate2
    property bool downloadEnabled: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        Text {
            id: titleLabel
            text: "Download Queue" + (listView.count > 0 ? " (" + listView.count + ")" : "")
            color: Theme.textMain
            font.pixelSize: Theme.fontSizeMd
            font.weight: Font.Bold
            Layout.bottomMargin: 8
        }

        ColumnLayout {
            visible: downloadQueueModel.progress_total > 0
            Layout.bottomMargin: 8
            spacing: 4

            Text {
                text: "Downloading " + downloadQueueModel.progress_completed
                    + "/" + downloadQueueModel.progress_total
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeXs
            }

            ProgressBar {
                id: progressBar
                from: 0
                to: Math.max(downloadQueueModel.progress_total, 1)
                value: downloadQueueModel.progress_completed
                Layout.fillWidth: true
                height: 6
                contentItem: Rectangle {
                    radius: 3
                    color: Theme.elevate3
                    Rectangle {
                        width: progressBar.visualPosition * parent.width
                        height: parent.height
                        radius: 3
                        color: Theme.primary
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: 8
            clip: true

            Text {
                anchors.centerIn: parent
                visible: listView.count === 0
                text: "No mods in download queue\n\nClick + on a mod to add it"
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSm
                horizontalAlignment: Text.AlignHCenter
            }

            ListView {
                id: listView
                objectName: "downloadList"
                anchors.fill: parent
                visible: listView.count > 0
                spacing: 2
                model: downloadQueueModel

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    width: Theme.scrollbarWidth
                }

                delegate: Rectangle {
                    width: listView.width
                    height: 28
                    radius: Theme.radiusMd
                    color: model.id === downloadQueueModel.downloading_id
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
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        Text {
                            text: model.status === "downloading" ? "\u21BB"
                                : model.status === "success" ? "\u2713"
                                : model.status === "error" ? "\u2715" : ""
                            color: model.status === "error" ? Theme.danger
                                : model.status === "success" ? Theme.success
                                : Theme.textDim
                            font.pixelSize: 12
                        }

                        Text {
                            text: "\u2715"
                            color: Theme.textDim
                            font.pixelSize: 12
                            visible: removeArea.containsMouse
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        radius: 1
                        color: Theme.primary
                        visible: model.id === downloadQueueModel.downloading_id
                        opacity: 0.5
                    }

                    MouseArea {
                        id: removeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: downloadSidebar.removeItem(model.id)
                    }
                }
            }
        }

        Rectangle {
            id: clearButton
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            Layout.topMargin: 0
            radius: Theme.radiusMd
            color: clearBtnMouse.containsMouse ? Theme.dangerBg : "transparent"
            visible: listView.count > 0 && root.downloadEnabled

            Text {
                anchors.centerIn: parent
                text: "Clear Queue"
                color: clearBtnMouse.containsMouse ? Theme.danger : Theme.textDim
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            MouseArea {
                id: clearBtnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: downloadSidebar.clearQueue()
            }
        }

        Rectangle {
            id: downloadButton
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.topMargin: visible ? 4 : 0
            radius: Theme.radiusMd
            color: {
                if (!enabled) return Theme.elevate3
                return downloadBtnMouse.containsMouse ? Qt.lighter(Theme.success, 1.15) : Theme.success
            }
            visible: root.downloadEnabled

            property bool enabled: listView.count > 0 && root.downloadEnabled

            Text {
                anchors.centerIn: parent
                text: "Download All"
                color: downloadButton.enabled ? "#0b0d10" : Theme.textDim
                font.pixelSize: Theme.fontSizeMd
                font.weight: Font.Medium
            }

            MouseArea {
                id: downloadBtnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: downloadButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    if (downloadButton.enabled)
                        downloadSidebar.downloadRequested()
                }
            }
        }

        Rectangle {
            id: stopButton
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            Layout.topMargin: 4
            radius: Theme.radiusMd
            color: stopBtnMouse.containsMouse ? Qt.lighter(Theme.danger, 1.15) : Theme.danger
            visible: !root.downloadEnabled

            Text {
                anchors.centerIn: parent
                text: "Stop"
                color: "white"
                font.pixelSize: Theme.fontSizeMd
                font.weight: Font.Medium
            }

            MouseArea {
                id: stopBtnMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: downloadSidebar.stopRequested()
            }
        }
    }
}
