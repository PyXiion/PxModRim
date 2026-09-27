import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: Theme.elevate1
    clip: true

    property int anchorRow: -1
    property int dropRow: -1
    property var dropTarget: null

    function openTags() { tagEditor.open() }
    function openRules() { ruleEditor.open() }

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

    Connections {
        target: modTreeModel
        function onModelReset() {
            root.anchorRow = -1
            Qt.callLater(root.restoreExpansion)
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
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Rectangle {
            id: item
            required property int row
            required property int column
            required property int depth
            required property bool expanded
            required property bool hasChildren
            required property var model

            implicitWidth: tree.width
            implicitHeight: 42
            radius: Theme.radiusMd
            color: root.dropRow === item.row
                ? Theme.primaryBg
                : (item.model.selected ? Theme.elevate4
                : (rowMouse.containsMouse ? Theme.elevate3 : "transparent"))
            border.width: root.dropRow === item.row ? 1 : 0
            border.color: Theme.primary
            Accessible.role: Accessible.TreeItem
            Accessible.name: item.model.name || ""
            Accessible.selected: !!item.model.selected

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8 + item.depth * 22
                anchors.rightMargin: 14
                spacing: 8

                Item {
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 30
                    Image {
                        anchors.centerIn: parent
                        visible: item.hasChildren
                        source: "image://icons/chevron?color=" + encodeURIComponent(Theme.textDim)
                        sourceSize.width: 14
                        sourceSize.height: 14
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
                    Layout.preferredHeight: 32
                    Rectangle {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        radius: 3
                        color: item.model.checkState === Qt.Checked ? Theme.primary : "transparent"
                        border.color: item.model.checkState === Qt.Unchecked ? Theme.border : Theme.primary
                        border.width: 1.5
                        Text {
                            anchors.centerIn: parent
                            visible: item.model.checkState !== Qt.Unchecked
                            text: item.model.checkState === Qt.Checked ? "\u2713" : "−"
                            color: Theme.elevate0
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: (item.model.checkState === Qt.Checked ? "Disable " : "Enable ") + item.model.name
                        Accessible.checked: item.model.checkState === Qt.Checked
                        onClicked: organizerPanel.toggleCheck(tree.index(item.row, 0))
                        Accessible.onPressAction: organizerPanel.toggleCheck(tree.index(item.row, 0))
                    }
                }

                Image {
                    Layout.preferredWidth: 17
                    Layout.preferredHeight: 17
                    source: "image://icons/"
                            + (item.model.kind === "mod" ? (item.model.providerIcon || "folder") : "folder")
                            + "?color=" + encodeURIComponent(
                                item.model.kind === "mod" ? (item.model.providerColor || Theme.textDim) : Theme.textMuted)
                    sourceSize.width: 17
                    sourceSize.height: 17
                    fillMode: Image.PreserveAspectFit
                }

                Text {
                    text: item.model.name || ""
                    Layout.fillWidth: true
                    color: item.model.kind === "ungrouped" ? Theme.textMuted : Theme.textMain
                    font.pixelSize: Theme.fontSizeMd
                    font.bold: item.model.kind !== "mod"
                    elide: Text.ElideRight
                }

                Rectangle {
                    visible: !!item.model.hasRule
                    Layout.preferredWidth: ruleText.implicitWidth + 12
                    Layout.preferredHeight: 19
                    radius: Theme.radiusSm
                    color: Theme.primaryBg
                    Text {
                        id: ruleText
                        anchors.centerIn: parent
                        text: "rule"
                        color: Theme.primary
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                Text {
                    visible: item.model.kind === "mod" && !!item.model.packageId
                    text: item.model.packageId || ""
                    Layout.maximumWidth: Math.max(80, item.width * 0.30)
                    color: Theme.textDim
                    font.pixelSize: Theme.fontSizeSm
                    font.family: "monospace"
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                }

                Image {
                    visible: !!item.model.hasError
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: "image://icons/error?color=" + encodeURIComponent(Theme.danger)
                    sourceSize.width: 16
                    sourceSize.height: 16
                    ToolTip.visible: errorHover.containsMouse
                    ToolTip.text: item.model.errorTooltip || ""
                    MouseArea { id: errorHover; anchors.fill: parent; hoverEnabled: true }
                }

                Image {
                    visible: !!item.model.hasWarning
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    source: "image://icons/warning?color=" + encodeURIComponent(Theme.warning)
                    sourceSize.width: 16
                    sourceSize.height: 16
                    ToolTip.visible: warningHover.containsMouse
                    ToolTip.text: item.model.warningTooltip || ""
                    MouseArea { id: warningHover; anchors.fill: parent; hoverEnabled: true }
                }

                Rectangle {
                    visible: item.model.kind !== "mod"
                    Layout.preferredWidth: countText.implicitWidth + 14
                    Layout.preferredHeight: 21
                    radius: Theme.radiusSm
                    color: Theme.elevate3
                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: item.model.filtering
                              ? item.model.visibleCount + " of " + item.model.totalCount
                              : item.model.enabledCount + "/" + item.model.totalCount
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeXs
                    }
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

    RuleEditor {
        id: ruleEditor
        ruleRows: organizerPanel ? organizerPanel.ruleRows : []
        ruleFolders: organizerPanel ? organizerPanel.ruleFolders : []
        errorMessage: organizerPanel ? organizerPanel.editorError : ""
        onSaveRequested: rules => organizerPanel.saveRules(rules)
        onStandardRulesRequested: organizerPanel.addStandardRules()
    }

    Connections {
        target: organizerPanel
        function onTagCreated() { tagEditor.clearInputs() }
    }

    Text {
        anchors.centerIn: parent
        visible: organizerPanel && (!organizerPanel.ready || tree.rows === 0)
        text: organizerPanel && organizerPanel.ready ? "No mods to show" : "Loading organizer..."
        color: Theme.textDim
        font.pixelSize: Theme.fontSizeMd
    }
}
