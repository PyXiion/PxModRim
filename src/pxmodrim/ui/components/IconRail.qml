import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Rectangle {
    id: root
    color: Theme.elevate2

    property int currentIndex: 0
    property bool collapsed: root.width < Theme.railCollapseWidth
    property var tabModel: railModel

    signal tabSelected(int index)
    signal tabHovered(int index)
    signal settingsRequested()
    signal helpActionRequested(string actionId)

    component RailButton: Item {
        id: button
        property string iconName
        property string label
        property bool collapsed
        signal clicked()

        width: parent ? parent.width : 0
        height: 44

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: Theme.radiusMd
            color: area.containsMouse ? Theme.elevate3 : "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: button.collapsed ? 0 : 12
                anchors.rightMargin: button.collapsed ? 0 : 8
                spacing: 10

                Item {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: button.collapsed ? Qt.AlignCenter : Qt.AlignVCenter
                    Layout.fillWidth: button.collapsed

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/" + button.iconName + "?color=" + encodeURIComponent(Theme.textMuted)
                        sourceSize.width: 18; sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                    }
                }

                Text {
                    visible: !button.collapsed
                    Layout.fillWidth: true
                    text: button.label
                    color: Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: button.clicked()
            }

            Loader {
                active: button.collapsed && area.containsMouse
                sourceComponent: PxToolTip {
                    visible: true
                    text: button.label
                }
            }
        }

        Accessible.role: Accessible.Button
        Accessible.name: button.label
    }

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
                    radius: Theme.radiusMd
                    color: delegate.active
                        ? Theme.elevate4
                        : (hoverArea.containsMouse ? Theme.elevate3 : "transparent")

                    Rectangle {
                        visible: delegate.active
                        width: 3
                        height: 18
                        radius: Theme.radiusXs
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
                            font.family: Theme.fontFamily
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
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.tabHovered(index)
                        onClicked: {
                            root.currentIndex = index
                            root.tabSelected(index)
                        }
                    }

                    Loader {
                        active: root.collapsed && hoverArea.containsMouse
                        sourceComponent: PxToolTip {
                            visible: true
                            text: modelData.label || ""
                        }
                    }

                    Behavior on color {
                        ColorAnimation { duration: 120; easing.type: Easing.OutCubic }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        Column {
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.bottomMargin: 4

            RailButton {
                iconName: "settings"
                label: "Settings"
                collapsed: root.collapsed
                onClicked: root.settingsRequested()
            }

            RailButton {
                id: helpButton
                iconName: "help"
                label: "Help"
                collapsed: root.collapsed
                onClicked: helpMenu.popup(helpButton, helpButton.width, helpButton.height - helpMenu.implicitHeight)
            }
        }
    }

    PxMenu {
        id: helpMenu
        objectName: "helpMenu"

        Repeater {
            model: railHelpEntries

            PxMenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: root.helpActionRequested(modelData.id)
            }
        }
    }
}
