import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls"

Rectangle {
    id: root
    height: 80
    color: Theme.elevate1
    property var controller: headerController

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 8
        spacing: 12

        // ── Logo (easter egg: poke it) ──
        Item {
            id: logoBox
            z: 10
            Layout.preferredWidth: 128
            Layout.fillHeight: true

            property int pokes: 0
            readonly property var quips: [
                "Oh, hello.", "Ow.", "Hey!", "Stop poking me.",
                "Load order: sorted.", "Nothing to see here."
            ]

            Timer {
                id: pokeReset
                interval: 2500
                onTriggered: logoBox.pokes = 0
            }

            Image {
                id: logo
                anchors.fill: parent
                source: Theme.logoFileDataUri
                fillMode: Image.PreserveAspectFit
                scale: logoArea.pressed ? 0.92 : logoArea.containsMouse ? 1.08 : 1.0
                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

                transform: Rotation {
                    id: flip
                    origin.x: logo.width / 2
                    origin.y: logo.height / 2
                    axis { x: 0; y: 1; z: 0 }
                    angle: 0
                }

                NumberAnimation {
                    id: spin
                    target: flip
                    property: "angle"
                    from: 0
                    to: 360
                    duration: 600
                    easing.type: Easing.OutBack
                }
            }

            MouseArea {
                id: logoArea
                anchors.centerIn: parent
                width: logo.paintedWidth
                height: logo.paintedHeight
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    logoBox.pokes += 1
                    pokeReset.restart()
                    if (logoBox.pokes >= 10) {
                        bubble.show("Fine. Have a crab. \ud83e\udd80")
                        spin.duration = 1400
                        spin.to = 1440
                        logoBox.pokes = 0
                    } else {
                        bubble.show(logoBox.quips[Math.min(logoBox.pokes - 1, logoBox.quips.length - 1)])
                        spin.duration = 600
                        spin.to = 360
                    }
                    spin.restart()
                }
            }

            Rectangle {
                id: bubble
                property alias text: bubbleText.text
                function show(message) {
                    bubbleText.text = message
                    opacity = 1
                    bubbleFade.restart()
                }
                anchors.left: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 0
                width: bubbleText.implicitWidth + 20
                height: bubbleText.implicitHeight + 12
                radius: Theme.radiusXs
                color: Theme.elevate3
                border.color: Theme.primary
                border.width: 1
                opacity: 0
                z: 10
                visible: opacity > 0

                Text {
                    id: bubbleText
                    anchors.centerIn: parent
                    color: Theme.textMain
                    font.pixelSize: Theme.fontSizeSm
                    font.family: Theme.fontFamily
                }

                SequentialAnimation {
                    id: bubbleFade
                    PauseAnimation { duration: 1400 }
                    NumberAnimation { target: bubble; property: "opacity"; to: 0; duration: 300 }
                }
            }
        }

        // ── Text column ──
        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 0

            Text {
                text: "RimWorld Mod Manager"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
            }

            Text {
                text: "v" + root.controller.appVersion
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeXs
                font.family: Theme.fontFamily
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

                PxButton {
                    variant: "ghost"
                    iconName: "minimize"
                    ToolTip.text: "Minimize"
                    onClicked: root.controller.minimize()
                }

                PxButton {
                    variant: "ghost"
                    iconName: root.controller.maximized ? "restore" : "maximize"
                    ToolTip.text: root.controller.maximized ? "Restore" : "Maximize"
                    onClicked: root.controller.maximize()
                }

                PxButton {
                    variant: "danger"
                    iconName: "close"
                    ToolTip.text: "Close"
                    onClicked: root.controller.closeWindow()
                }
            }

            // Action buttons (bottom row)
            Row {
                Layout.alignment: Qt.AlignRight
                spacing: 6

                PxButton {
                    iconName: "settings"
                    ToolTip.text: root.controller.tooltips.settings || "Settings"
                    onClicked: root.controller.openSettings()
                }

                PxButton {
                    iconName: "save"
                    iconColor: root.controller.unsavedChanges ? Theme.warning : "transparent"
                    ToolTip.text: root.controller.tooltips.save || "Save"
                    onClicked: root.controller.save()

                    Rectangle {
                        visible: root.controller.unsavedChanges
                        width: 8; height: 8
                        radius: Theme.radiusXs
                        color: Theme.warning
                        border.width: 2
                        border.color: Theme.elevate3
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 4
                        anchors.rightMargin: 4
                    }
                }

                PxButton {
                    variant: "primary"
                    iconName: "sort"
                    ToolTip.text: root.controller.tooltips.sort || "Auto-sort"
                    onClicked: root.controller.autoSort()
                }

                PxButton {
                    iconName: "refresh"
                    ToolTip.text: root.controller.tooltips.refresh || "Refresh"
                    onClicked: root.controller.refresh()
                }

                // ── Launch split-button ──
                Row {
                    spacing: 1

                    PxButton {
                        variant: "success"
                        iconName: "play"
                        text: "Play"
                        ToolTip.text: "Launch game"
                        onClicked: root.controller.launch()
                    }

                    PxButton {
                        id: chevronBtn
                        objectName: "launchStrategyButton"
                        variant: "success"
                        iconName: "chevron-down"
                        ToolTip.text: "Launch strategy"
                        onClicked: strategyMenu.popup(chevronBtn, 0, chevronBtn.height + 4)
                    }
                }
            }
        }
    }

    // ── Dropdown menu (children of root, outside layouts) ──
    PxMenu {
        id: strategyMenu
        objectName: "launchStrategyMenu"

        Repeater {
            model: root.controller.strategies

            PxMenuItem {
                required property var modelData
                text: modelData.label
                checkable: true
                checked: root.controller.strategyIndex === modelData.index
                onTriggered: root.controller.setStrategy(modelData.index)
            }
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
}
