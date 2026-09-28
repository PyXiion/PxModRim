import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    color: Theme.elevate2

    ListView {
        id: filters
        anchors.fill: parent
        anchors.margins: 12
        anchors.topMargin: 8
        clip: true
        spacing: 2
        model: organizerFilters
        currentIndex: 0
        section.property: "sectionName"
        section.delegate: Item {
            width: filters.width
            height: 26
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
            Text {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: -1
                text: section
                color: Theme.textDim
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 0.5
            }
        }
        ScrollBar.vertical: PxScrollBar { policy: ScrollBar.AsNeeded }

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
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 18
                radius: 1.5
                color: Theme.primary
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: filters.currentIndex === row.index ? 13 : 16
                anchors.rightMargin: 10
                spacing: 8
                Image {
                    source: "image://icons/" + row.iconName + "?color="
                            + encodeURIComponent(filters.currentIndex === row.index ? Theme.primary : Theme.textDim)
                    sourceSize.width: 14
                    sourceSize.height: 14
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignVCenter
                }
                Text {
                    text: row.filterLabel
                    Layout.fillWidth: true
                    color: filters.currentIndex === row.index ? Theme.primary : Theme.textMain
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                    font.weight: filters.currentIndex === row.index ? Font.Medium : Font.Normal
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: row.count > 0
                    Layout.preferredWidth: countText.contentWidth + 12
                    Layout.preferredHeight: 20
                    radius: Theme.radiusPill
                    color: Theme.elevate4
                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: String(row.count)
                        color: Theme.textMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }
                }
            }
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    filters.currentIndex = row.index
                    organizerPanel.chooseFilter(row.key)
                }
            }
        }
    }
}
