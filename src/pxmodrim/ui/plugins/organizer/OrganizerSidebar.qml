import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    color: Theme.elevate2

    ListView {
        id: filters
        anchors.fill: parent
        anchors.margins: 10
        anchors.topMargin: 8
        clip: true
        spacing: 2
        model: organizerFilters
        currentIndex: 0
        section.property: "sectionName"
        section.delegate: Item {
            width: filters.width
            height: 28
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
            Text {
                x: 8
                anchors.verticalCenter: parent.verticalCenter
                text: section
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeXs
                font.bold: true
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.5
            }
        }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Rectangle {
            id: row
            required property int index
            required property string key
            required property string filterLabel
            required property int count
            required property string iconName
            width: filters.width
            height: 36
            radius: Theme.radiusMd
            color: filters.currentIndex === index ? Theme.elevate4
                   : (mouse.containsMouse ? Theme.elevate3 : "transparent")
            Rectangle {
                visible: filters.currentIndex === row.index
                x: 1
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 18
                radius: 1.5
                color: Theme.primary
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 9
                Image {
                    source: "image://icons/" + row.iconName + "?color="
                            + encodeURIComponent(filters.currentIndex === row.index ? Theme.primary : Theme.textDim)
                    sourceSize.width: 16
                    sourceSize.height: 16
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                Text {
                    text: row.filterLabel
                    Layout.fillWidth: true
                    color: filters.currentIndex === row.index ? Theme.primary : Theme.textMain
                    font.pixelSize: Theme.fontSizeMd
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: row.count > 0
                    Layout.preferredWidth: countText.contentWidth + 12
                    Layout.preferredHeight: 20
                    radius: 10
                    color: Theme.elevate4
                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: String(row.count)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    filters.currentIndex = row.index
                    organizerPanel.chooseFilter(row.key)
                }
            }
        }
    }
}
