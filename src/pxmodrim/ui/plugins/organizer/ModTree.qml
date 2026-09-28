import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

Rectangle {
    id: root
    color: Theme.elevate1
    clip: true

    property int anchorRow: -1
    property int focusedRow: -1
    property int dropRow: -1
    property var dropTarget: null

    function openTags() { tagEditor.open() }

    function expandAll() {
        tree.expandRecursively()
        organizerPanel.persistAllCollapsed(false)
    }

    function collapseAll() {
        tree.collapseRecursively()
        root.anchorRow = -1
        root.focusedRow = -1
        organizerPanel.persistAllCollapsed(true)
    }

    function restoreExpansion() {
        var indexes = organizerPanel.expandedIndexes()
        for (var i = 0; i < indexes.length; ++i) {
            var row = tree.rowAtIndex(indexes[i])
            if (row >= 0 && !tree.isExpanded(row))
                tree.expand(row)
        }
    }

    function clickRow(index, row, modifiers) {
        var range = []
        if ((modifiers & Qt.ShiftModifier) && root.anchorRow >= 0) {
            var from = Math.min(root.anchorRow, row)
            var to = Math.max(root.anchorRow, row)
            for (var r = from; r <= to; ++r)
                range.push(tree.index(r, 0))
        } else {
            root.anchorRow = row
        }
        root.focusedRow = row
        organizerPanel.selectRow(index, modifiers, range)
        tree.forceActiveFocus()
    }

    function toggleRow(index, row) {
        if (tree.isExpanded(row)) {
            tree.collapse(row)
            organizerPanel.persistCollapsed(index, true)
        } else {
            tree.expand(row)
            organizerPanel.persistCollapsed(index, false)
            root.restoreExpansion()
        }
    }

    function navigateRow(row, modifiers) {
        if (row < 0 || row >= tree.rows)
            return
        clickRow(tree.index(row, 0), row, modifiers)
        tree.positionViewAtRow(row, TableView.Contain)
    }

    function activeRow() {
        if (root.focusedRow >= 0 && root.focusedRow < tree.rows)
            return root.focusedRow
        return -1
    }

    property int modelRevision: 0
    readonly property bool noModsInstalled: {
        modelRevision
        if (tree.rows !== 1)
            return false
        var idx = tree.index(0, 0)
        return modTreeModel.data(idx, Qt.UserRole + 1) === "ungrouped"
            && modTreeModel.data(idx, Qt.UserRole + 7) === 0
    }

    function selectAll() {
        if (tree.rows === 0)
            return
        var all = []
        for (var r = 0; r < tree.rows; ++r)
            all.push(tree.index(r, 0))
        organizerPanel.selectRow(all[0], Qt.NoModifier, [])
        organizerPanel.selectRow(all[all.length - 1], Qt.ShiftModifier, all)
        root.anchorRow = 0
        root.focusedRow = tree.rows - 1
    }

    Connections {
        target: modTreeModel
        function onDataChanged() { root.modelRevision++ }
        function onRowsInserted() { root.modelRevision++ }
        function onRowsRemoved() { root.modelRevision++ }
        function onModelReset() {
            root.modelRevision++
            root.anchorRow = -1
            Qt.callLater(root.restoreExpansion)
            root.focusedRow = -1
        }
    }

    TreeView {
        id: tree
        objectName: "organizerTreeView"
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        clip: true
        model: modTreeModel
        columnWidthProvider: function() { return tree.width }
        onWidthChanged: forceLayout()
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: false
        focus: true
        ScrollBar.vertical: PxScrollBar { policy: ScrollBar.AsNeeded }
        Keys.onPressed: function(event) {
            var row = root.activeRow()
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                root.navigateRow(row < 0 ? 0 : row + (event.key === Qt.Key_Down ? 1 : -1), event.modifiers)
                event.accepted = true
            } else if (event.key === Qt.Key_Right && row >= 0) {
                if (!tree.isExpanded(row))
                    root.toggleRow(tree.index(row, 0), row)
                event.accepted = true
            } else if (event.key === Qt.Key_Left && row >= 0) {
                if (tree.isExpanded(row)) {
                    root.toggleRow(tree.index(row, 0), row)
                } else {
                    var depth = tree.depth(row)
                    for (var parentRow = row - 1; parentRow >= 0; --parentRow) {
                        if (tree.depth(parentRow) < depth) {
                            root.navigateRow(parentRow, Qt.NoModifier)
                            break
                        }
                    }
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Space && row >= 0) {
                organizerPanel.toggleCheck(tree.index(row, 0))
                event.accepted = true
            } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && row >= 0) {
                var enterIndex = tree.index(row, 0)
                if (tree.hasChildren(row))
                    root.toggleRow(enterIndex, row)
                else
                    organizerPanel.toggleCheck(enterIndex)
                event.accepted = true
            } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                root.selectAll()
                event.accepted = true
            }
        }

        delegate: Rectangle {
            id: item
            required property int row
            required property int column
            required property int depth
            required property bool expanded
            required property bool hasChildren
            required property var model

            implicitWidth: tree.width
            implicitHeight: item.model.kind === "mod" ? 30 : 32
            radius: Theme.radiusSm
            color: root.dropRow === item.row
                ? Theme.primaryBg
                : (item.model.selected ? Theme.primaryBg
                : (rowMouse.containsMouse ? Theme.elevate3 : "transparent"))
            border.width: root.dropRow === item.row ? 1 : 0
            border.color: Theme.primary
            Accessible.role: Accessible.TreeItem
            Accessible.name: item.model.name || ""
            Accessible.selected: !!item.model.selected

            Repeater {
                model: item.depth
                delegate: Rectangle {
                    required property int index
                    x: 12 + index * 20
                    width: 1
                    height: item.height
                    color: Theme.border
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4 + item.depth * 20
                anchors.rightMargin: 10
                spacing: 7
                Item {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 28
                    Image {
                        anchors.centerIn: parent
                        visible: item.hasChildren
                        source: "image://icons/chevron?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 13
                        sourceSize.height: 13
                        rotation: item.expanded ? 90 : 0
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: item.hasChildren
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleRow(tree.index(item.row, 0), item.row)
                    }
                }

                Item {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 28
                    PxCheckIndicator {
                        anchors.centerIn: parent
                        checkState: item.model.checkState
                        hovered: checkMouse.containsMouse
                    }
                    MouseArea {
                        id: checkMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: (item.model.checkState === Qt.Checked ? "Disable " : "Enable ") + item.model.name
                        Accessible.checked: item.model.checkState === Qt.Checked
                        onClicked: organizerPanel.toggleCheck(tree.index(item.row, 0))
                        Accessible.onPressAction: organizerPanel.toggleCheck(tree.index(item.row, 0))
                    }
                }

                Image {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: "image://icons/"
                            + (item.model.kind === "mod" ? (item.model.providerIcon || "folder") : "folder")
                            + "?color=" + encodeURIComponent(
                                item.model.kind === "mod" ? (item.model.providerColor || Theme.textDim)
                                : item.model.kind === "ungrouped" ? Theme.textDim : Theme.primary)
                    sourceSize.width: 16
                    sourceSize.height: 16
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    text: item.model.name || ""
                    Layout.fillWidth: true
                    color: item.model.kind === "ungrouped" || (item.model.kind === "mod" && item.model.checkState !== Qt.Checked)
                           ? Theme.textMuted : Theme.textMain
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeMd
                    font.bold: item.model.kind !== "mod"
                    font.italic: item.model.kind === "ungrouped"
                    elide: Text.ElideRight
                }

                PxBadge {
                    visible: !!item.model.hasRule
                    text: "Auto"
                    textColor: Theme.warning
                    fillColor: Theme.warningBg
                }

                PxBadge {
                    visible: item.model.kind === "mod" && !!item.model.providerLabel
                    outlined: true
                    text: item.model.providerLabel || ""
                    accent: item.model.providerColor || Theme.border
                    textColor: item.model.providerColor || Theme.textMuted
                }

                PxBadge {
                    visible: item.model.kind === "mod" && !!item.model.modVersion
                    text: item.model.modVersion || ""
                }

                Repeater {
                    model: item.model.kind === "mod" ? (item.model.tags || []) : []
                    delegate: PxBadge {
                        required property var modelData
                        property color tagColor: modelData.color
                        text: modelData.name
                        textColor: tagColor
                        fillColor: Qt.rgba(tagColor.r, tagColor.g, tagColor.b, 0.14)
                    }
                }
                Text {
                    visible: item.model.kind === "mod" && !!item.model.packageId
                    text: item.model.packageId || ""
                    Layout.maximumWidth: Math.max(80, item.width * 0.30)
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSm
                    font.family: Theme.fontMono
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                }

                DiagnosticBadge {
                    visible: !!item.model.hasError
                    level: "error"
                    tooltip: item.model.errorTooltip || ""
                }

                DiagnosticBadge {
                    visible: !!item.model.hasWarning
                    level: "warning"
                    tooltip: item.model.warningTooltip || ""
                }

                PxBadge {
                    visible: item.model.kind !== "mod"
                    textColor: Theme.textMuted
                    fillColor: Theme.elevate3
                    text: item.model.filtering
                          ? item.model.visibleCount + " of " + item.model.totalCount
                          : item.model.enabledCount + "/" + item.model.totalCount
                }
            }

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                preventStealing: true
                property real pressX: 0
                property real pressY: 0
                property bool dragging: false
                property bool pendingSingle: false
                onPressed: function(mouse) {
                    pressX = mouse.x
                    pressY = mouse.y
                    dragging = false
                    pendingSingle = false
                    if (mouse.button === Qt.RightButton) {
                        organizerPanel.contextMenu(tree.index(item.row, 0))
                        return
                    }
                    if (mouse.modifiers === Qt.NoModifier && item.model.selected)
                        pendingSingle = true
                    else
                        root.clickRow(tree.index(item.row, 0), item.row, mouse.modifiers)
                }
                onPositionChanged: function(mouse) {
                    if (!(pressedButtons & Qt.LeftButton))
                        return
                    if (!dragging && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < Qt.styleHints.startDragDistance)
                        return
                    dragging = true
                    var pos = rowMouse.mapToItem(tree.contentItem, mouse.x, mouse.y)
                    var cell = tree.cellAtPosition(Qt.point(pos.x, pos.y))
                    var target = cell.y >= 0 ? tree.index(cell.y, 0) : null
                    root.dropTarget = target !== null && organizerPanel.canDrop(tree.index(item.row, 0), target) ? target : null
                    root.dropRow = root.dropTarget !== null ? cell.y : -1
                }
                onReleased: function(mouse) {
                    if (dragging && root.dropTarget !== null)
                        organizerPanel.dropOn(tree.index(item.row, 0), root.dropTarget)
                    else if (!dragging && pendingSingle)
                        root.clickRow(tree.index(item.row, 0), item.row, Qt.NoModifier)
                    dragging = false
                    pendingSingle = false
                    root.dropTarget = null
                    root.dropRow = -1
                }
                onCanceled: {
                    dragging = false
                    pendingSingle = false
                    root.dropTarget = null
                    root.dropRow = -1
                }
                onDoubleClicked: {
                    if (item.model.kind !== "mod")
                        root.toggleRow(tree.index(item.row, 0), item.row)
                }
            }
        }
    }

    TagEditor {
        id: tagEditor
        tagRows: organizerPanel ? organizerPanel.tagRows : []
        selectedCount: organizerPanel ? organizerPanel.selectedTagCount : 0
        errorMessage: organizerPanel ? organizerPanel.editorError : ""
        onCreateRequested: (name, color) => organizerPanel.createTag(name, color)
        onUpdateRequested: (id, name, color) => organizerPanel.updateTag(id, name, color)
        onDeleteRequested: id => organizerPanel.deleteTag(id)
        onAssignmentRequested: (id, add) => organizerPanel.assignTag(id, add)
    }


    Connections {
        target: organizerPanel
        function onTagCreated() { tagEditor.clearInputs() }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width - 48
        visible: organizerPanel && (!organizerPanel.ready || tree.rows === 0 || root.noModsInstalled)
        iconName: organizerPanel && organizerPanel.ready ? "empty" : ""
        title: !organizerPanel || !organizerPanel.ready ? "Loading organizer…"
               : root.noModsInstalled ? "No mods installed"
               : "No mods match this filter"
        detail: organizerPanel && organizerPanel.ready && !root.noModsInstalled
                ? "Try a different filter or search." : ""
    }
}
