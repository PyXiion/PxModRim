import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    color: Theme.elevate1
    clip: true

    property bool keyboardActive: modListHasFocus && listView.activeFocus

    ListView {
        id: listView
        objectName: "listView"
        anchors.fill: parent
        spacing: 2
        model: modListModel
        delegate: dragDelegate
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 2000
        focus: true
        currentIndex: -1
        Accessible.role: Accessible.List
        Accessible.name: "Mods"

        reuseItems: true
        cacheBuffer: 100

        property int dragSourceIndex: -1
        property int dragTargetIndex: -1
        property real autoScrollSpeed: 0
        property var selectedIndices: []
        property var selectedUuids: []
        property int anchorIndex: -1
        property string anchorUuid: ""
        property string currentUuid: ""

        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
            active: true
        }

        moveDisplaced: Transition {
            NumberAnimation { properties: "y"; duration: 120; easing.type: Easing.InOutQuad }
        }
    }

    Text {
        id: emptyStateText
        objectName: "emptyStateText"
        anchors.centerIn: parent
        visible: listView.count === 0
        text: "No mods to show"
        color: Theme.textDim
        font.pixelSize: Theme.fontSizeMd
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }

    Timer {
        id: autoScrollTimer
        interval: 16
        repeat: true
        running: dragProxy.visible && listView.autoScrollSpeed !== 0
        onTriggered: {
            var newContentY = listView.contentY + listView.autoScrollSpeed
            var maxContentY = Math.max(0, listView.contentHeight - listView.height)
            listView.contentY = Math.max(0, Math.min(maxContentY, newContentY))
            var center = dragProxy.mapToItem(
                listView.contentItem,
                dragProxy.width / 2,
                dragProxy.height / 2
            )
            listView.dragTargetIndex = Math.max(
                0,
                Math.min(Math.floor(center.y / 54), listView.count - 1)
            )
        }
    }

    Connections {
        target: modListModel
        function onLayoutChanged() { root.reconcileSelection() }
        function onModelReset() { root.reconcileSelection() }
        function onRowsMoved() { root.reconcileSelection() }
    }

    // ── Drag proxy ──
    Rectangle {
        id: dragProxy
        objectName: "dragProxy"
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

    Rectangle {
        visible: dragProxy.visible && listView.dragTargetIndex >= 0
        x: 0
        y: Math.max(0, Math.min(
            listView.height - height,
            listView.dragTargetIndex * 54 - listView.contentY
        ))
        width: listView.width
        height: 2
        color: Theme.primary
        z: 999
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
                if (mouseArea.containsMouse && !dragProxy.visible)
                    return Theme.elevate3
                return "transparent"
            }
            opacity: listView.dragSourceIndex === index && dragProxy.visible ? 0.0 : 1.0
            Accessible.role: Accessible.ListItem
            Accessible.name: (model.name || "")
                + (model.packageId ? ", " + model.packageId : "")
            Accessible.selected: listView.selectedIndices.indexOf(index) >= 0

            MouseArea {
                objectName: "rowMouseArea"
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: (mouse) => root.selectRow(index, model.uuid, mouse.modifiers)
            }

            Item {
                id: dragHandle
                x: 0
                width: 18
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text: "\u22ee"
                    color: Theme.textDim
                    font.pixelSize: 20
                }

                MouseArea {
                    id: dragArea
                    objectName: "dragArea"
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor

                    property real pressRootX: 0
                    property real pressRootY: 0

                    onPressed: (mouse) => {
                        root.selectRow(index, model.uuid, mouse.modifiers)
                        var point = dragArea.mapToItem(root, mouse.x, mouse.y)
                        pressRootX = point.x
                        pressRootY = point.y

                        var delegatePosition = delegateRect.mapToItem(root, 0, 0)
                        dragProxy.modName = model.name || ""
                        dragProxy.modPackageId = model.packageId || ""
                        dragProxy.providerColor = model.providerColor || ""
                        dragProxy.pressOffsetY = mouse.y
                        dragProxy.x = delegatePosition.x
                        dragProxy.y = delegatePosition.y
                        listView.dragSourceIndex = index
                        listView.dragTargetIndex = index
                    }

                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        var point = dragArea.mapToItem(root, mouse.x, mouse.y)
                        if (!dragProxy.visible) {
                            var distance = Math.abs(point.x - pressRootX)
                                + Math.abs(point.y - pressRootY)
                            if (distance < Qt.styleHints.startDragDistance)
                                return
                            dragProxy.visible = true
                        }

                        dragProxy.y = point.y - dragProxy.pressOffsetY
                        var center = dragProxy.mapToItem(
                            listView.contentItem,
                            dragProxy.width / 2,
                            dragProxy.height / 2
                        )
                        listView.dragTargetIndex = Math.max(
                            0,
                            Math.min(Math.floor(center.y / 54), listView.count - 1)
                        )

                        var edgeThreshold = 40
                        var bottomEdge = listView.height - edgeThreshold
                        if (point.y < edgeThreshold) {
                            var topDistance = edgeThreshold - point.y
                            listView.autoScrollSpeed = -Math.min(
                                15,
                                Math.max(3, topDistance / 2)
                            )
                        } else if (point.y > bottomEdge) {
                            var bottomDistance = point.y - bottomEdge
                            listView.autoScrollSpeed = Math.min(
                                15,
                                Math.max(3, bottomDistance / 2)
                            )
                        } else {
                            listView.autoScrollSpeed = 0
                        }
                    }

                    onReleased: {
                        var sourceIndex = listView.dragSourceIndex
                        var targetIndex = listView.dragTargetIndex
                        var shouldMove = dragProxy.visible && sourceIndex !== targetIndex
                        dragProxy.visible = false
                        listView.autoScrollSpeed = 0
                        listView.dragSourceIndex = -1
                        listView.dragTargetIndex = -1
                        if (shouldMove) {
                            modListPanel.moveRow(sourceIndex, targetIndex)
                            modListPanel.dragEnded()
                        }
                    }

                    onCanceled: {
                        dragProxy.visible = false
                        listView.autoScrollSpeed = 0
                        listView.dragSourceIndex = -1
                        listView.dragTargetIndex = -1
                    }
                }
            }

            Item {
                x: 18
                width: 32
                height: parent.height

                Rectangle {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    radius: 3
                    color: model.checkState === Qt.Checked ? Theme.primary : "transparent"
                    border.color: model.checkState === Qt.Checked ? Theme.primary : Theme.border
                    border.width: 1.5

                    Text {
                        anchors.centerIn: parent
                        visible: model.checkState === Qt.Checked
                        text: "\u2713"
                        color: Theme.elevate0
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }
                }

                MouseArea {
                    objectName: "checkboxArea"
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: (
                        model.checkState === Qt.Checked ? "Disable " : "Enable "
                    ) + (model.name || "mod")
                    Accessible.checked: model.checkState === Qt.Checked
                    onClicked: (mouse) => {
                        root.selectRow(index, model.uuid, mouse.modifiers)
                        modListPanel.toggleCheck(index)
                    }
                    Accessible.onPressAction: {
                        root.selectRow(index, model.uuid, Qt.NoModifier)
                        modListPanel.toggleCheck(index)
                    }
                }
            }

            // ── Avatar ──
            Rectangle {
                x: 50; y: 8; width: 36; height: 36
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
                x: 96; y: 8
                width: parent.width - badgesRow.width - 108
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
                    visible: !!model.hasError
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
                        cursorShape: Qt.WhatsThisCursor
                    }

                    ToolTip {
                        text: model.errorTooltip || ""
                        visible: errorHover.containsMouse
                        delay: 300
                    }
                }

                // Warning badge
                Rectangle {
                    visible: !!model.hasWarning
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
                        cursorShape: Qt.WhatsThisCursor
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
        enabled: root.keyboardActive
        onActivated: navigate(false, -1)
    }
    Shortcut {
        sequence: "Down"
        context: Qt.WindowShortcut
        enabled: root.keyboardActive
        onActivated: navigate(false, 1)
    }
    Shortcut {
        sequence: "Shift+Up"
        context: Qt.WindowShortcut
        enabled: root.keyboardActive
        onActivated: navigate(true, -1)
    }
    Shortcut {
        sequence: "Shift+Down"
        context: Qt.WindowShortcut
        enabled: root.keyboardActive
        onActivated: navigate(true, 1)
    }
    Shortcut {
        sequence: "Return"
        context: Qt.WindowShortcut
        enabled: root.keyboardActive
        onActivated: modListPanel.toggleChecked(listView.selectedIndices)
    }
    Shortcut {
        sequence: "Ctrl+A"
        context: Qt.WindowShortcut
        enabled: root.keyboardActive
        onActivated: {
            var indices = []
            var uuids = []
            for (var i = 0; i < listView.count; ++i) {
                indices.push(i)
                uuids.push(modListPanel.uuidAt(i))
            }
            listView.selectedIndices = indices
            listView.selectedUuids = uuids
            listView.anchorIndex = listView.count - 1
            listView.anchorUuid = uuids.length > 0 ? uuids[uuids.length - 1] : ""
            listView.currentIndex = listView.count - 1
            listView.currentUuid = listView.anchorUuid
            modListPanel.selectionChanged(uuids)
            if (listView.currentUuid)
                modListPanel.modSelected(listView.currentUuid)
        }
    }

    function selectRow(index, uuid, modifiers) {
        listView.forceActiveFocus()
        if (modifiers & Qt.ControlModifier) {
            var indices = listView.selectedIndices.slice()
            var uuids = listView.selectedUuids.slice()
            var selectedPosition = uuids.indexOf(uuid)
            if (selectedPosition >= 0) {
                uuids.splice(selectedPosition, 1)
                indices.splice(selectedPosition, 1)
            } else {
                indices.push(index)
                uuids.push(uuid)
            }
            listView.selectedIndices = indices
            listView.selectedUuids = uuids
            listView.anchorIndex = index
            listView.anchorUuid = uuid
        } else if (modifiers & Qt.ShiftModifier && listView.anchorIndex >= 0) {
            var start = Math.min(listView.anchorIndex, index)
            var end = Math.max(listView.anchorIndex, index)
            var range = []
            var rangeUuids = []
            for (var row = start; row <= end; ++row) {
                range.push(row)
                rangeUuids.push(modListPanel.uuidAt(row))
            }
            listView.selectedIndices = range
            listView.selectedUuids = rangeUuids
        } else {
            listView.selectedIndices = [index]
            listView.selectedUuids = [uuid]
            listView.anchorIndex = index
            listView.anchorUuid = uuid
        }

        listView.currentIndex = index
        listView.currentUuid = uuid
        modListPanel.selectionChanged(listView.selectedUuids)
        modListPanel.modSelected(uuid)
    }

    function navigate(extend, direction) {
        var newIndex = listView.currentIndex + direction
        if (newIndex < 0 || newIndex >= listView.count)
            return

        var uuid = modListPanel.uuidAt(newIndex)
        if (extend) {
            if (listView.anchorIndex < 0) {
                listView.anchorIndex = newIndex
                listView.anchorUuid = uuid
            }
            var start = Math.min(listView.anchorIndex, newIndex)
            var end = Math.max(listView.anchorIndex, newIndex)
            var range = []
            var rangeUuids = []
            for (var row = start; row <= end; ++row) {
                range.push(row)
                rangeUuids.push(modListPanel.uuidAt(row))
            }
            listView.selectedIndices = range
            listView.selectedUuids = rangeUuids
        } else {
            listView.selectedIndices = [newIndex]
            listView.selectedUuids = [uuid]
            listView.anchorIndex = newIndex
            listView.anchorUuid = uuid
        }

        listView.currentIndex = newIndex
        listView.currentUuid = uuid
        listView.positionViewAtIndex(newIndex, ListView.Contain)
        modListPanel.selectionChanged(listView.selectedUuids)
        modListPanel.modSelected(uuid)
    }

    function reconcileSelection() {
        var indices = []
        var uuids = []
        for (var i = 0; i < listView.selectedUuids.length; ++i) {
            var uuid = listView.selectedUuids[i]
            var row = modListPanel.rowForUuid(uuid)
            if (row >= 0) {
                indices.push(row)
                uuids.push(uuid)
            }
        }
        listView.selectedIndices = indices
        listView.selectedUuids = uuids

        var currentRow = listView.currentUuid ? modListPanel.rowForUuid(listView.currentUuid) : -1
        if (currentRow < 0 && indices.length > 0) {
            currentRow = indices[0]
            listView.currentUuid = uuids[0]
            modListPanel.modSelected(listView.currentUuid)
        } else if (currentRow < 0) {
            listView.currentIndex = -1
            listView.currentUuid = ""
            listView.anchorIndex = -1
            listView.anchorUuid = ""
            modListPanel.selectionChanged([])
            modListPanel.modSelected("")
            return
        }
        listView.currentIndex = currentRow

        var anchorRow = listView.anchorUuid ? modListPanel.rowForUuid(listView.anchorUuid) : -1
        if (anchorRow < 0) {
            anchorRow = currentRow
            listView.anchorUuid = listView.currentUuid
        }
        listView.anchorIndex = anchorRow
        modListPanel.selectionChanged(uuids)
    }
}
