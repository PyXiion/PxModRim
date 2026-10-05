import QtQuick
import QtQuick.Controls
import "../components/controls"

Rectangle {
    id: root
    color: Theme.elevate1
    clip: true

    property bool keyboardActive: modListHasFocus && listView.activeFocus
    readonly property bool compactMode: Boolean(modListPanel && modListPanel.compactMode)
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

        ScrollBar.vertical: PxScrollBar {
            id: scrollBar
            policy: ScrollBar.AsNeeded
            active: true
        }
        section.property: "sectionName"
        section.criteria: ViewSection.FullString
        section.delegate: Component {
            Item {
                width: listView.width
                height: section !== "" ? 28 : 0
                visible: section !== ""

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Text {
                        id: sectionLabel
                        text: section
                        color: Theme.textDim
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Bold
                        font.letterSpacing: 0.5
                        font.capitalization: Font.AllUppercase
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - sectionLabel.width - parent.spacing
                        height: 1
                        color: Theme.border
                    }
                }
            }
        }

        moveDisplaced: Transition {
            NumberAnimation { properties: "y"; duration: 120; easing.type: Easing.InOutQuad }
        }
    }

    EmptyState {
        id: emptyStateText
        objectName: "emptyStateText"
        anchors.fill: parent
        visible: listView.count === 0
        iconName: "mods"
        title: "No mods to show"
        Accessible.name: title
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
            listView.dragTargetIndex = root.targetIndexForPoint(center.x, center.y)
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
        height: root.compactMode ? 32 : 52
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
                font.family: Theme.fontFamily
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
                font.family: Theme.fontFamily
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
                font.family: Theme.fontMono
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    Rectangle {
        visible: dragProxy.visible && listView.dragTargetIndex >= 0 && (modListModel ? listView.dragTargetIndex < modListModel.activeCount : true)
        x: 0
        y: {
            var targetItem = listView.itemAtIndex(listView.dragTargetIndex)
            if (targetItem) {
                return targetItem.y - listView.contentY
            }
            var rowH = root.compactMode ? 34 : 54
            return Math.max(0, Math.min(
                listView.height - height,
                listView.dragTargetIndex * rowH - listView.contentY
            ))
        }
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
            height: root.compactMode ? 32 : 52
            radius: Theme.radiusMd

            color: {
                var highlighted = modListPanel
                    && modListPanel.highlightRevision >= 0
                    && modListPanel.isHighlighted(model.uuid)
                if (listView.selectedIndices.indexOf(index) >= 0)
                    return highlighted
                        ? Qt.tint(Theme.elevate4, Qt.alpha(Theme.primary, 0.25))
                        : Theme.elevate4
                if (highlighted)
                    return Qt.alpha(Theme.primary, 0.25)
                if (mouseArea.containsMouse && !dragProxy.visible)
                    return Theme.elevate3
                return "transparent"
            }
            opacity: listView.dragSourceIndex === index && dragProxy.visible ? 0.0 : 1.0
            Accessible.role: Accessible.ListItem
            Accessible.name: (model.loadIndex > 0 ? "#" + model.loadIndex + ", " : "")
                + (model.name || "")
                + (model.packageId ? ", " + model.packageId : "")
            Accessible.selected: listView.selectedIndices.indexOf(index) >= 0

            MouseArea {
                objectName: "rowMouseArea"
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        if (listView.selectedUuids.indexOf(model.uuid) < 0)
                            root.selectRow(index, model.uuid, Qt.NoModifier)
                        modListPanel.showContextMenu(listView.selectedUuids)
                        return
                    }
                    root.selectRow(index, model.uuid, mouse.modifiers)
                }
            }

            Item {
                id: dragHandle
                x: 0
                width: 24
                height: parent.height
                visible: model.loadIndex > 0

                Image {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    sourceSize.width: 16
                    sourceSize.height: 16
                    opacity: (modListModel && modListModel.isFiltered) ? Theme.disabledOpacity : 1.0
                    source: "image://icons/grip?color=" + encodeURIComponent(
                        (modListModel && modListModel.isFiltered) ? Theme.textDim : Theme.textMuted)
                }

                Loader {
                    active: Boolean(modListModel && modListModel.isFiltered)
                    sourceComponent: PxToolTip {
                        text: "Clear search and filters to reorder"
                        visible: dragArea.containsMouse
                    }
                }

                MouseArea {
                    id: dragArea
                    objectName: "dragArea"
                    anchors.fill: parent
                    hoverEnabled: true
                    preventStealing: true
                    cursorShape: (modListModel && modListModel.isFiltered)
                        ? Qt.ArrowCursor
                        : (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)

                    property real pressRootX: 0
                    property real pressRootY: 0

                    onPressed: (mouse) => {
                        root.selectRow(index, model.uuid, mouse.modifiers)
                        if (modListModel && modListModel.isFiltered)
                            return
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
                        listView.dragTargetIndex = root.targetIndexForPoint(center.x, center.y)

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
                id: loadIndexItem
                x: 24
                width: 32
                height: parent.height

                Text {
                    anchors.centerIn: parent
                    text: model.loadIndex > 0 ? ("#" + model.loadIndex) : "–"
                    color: model.loadIndex > 0 ? Theme.textMuted : Theme.textDim
                    font.pixelSize: Theme.fontSizeSm
                    font.family: Theme.fontMono
                    font.weight: model.loadIndex > 0 ? Font.Medium : Font.Normal
                }
            }

            Item {
                x: 56
                width: 32
                height: parent.height

                PxCheckIndicator {
                    anchors.centerIn: parent
                    checkState: model.checkState
                    hovered: checkboxArea.containsMouse
                }

                MouseArea {
                    id: checkboxArea
                    objectName: "checkboxArea"
                    anchors.fill: parent
                    hoverEnabled: true
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

            // ── Avatar (hidden in compact mode) ──
            Rectangle {
                visible: !root.compactMode
                x: 88; y: 8; width: 36; height: 36
                radius: Theme.radiusSm
                color: model.providerColor || Theme.elevate3

                Text {
                    anchors.centerIn: parent
                    text: model.name ? model.name.charAt(0).toUpperCase() : "?"
                    color: Theme.elevate0
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeLg
                    font.weight: Font.Bold
                }
            }

            // ── Standard 2-line layout (name + packageId) ──
            Column {
                visible: !root.compactMode
                x: 132; y: 8
                width: parent.width - badgesRow.width - 142
                spacing: 1

                Text {
                    width: parent.width
                    text: model.name || ""
                    color: Theme.textMain
                    font.family: Theme.fontFamily
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
                    font.family: Theme.fontMono
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }
            }

            // ── Compact single-line layout ──
            Row {
                visible: root.compactMode
                x: 92
                width: parent.width - badgesRow.width - 102
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                clip: true

                Text {
                    id: compactNameText
                    text: model.name || ""
                    color: Theme.textMain
                    font.family: Theme.fontFamily
                    font.bold: true
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, parent.width - (compactSubText.visible ? Math.min(compactSubText.implicitWidth + 6, parent.width * 0.45) : 0))
                }

                Text {
                    id: compactSubText
                    text: (model.modVersion ? model.modVersion + " " : "") + (model.packageId ? "(" + model.packageId + ")" : "")
                    visible: text !== "" && (parent.width - compactNameText.width - 6) >= 40
                    width: Math.min(implicitWidth, Math.max(0, parent.width - compactNameText.width - 6))
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontMono
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    anchors.verticalCenter: parent.verticalCenter
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
                Loader {
                    id: startupImpactLoader
                    active: model.startupImpact > 0
                    visible: active
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: PxBadge {
                        compact: root.compactMode
                        text: model.startupImpactText
                        textColor: Theme.onAccent
                        fillColor: model.startupImpactColor
                        tooltip: "Startup impact: adds " + model.startupImpactText + " to game load"
                    }
                }

                PxBadge {
                    visible: !!model.providerLabel
                    anchors.verticalCenter: parent.verticalCenter
                    compact: root.compactMode
                    outlined: true
                    text: model.providerLabel || ""
                    accent: model.providerColor || Theme.border
                    textColor: model.providerColor || Theme.textMuted
                }

                PxBadge {
                    visible: !!model.modVersion
                    anchors.verticalCenter: parent.verticalCenter
                    compact: root.compactMode
                    text: model.modVersion || ""
                }

                // Error badge
                Loader {
                    active: !!model.hasError
                    visible: active
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: DiagnosticBadge {
                        level: "error"
                        tooltip: model.errorTooltip || ""
                    }
                }

                // Warning badge
                Loader {
                    active: !!model.hasWarning
                    visible: active
                    anchors.verticalCenter: parent.verticalCenter
                    sourceComponent: DiagnosticBadge {
                        level: "warning"
                        tooltip: model.warningTooltip || ""
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
        sequence: "Space"
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

    function revealRow(index, uuid) {
        selectRow(index, uuid, 0)
        listView.positionViewAtIndex(index, ListView.Center)
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

    function targetIndexForPoint(x, y) {
        if (listView.count === 0 || !modListModel || modListModel.activeCount <= 0)
            return -1
        var maxActiveTarget = modListModel.activeCount - 1
        var idx = listView.indexAt(x, y)
        if (idx >= 0)
            return Math.max(0, Math.min(idx, maxActiveTarget))
        if (y <= 0)
            return 0
        if (y >= listView.contentHeight)
            return maxActiveTarget
        idx = listView.indexAt(x, y + 16)
        if (idx >= 0)
            return Math.max(0, Math.min(idx, maxActiveTarget))
        idx = listView.indexAt(x, y - 16)
        if (idx >= 0)
            return Math.max(0, Math.min(idx, maxActiveTarget))
        var rowH = root.compactMode ? 34 : 54
        return Math.max(0, Math.min(Math.floor(y / rowH), maxActiveTarget))
    }
}
