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

        property var selectedIndices: []
        property int anchorIndex: -1

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

            color: {
                if (listView.selectedIndices.indexOf(index) >= 0)
                    return Theme.elevate4
                if (mouseArea.containsMouse && !listView.itemIsHeld)
                    return Theme.elevate3
                return "transparent"
            }
            opacity: (listView.dragSourceIndex === index && listView.itemIsHeld) ? 0.0 : 1.0

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true

                property bool wasDragged: false
                property bool dragConfirmed: false
                property bool mousePressed: false

                onPressed: (mouse) => {
                    mousePressed = true
                    wasDragged = false
                    dragConfirmed = false

                    if (mouse.modifiers & Qt.ControlModifier) {
                        var i = listView.selectedIndices.indexOf(index)
                        if (i >= 0)
                            listView.selectedIndices = listView.selectedIndices.filter(function(idx) { return idx !== index })
                        else
                            listView.selectedIndices = listView.selectedIndices.concat([index])
                        listView.anchorIndex = index
                    } else if (mouse.modifiers & Qt.ShiftModifier && listView.anchorIndex >= 0) {
                        var start = Math.min(listView.anchorIndex, index)
                        var end = Math.max(listView.anchorIndex, index)
                        var range = []
                        for (var r = start; r <= end; ++r)
                            range.push(r)
                        listView.selectedIndices = range
                    } else {
                        listView.selectedIndices = [index]
                        listView.anchorIndex = index
                    }

                    modListPanel.selectionChanged(listView.selectedIndices)

                    var rootPos = delegateRect.mapToItem(root, 0, 0)
                    dragProxy.modName = model.name || ""
                    dragProxy.modPackageId = model.packageId || ""
                    dragProxy.providerColor = model.providerColor || ""
                    dragProxy.pressOffsetY = mouse.y
                    dragProxy.x = rootPos.x
                    dragProxy.y = rootPos.y

                    listView.dragSourceIndex = index
                    listView.currentIndex = index
                }

                onPositionChanged: (mouse) => {
                    if (!mousePressed)
                        return

                    if (!dragConfirmed) {
                        dragConfirmed = true
                        listView.itemIsHeld = true
                        dragProxy.visible = true
                    }

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
                    mousePressed = false
                    dragProxy.visible = false
                    listView.itemIsHeld = false
                    var hadDrag = wasDragged
                    listView.dragSourceIndex = -1
                    dragConfirmed = false
                    if (hadDrag) {
                        modListPanel.dragEnded()
                    }
                }

                onCanceled: {
                    mousePressed = false
                    dragProxy.visible = false
                    listView.itemIsHeld = false
                    listView.dragSourceIndex = -1
                    dragConfirmed = false
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

                // Startup impact pill
                Rectangle {
                    visible: model.startupImpact > 0
                    height: 20
                    width: siText.width + 12
                    radius: Theme.radiusSm
                    color: model.startupImpact < 1.0 ? "#3d8b3d" :
                           model.startupImpact < 5.0 ? "#c98a1e" : "#b23b3b"
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: siText
                        anchors.centerIn: parent
                        text: Math.round(model.startupImpact * 1000) + "ms"
                        color: "white"
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Bold
                    }

                    ToolTip {
                        text: "Startup impact: " + Math.round(model.startupImpact * 1000) + "ms"
                        visible: siMouseArea.containsMouse
                        delay: 300
                    }

                    MouseArea {
                        id: siMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.WhatsThisCursor
                    }
                }

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

    // ── Keyboard navigation ──
    Shortcut {
        sequence: "Up"
        context: Qt.WindowShortcut
        onActivated: navigate(false, -1)
    }
    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        onActivated: navigate(false, 1)
    }
    Shortcut {
        sequence: "Shift+Up"
        context: Qt.WindowShortcut
        onActivated: navigate(true, -1)
    }
    Shortcut {
        sequence: "Shift+Down"
        context: Qt.WindowShortcut
        onActivated: navigate(true, 1)
    }
    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        onActivated: {
            var sels = listView.selectedIndices
            if (sels.length === 0 && listView.currentIndex >= 0)
                sels = [listView.currentIndex]
            modListPanel.toggleChecked(sels)
        }
    }
    Shortcut {
        sequence: "Ctrl+A"
        context: Qt.WindowShortcut
        onActivated: {
            if (searchFocused)
                return
            var all = []
            for (var i = 0; i < listView.count; ++i)
                all.push(i)
            listView.selectedIndices = all
            listView.anchorIndex = listView.count - 1
            listView.currentIndex = listView.count - 1
            modListPanel.selectionChanged(listView.selectedIndices)
        }
    }

    function navigate(extend, direction) {
        if (searchFocused)
            return

        var newIdx = listView.currentIndex + direction
        if (newIdx < 0 || newIdx >= listView.count)
            return

        if (extend) {
            if (listView.anchorIndex < 0)
                listView.anchorIndex = listView.currentIndex

            var start = Math.min(listView.anchorIndex, newIdx)
            var end = Math.max(listView.anchorIndex, newIdx)
            var range = []
            for (var i = start; i <= end; ++i)
                range.push(i)
            listView.selectedIndices = range
        } else {
            listView.selectedIndices = [newIdx]
            listView.anchorIndex = newIdx
        }

        listView.currentIndex = newIdx
        modListPanel.selectionChanged(listView.selectedIndices)
    }
}
