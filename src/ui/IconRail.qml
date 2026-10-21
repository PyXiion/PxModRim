import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import PxModRim

Rectangle {
    id: root
    color: Theme.elevate2

    property int currentIndex: 0
    property bool collapsed: root.width < Theme.railCollapseWidth

    signal tabSelected(int index)
    signal tabHovered(int index)
    signal settingsRequested()
    signal helpActionRequested(string actionId)

    component RailItem: Item {
        id: item
        property string iconName
        property string label
        property bool collapsed
        property bool active: false
        signal clicked()
        signal entered()

        width: parent ? parent.width : 0
        height: 44

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: Theme.radiusMd
            color: item.active
                ? Theme.elevate4
                : (area.containsMouse ? Theme.elevate3 : "transparent")

            Behavior on color {
                ColorAnimation { duration: 120; easing.type: Easing.OutCubic }
            }

            Rectangle {
                visible: item.active
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
                anchors.leftMargin: item.collapsed ? 0 : 12
                anchors.rightMargin: item.collapsed ? 0 : 8
                spacing: 10

                Item {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    Layout.alignment: item.collapsed ? Qt.AlignCenter : Qt.AlignVCenter
                    Layout.fillWidth: item.collapsed

                    Image {
                        anchors.centerIn: parent
                        source: "image://icons/" + item.iconName + "?color=" +
                            encodeURIComponent(item.active ? Theme.primary : Theme.textMuted)
                        sourceSize.width: 18; sourceSize.height: 18
                        fillMode: Image.PreserveAspectFit
                    }
                }

                Text {
                    visible: !item.collapsed
                    Layout.fillWidth: true
                    text: item.label
                    color: item.active ? Theme.primary : Theme.textMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                    font.weight: item.active ? Font.Medium : Font.Normal
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: item.entered()
                onClicked: item.clicked()
            }

            Loader {
                active: item.collapsed && area.containsMouse
                sourceComponent: PxToolTip {
                    visible: true
                    text: item.label
                }
            }
        }

        Accessible.role: Accessible.Button
        Accessible.name: item.label
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
            model: railModel
            currentIndex: root.currentIndex

            delegate: RailItem {
                width: listView.width
                iconName: modelData.icon || ""
                label: modelData.label || ""
                collapsed: root.collapsed
                active: index === listView.currentIndex
                onEntered: root.tabHovered(index)
                onClicked: {
                    root.currentIndex = index
                    root.tabSelected(index)
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

            RailItem {
                iconName: "settings"
                label: "Settings"
                collapsed: root.collapsed
                onClicked: root.settingsRequested()
            }

            RailItem {
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
