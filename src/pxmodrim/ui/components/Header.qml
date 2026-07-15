import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    height: 80
    color: Theme.elevate1
    property var controller: null

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 8
        spacing: 12

        // ── Logo ──
        Image {
            source: Theme.logoFileDataUri
            Layout.preferredWidth: 128
            Layout.fillHeight: true
            fillMode: Image.PreserveAspectFit
        }

        // ── Text column ──
        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            Text {
                text: "RimWorld Mod Manager"
                color: Theme.textMuted
                font.pixelSize: 11
            }

            Text {
                text: "v0.1.0"
                color: Theme.textDim
                font.pixelSize: 11
            }
        }

        Item { Layout.fillWidth: true }

        // ── Buttons column ──
        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            // Window controls (top row)
            Row {
                Layout.alignment: Qt.AlignRight
                spacing: 6
                visible: root.controller.is_frameless

                HeaderButton {
                    iconName: "minimize"
                    onClicked: root.controller.minimize()
                }

                HeaderButton {
                    iconName: root.controller.maximized ? "restore" : "maximize"
                    onClicked: root.controller.maximize()
                }

                HeaderButton {
                    iconName: "close"
                    bgHoverColor: Theme.danger
                    iconHoverColor: "#ffffff"
                    onClicked: root.controller.closeWindow()
                }
            }

            // Action buttons (bottom row)
            Row {
                Layout.alignment: Qt.AlignRight
                spacing: 6

                HeaderButton {
                    iconName: "settings"
                    bgColor: Theme.elevate0
                    tooltip: "Settings"
                    onClicked: root.controller.openSettings()
                }

                HeaderButton {
                    iconName: "save"
                    bgColor: Theme.elevate0
                    tooltip: "Save (Ctrl+S)"
                    onClicked: root.controller.save()
                }

                HeaderButton {
                    iconName: "sort"
                    tooltip: "Auto-sort"
                    bgColor: Theme.primary
                    bgHoverColor: Qt.lighter(Theme.primary, 1.15)
                    iconColor: "#0b0d10"
                    iconHoverColor: "#0b0d10"
                    onClicked: root.controller.autoSort()
                }

                HeaderButton {
                    iconName: "refresh"
                    bgColor: Theme.elevate0
                    tooltip: "Refresh (F5)"
                    onClicked: root.controller.refresh()
                }

                // ── Launch split-button ──
                Rectangle {
                    id: launchBtn
                    height: 32
                    width: playRow.implicitWidth
                    radius: Theme.radiusMd
                    color: launchHover.containsMouse ? Qt.lighter(Theme.success, 1.15) : Theme.success

                    Row {
                        id: playRow
                        height: parent.height
                        spacing: 0

                        Item {
                            width: 28; height: parent.height
                            Image {
                                anchors.centerIn: parent
                                source: "image://icons/play?color=" + encodeURIComponent("white")
                                sourceSize.width: 16; sourceSize.height: 16
                            }
                        }

                        Text {
                            text: "Play"
                            color: "white"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            anchors.verticalCenter: parent.verticalCenter
                            rightPadding: 10
                        }

                        Rectangle {
                            width: 1; height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            color: Qt.rgba(1, 1, 1, 0.2)
                        }

                        Item {
                            width: 20; height: parent.height
                            Image {
                                anchors.centerIn: parent
                                source: "image://icons/chevron-down?color=" + encodeURIComponent("white")
                                sourceSize.width: 12; sourceSize.height: 12
                            }
                        }
                    }

                    MouseArea {
                        id: launchHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        onClicked: function(mouse) {
                            if (mouse.x < playRow.implicitWidth - 20) {
                                root.controller.launch()
                            } else {
                                strategyMenu.popup()
                            }
                        }

                        ToolTip {
                            text: "Launch game\nStrategy: " + (root.controller.strategyIndex === 0 ? "Direct executable" : "Steam")
                            visible: launchHover.containsMouse
                            delay: 300
                        }
                    }
                }

            }
        }
    }

    // ── Dropdown menu (children of root, outside layouts) ──
    Menu {
        id: strategyMenu

        MenuItem {
            text: "Direct executable"
            checkable: true
            checked: root.controller.strategyIndex === 0
            onTriggered: root.controller.setStrategy(0)
        }

        MenuItem {
            text: "Steam"
            checkable: true
            checked: root.controller.strategyIndex === 1
            onTriggered: root.controller.setStrategy(1)
        }
    }

    // ── Bottom border ──
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.border
    }

    // ── Drag region (behind buttons, z: -1) ──
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton
        onPressed: root.controller.dragStarted()
        onDoubleClicked: root.controller.maximize()

        cursorShape: root.controller.is_frameless ? Qt.OpenHandCursor : Qt.ArrowCursor
    }

    // ── Reusable button component ──
    component HeaderButton: Rectangle {
        id: btn
        width: 32; height: 32
        radius: Theme.radiusMd
        color: mouseArea.containsMouse ? bgHoverColor : bgColor
        property string iconName: ""
        property string tooltip: ""
        property color bgColor: "transparent"
        property color bgHoverColor: Theme.elevate3
        property color iconColor: Theme.textMuted
        property color iconHoverColor: Theme.textMain

        signal clicked()

        Image {
            anchors.centerIn: parent
            source: {
                if (btn.iconName === "") return ""
                var color = mouseArea.containsMouse ? btn.iconHoverColor : btn.iconColor
                return "image://icons/" + btn.iconName + "?color=" + encodeURIComponent(color)
            }
            sourceSize.width: 16; sourceSize.height: 16
            fillMode: Image.PreserveAspectFit
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.clicked()

            ToolTip {
                text: btn.tooltip
                visible: mouseArea.containsMouse && btn.tooltip !== ""
                delay: 300
            }
        }
    }
}
