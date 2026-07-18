import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: Theme.elevate2

    property int currentIndex: 0
    property bool collapsed: root.width < 110
    property var tabModel: railModel

    signal tabSelected(int index)
    signal tabHovered(int index)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ListView {
            id: listView
            objectName: "railList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: root.tabModel
            currentIndex: root.currentIndex

            delegate: Item {
                id: delegate
                width: listView.width
                height: 44
                property bool active: index === listView.currentIndex

                Rectangle {
                    id: bg
                    anchors.fill: parent
                    anchors.margins: 4
                    radius: 6
                    color: delegate.active
                        ? Theme.elevate4
                        : (hoverArea.containsMouse ? Theme.elevate3 : "transparent")

                    Rectangle {
                        visible: delegate.active
                        width: 3
                        height: 18
                        radius: 1.5
                        color: Theme.primary
                        anchors.left: parent.left
                        anchors.leftMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: root.collapsed ? 0 : 12
                        anchors.rightMargin: 8
                        spacing: 10
                        visible: !root.collapsed

                        Item {
                            width: 18; height: 18
                            Layout.alignment: Qt.AlignVCenter

                            Image {
                                anchors.centerIn: parent
                                source: "image://icons/" + (modelData.icon || "") + "?color=" +
                                    encodeURIComponent(delegate.active ? Theme.primary : Theme.textMuted)
                                sourceSize.width: 18; sourceSize.height: 18
                                fillMode: Image.PreserveAspectFit
                                opacity: 1
                            }
                        }

                        Text {
                            id: labelText
                            Layout.fillWidth: true
                            text: modelData.label || ""
                            color: delegate.active ? Theme.primary : Theme.textMuted
                            font.pixelSize: Theme.fontSizeMd
                            font.weight: delegate.active ? Font.Medium : Font.Normal
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignLeft
                            opacity: 1
                        }
                    }

                    Item {
                        anchors.fill: parent
                        visible: root.collapsed

                        Image {
                            id: collapsedIcon
                            width: 18; height: 18
                            anchors.centerIn: parent
                            source: "image://icons/" + (modelData.icon || "") + "?color=" +
                                encodeURIComponent(delegate.active ? Theme.primary : Theme.textMuted)
                            sourceSize.width: 18; sourceSize.height: 18
                            fillMode: Image.PreserveAspectFit
                            opacity: 1
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: root.tabHovered(index)
                        onClicked: {
                            root.currentIndex = index
                            root.tabSelected(index)
                        }
                    }

                    Behavior on color {
                        ColorAnimation { duration: 120; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }
}
