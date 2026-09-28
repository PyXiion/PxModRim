import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

// Shared filter list (Mods and Organizer). The model exposes: filterLabel, count,
// iconName, sectionName and optionally iconColor, badgeBg, badgeFg.
Rectangle {
    id: root

    property var model: null
    property alias currentIndex: filters.currentIndex
    signal activated(int index)

    color: Theme.elevate2

    ListView {
        id: filters
        objectName: "listView"
        anchors.fill: parent
        anchors.margins: 12
        anchors.topMargin: 8
        clip: true
        spacing: 2
        model: root.model
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
            readonly property bool selected: filters.currentIndex === index
            width: filters.width
            height: 36
            radius: Theme.radiusMd
            color: selected ? Theme.elevate4
                   : (mouse.containsMouse ? Theme.elevate3 : "transparent")
            Rectangle {
                visible: row.selected
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: 18
                radius: Theme.radiusXs
                color: Theme.primary
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: row.selected ? 13 : 16
                anchors.rightMargin: 10
                spacing: 8
                Image {
                    source: "image://icons/" + (model.iconName || "folder") + "?color="
                            + encodeURIComponent(row.selected ? Theme.primary
                                                              : (model.iconColor || Theme.textDim))
                    sourceSize.width: 14
                    sourceSize.height: 14
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignVCenter
                }
                Text {
                    text: model.filterLabel || ""
                    Layout.fillWidth: true
                    color: row.selected ? Theme.primary : Theme.textMain
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                    font.weight: row.selected ? Font.Medium : Font.Normal
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
                Rectangle {
                    visible: (model.count || 0) > 0
                    Layout.preferredWidth: countText.contentWidth + 12
                    Layout.preferredHeight: 20
                    radius: Theme.radiusPill
                    color: model.badgeBg || Theme.elevate4
                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: String(model.count || 0)
                        color: model.badgeFg || Theme.textMuted
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
                    filters.currentIndex = index
                    root.activated(index)
                }
            }
        }
    }
}
