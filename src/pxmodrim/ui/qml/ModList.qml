import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    color: Theme.elevate1
    clip: true

    ListView {
        id: listView
        objectName: "listView"
        anchors.fill: parent
        spacing: 2
        model: modListModel
        delegate: dragDelegate
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 2000

        reuseItems: true
        cacheBuffer: 100

        property bool itemIsHeld: false
        property int dragSourceIndex: -1

        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
            active: true
        }

        WheelHandler {
            target: listView
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                listView.flick(0, event.angleDelta.y * 10)
                event.accepted = true
            }
        }

        move: Transition {
            NumberAnimation { properties: "y"; duration: 120; easing.type: Easing.InOutQuad }
        }
        moveDisplaced: Transition {
            NumberAnimation { properties: "y"; duration: 120; easing.type: Easing.InOutQuad }
        }

        onCurrentIndexChanged: {
            if (currentIndex >= 0 && !listView.itemIsHeld)
                modListPanel.modSelected(modListPanel.uuidAt(currentIndex))
        }
    }

    // ── Drag proxy ──
    Rectangle {
        id: dragProxy
        visible: false
        width: listView.width
        height: 52
        radius: Theme.radiusMd
        color: Theme.elevate3
        opacity: 0.92
        z: 1000

        property int pressOffsetY: 0
        property string modName: ""
        property string modPackageId: ""
        property string providerColor: ""

        Rectangle {
            x: 28; y: 10; width: 32; height: 32
            radius: Theme.radiusSm
            color: dragProxy.providerColor || Theme.elevate3
            Text {
                anchors.centerIn: parent
                text: dragProxy.modName ? dragProxy.modName.charAt(0).toUpperCase() : "?"
                color: Theme.elevate0
                font.pixelSize: Theme.fontSizeLg
                font.weight: Font.Bold
            }
        }

        Column {
            x: 68; y: 8
            width: parent.width - 78
            Text {
                width: parent.width
                text: dragProxy.modName
                color: Theme.textMain
                font.bold: true
                font.pixelSize: Theme.fontSizeMd
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Text {
                width: parent.width
                text: dragProxy.modPackageId
                visible: text !== ""
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSm
                font.family: "monospace"
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    // ── Delegate ──
    Component {
        id: dragDelegate

        Rectangle {
            id: delegateRect
            width: listView.width
            height: 52
            radius: Theme.radiusMd

            color: ListView.isCurrentItem
                ? Theme.elevate4
                : ((mouseArea.containsMouse && !listView.itemIsHeld) ? Theme.elevate3 : "transparent")
            opacity: (listView.dragSourceIndex === index && listView.itemIsHeld) ? 0.0 : 1.0

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true

                property bool wasDragged: false

                onPressed: (mouse) => {
                    wasDragged = false
                    var rootPos = delegateRect.mapToItem(root, 0, 0)
                    dragProxy.modName = model.name || ""
                    dragProxy.modPackageId = model.packageId || ""
                    dragProxy.providerColor = model.providerColor || ""
                    dragProxy.pressOffsetY = mouse.y
                    dragProxy.x = rootPos.x
                    dragProxy.y = rootPos.y
                    dragProxy.visible = true

                    listView.dragSourceIndex = index
                    listView.currentIndex = index
                    listView.itemIsHeld = true
                }

                onPositionChanged: (mouse) => {
                    if (!listView.itemIsHeld)
                        return

                    var rootPos = mouseArea.mapToItem(root, mouse.x, mouse.y)
                    dragProxy.y = rootPos.y - dragProxy.pressOffsetY

                    var proxyCenterInContent = dragProxy.mapToItem(listView.contentItem, dragProxy.width / 2, dragProxy.height / 2)
                    var spacing = 2
                    var targetIndex = Math.floor(proxyCenterInContent.y / (52 + spacing))
                    targetIndex = Math.max(0, Math.min(targetIndex, listView.count - 1))
                    if (targetIndex === listView.dragSourceIndex)
                        return

                    wasDragged = true
                    modListPanel.moveRow(listView.dragSourceIndex, targetIndex)
                    listView.dragSourceIndex = targetIndex
                    listView.currentIndex = targetIndex
                }

                onReleased: {
                    dragProxy.visible = false
                    listView.itemIsHeld = false
                    var hadDrag = wasDragged
                    listView.dragSourceIndex = -1
                    if (hadDrag) {
                        modListPanel.dragEnded()
                    }
                }

                onCanceled: {
                    dragProxy.visible = false
                    listView.itemIsHeld = false
                    listView.dragSourceIndex = -1
                }
            }

            // ── Checkbox ──
            Rectangle {
                x: 16; y: 18; width: 16; height: 16
                radius: 3
                color: {
                    var cs = model.checkState
                    cs === Qt.Checked ? Theme.primary : "transparent"
                }
                border.color: {
                    var cs = model.checkState
                    cs === Qt.Checked ? Theme.primary : Theme.border
                }
                border.width: 1.5

                Text {
                    anchors.centerIn: parent
                    visible: {
                        var cs = model.checkState
                        cs === Qt.Checked
                    }
                    text: "\u2713"
                    color: Theme.elevate0
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        modListPanel.toggleCheck(index)
                    }
                }
            }

            // ── Avatar ──
            Rectangle {
                x: 42; y: 8; width: 36; height: 36
                radius: Theme.radiusSm
                color: model.providerColor || Theme.elevate3

                Text {
                    anchors.centerIn: parent
                    text: model.name ? model.name.charAt(0).toUpperCase() : "?"
                    color: Theme.elevate0
                    font.pixelSize: Theme.fontSizeLg
                    font.weight: Font.Bold
                }
            }

            // ── Name + Package ID ──
            Column {
                x: 88; y: 8
                width: parent.width - badgesRow.width - 100
                spacing: 1

                Text {
                    width: parent.width
                    text: model.name || ""
                    color: Theme.textMain
                    font.bold: true
                    font.pixelSize: Theme.fontSizeMd
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Text {
                    width: parent.width
                    text: model.packageId || ""
                    visible: text !== ""
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSm
                    font.family: "monospace"
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }

            // ── Badges row (right-aligned) ──
            Row {
                id: badgesRow
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6

                // Version pill
                Rectangle {
                    visible: !!model.modVersion
                    height: 20
                    width: vText.width + 12
                    radius: Theme.radiusSm
                    color: Theme.primaryBg
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: vText
                        anchors.centerIn: parent
                        text: model.modVersion || ""
                        color: Theme.primary
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Bold
                    }
                }

                // Error badge
                Rectangle {
                    visible: model.hasError
                    height: 20
                    width: eText.width + 12
                    radius: Theme.radiusSm
                    color: Theme.danger
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: eText
                        anchors.centerIn: parent
                        text: "\u2716"
                        color: "white"
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                    }

                    MouseArea {
                        id: errorHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }

                    ToolTip {
                        text: model.errorTooltip || ""
                        visible: errorHover.containsMouse
                        delay: 300
                    }
                }

                // Warning badge
                Rectangle {
                    visible: model.hasWarning
                    height: 20
                    width: wText.width + 12
                    radius: Theme.radiusSm
                    color: Theme.warning
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: wText
                        anchors.centerIn: parent
                        text: "\u26a0"
                        color: "white"
                        font.pixelSize: Theme.fontSizeSm
                        font.bold: true
                    }

                    MouseArea {
                        id: warningHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                    }

                    ToolTip {
                        text: model.warningTooltip || ""
                        visible: warningHover.containsMouse
                        delay: 300
                    }
                }
            }
        }
    }
}
