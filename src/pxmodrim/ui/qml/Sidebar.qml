import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: Theme.elevate2

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: listView
            objectName: "listView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            spacing: 2
            model: sidebarModel

            section.property: "sectionName"
            section.labelPositioning: ViewSection.CurrentLabelAtStart
            section.delegate: Item {
                width: listView.width
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
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                    font.capitalization: Font.AllUppercase
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: Theme.scrollbarWidth
            }

            onCurrentIndexChanged: {
                if (currentIndex >= 0)
                    sidebarPanel.entrySelected(currentIndex)
            }

            delegate: Rectangle {
                id: delegate
                width: listView.width
                height: 36
                radius: Theme.radiusMd
                color: listView.currentIndex === index
                    ? Theme.elevate4
                    : (hoverArea.containsMouse ? Theme.elevate3 : "transparent")

                // Left accent border
                Rectangle {
                    visible: listView.currentIndex === index
                    width: 3
                    height: 18
                    radius: 1.5
                    color: Theme.primary
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: listView.currentIndex === index ? 13 : 16
                    anchors.rightMargin: 10
                    spacing: 8

                    // Icon
                    Item {
                        width: 16; height: 16
                        Layout.alignment: Qt.AlignVCenter
                        visible: model.iconName !== ""

                        Image {
                            anchors.centerIn: parent
                            visible: model.iconName !== ""
                            source: {
                                var name = model.iconName || ""
                                if (name === "") return ""
                                var color = listView.currentIndex === index
                                    ? Theme.primary : (model.iconColor || Theme.textDim)
                                return "image://icons/" + name + "?color=" + encodeURIComponent(color)
                            }
                            sourceSize.width: 14; sourceSize.height: 14
                            fillMode: Image.PreserveAspectFit
                        }
                    }

                    // Label
                    Text {
                        Layout.fillWidth: true
                        text: model.label || ""
                        color: listView.currentIndex === index
                            ? Theme.primary : Theme.textMain
                        font.pixelSize: Theme.fontSizeMd
                        font.weight: listView.currentIndex === index ? Font.Medium : Font.Normal
                        elide: Text.ElideRight
                        maximumLineCount: 1
                    }

                    // Badge
                    Rectangle {
                        visible: (model.count || 0) > 0
                        Layout.preferredWidth: badgeText.contentWidth + 12
                        Layout.preferredHeight: 20
                        radius: 10
                        color: model.badgeBg || Theme.elevate4

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: String(model.count || 0)
                            color: model.badgeFg || Theme.textMuted
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Medium
                        }
                    }
                }

                MouseArea {
                    id: hoverArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: listView.currentIndex = index
                }
            }
        }
    }
}
