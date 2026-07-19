import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: Theme.elevate2

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 0

        Text {
            id: titleLabel
            text: "Download Queue"
            color: Theme.textMain
            font.pixelSize: Theme.fontSizeMd
            font.weight: Font.Bold
            Layout.bottomMargin: 8
        }

        ListView {
            id: listView
            objectName: "downloadList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.bottomMargin: 8
            clip: true
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
                color: removeArea.containsMouse ? Theme.elevate3 : "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: model.display
                        color: Theme.textMain
                        font.pixelSize: Theme.fontSizeSm
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    Text {
                        text: "\u2715"
                        color: Theme.textDim
                        font.pixelSize: 12
                        visible: removeArea.containsMouse
                    }
                }

                MouseArea {
                    id: removeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: downloadSidebar.removeItem(model.id)
                }
            }
        }

        Button {
            id: downloadButton
            text: "Download All"
            enabled: listView.count > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            onClicked: downloadSidebar.downloadRequested()
        }
    }
}
