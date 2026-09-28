import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

PxDialog {
    id: root
    objectName: "organizerRuleEditor"
    width: Math.min(900, parent ? parent.width - 24 : 760)
    height: Math.min(660, parent ? parent.height - 24 : 540)
    title: "Auto-Folder Rules"

    property var ruleRows: []
    property var ruleFolders: []
    property string errorMessage: ""

    signal saveRequested(var rules)
    signal standardRulesRequested()

    readonly property var fieldOptions: [
        { text: "Package ID", value: "package_id" },
        { text: "Name", value: "name" },
        { text: "Author", value: "author" }
    ]

    readonly property var opOptions: [
        { text: "Prefix", value: "prefix" },
        { text: "Contains", value: "contains" },
        { text: "Equals", value: "equals" }
    ]

    readonly property bool hasFolders: ruleFolders.length > 0
    property var preview: ({ counts: [], assignable: 0 })
    property int previewRevision: 0
    property bool previewPending: false

    function schedulePreview() {
        if (!visible || loadingDraft || !organizerPanel || !organizerPanel.visible || !organizerPanel.ready)
            return
        previewRevision++
        previewPending = true
        previewTimer.restart()
    }

    Timer {
        id: previewTimer
        interval: 100
        onTriggered: {
            if (root.visible && organizerPanel && organizerPanel.visible && organizerPanel.ready)
                organizerPanel.requestRulePreview(root.draftRules(), root.previewRevision)
        }
    }

    property bool loadingDraft: false
    property int draftRevision: 0
    property var loadedRows: []
    readonly property bool draftDirty: {
        draftRevision
        return !draftMatches(ruleRows)
    }

    function draftMatches(rows) {
        if (draftModel.count !== rows.length)
            return false
        for (var i = 0; i < rows.length; ++i) {
            var d = draftModel.get(i)
            var r = rows[i]
            if (d.field !== r.field || d.op !== r.op || d.pattern !== r.pattern
                    || d.folder_id !== r.folder_id)
                return false
        }
        return true
    }

    function findFieldIndex(val) {
        for (var i = 0; i < fieldOptions.length; ++i) {
            if (fieldOptions[i].value === val)
                return i
        }
        return 0
    }

    function findOpIndex(val) {
        for (var i = 0; i < opOptions.length; ++i) {
            if (opOptions[i].value === val)
                return i
        }
        return 0
    }

    function findFolderIndex(fid) {
        for (var i = 0; i < ruleFolders.length; ++i) {
            if (ruleFolders[i].id === fid)
                return i
        }
        return 0
    }

    ListModel {
        id: draftModel
        onDataChanged: { if (!root.loadingDraft) root.draftRevision++ }
        onRowsInserted: { if (!root.loadingDraft) root.draftRevision++ }
        onRowsRemoved: { if (!root.loadingDraft) root.draftRevision++ }
        onRowsMoved: { if (!root.loadingDraft) root.draftRevision++ }
        onModelReset: { if (!root.loadingDraft) root.draftRevision++ }
    }

    function loadDraft() {
        loadingDraft = true
        draftModel.clear()
        for (var i = 0; i < ruleRows.length; ++i) {
            var r = ruleRows[i]
            draftModel.append({
                field: r.field,
                op: r.op,
                pattern: r.pattern,
                folder_id: r.folder_id
            })
        }
        loadedRows = ruleRows
        loadingDraft = false
        draftRevision++
    }

    onRuleRowsChanged: {
        if (!visible && !draftMatches(ruleRows))
            loadDraft()
        else if (visible && draftMatches(loadedRows) && !draftMatches(ruleRows))
            loadDraft()
        schedulePreview()
    }
    onDraftRevisionChanged: schedulePreview()

    function addRule() {
        if (!hasFolders)
            return
        draftModel.append({
            field: "package_id",
            op: "prefix",
            pattern: "",
            folder_id: ruleFolders[0].id
        })
    }

    function removeRule(index) {
        if (index >= 0 && index < draftModel.count)
            draftModel.remove(index)
    }

    function moveUp(index) {
        if (index > 0 && index < draftModel.count)
            draftModel.move(index, index - 1, 1)
    }

    function moveDown(index) {
        if (index >= 0 && index < draftModel.count - 1)
            draftModel.move(index, index + 1, 1)
    }

    function draftRules() {
        var rules = []
        for (var i = 0; i < draftModel.count; ++i) {
            var row = draftModel.get(i)
            rules.push({
                field: String(row.field),
                op: String(row.op),
                pattern: String(row.pattern),
                folder_id: parseInt(row.folder_id, 10)
            })
        }
        return rules
    }

    function submitRules() {
        root.saveRequested(draftRules())
    }


    Connections {
        target: organizerPanel
        function onRulesSaved() {
            root.close()
        }
        function onRulePreviewReady(revision, result) {
            if (root.visible && revision === root.previewRevision) {
                root.preview = result
                root.previewPending = false
            }
        }
    }

    onAboutToShow: {
        if (!draftMatches(ruleRows))
            loadDraft()
    }
    onOpened: schedulePreview()
    onClosed: {
        previewTimer.stop()
        previewRevision++
        previewPending = false
    }
    onRejected: root.close()

    contentItem: ColumnLayout {
        anchors.margins: 12
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "Evaluated top to bottom: first match wins. Rules choose folders only for mods without a manual placement. Moving a mod manually always takes priority; saving updates rule-based folders immediately."
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            PxButton {
                objectName: "organizerAddRule"
                text: "Add Rule"
                variant: "secondary"
                enabled: root.hasFolders
                onClicked: root.addRule()
            }

            PxButton {
                objectName: "organizerAddStandardRules"
                text: "Add standard rules"
                variant: "secondary"
                enabled: !root.draftDirty
                ToolTip.text: "Add the built-in set of common folders and rules"
                onClicked: root.standardRulesRequested()
            }

            Text {
                visible: !root.hasFolders
                text: "Create a folder first."
                color: Theme.warning
                font.pixelSize: Theme.fontSizeSm
            }

            Item { Layout.fillWidth: true }

            Text {
                text: draftModel.count === 1 ? "1 rule" : draftModel.count + " rules"
                color: Theme.textDim
                font.pixelSize: Theme.fontSizeSm
            }
        }

        Rectangle {
            id: errorBanner
            Layout.fillWidth: true
            visible: root.errorMessage !== ""
            color: Theme.dangerBg
            border.color: Theme.danger
            border.width: 1
            radius: Theme.radiusSm
            implicitHeight: errorText.implicitHeight + 14

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                Text {
                    id: errorText
                    Layout.fillWidth: true
                    text: root.errorMessage
                    color: Theme.danger
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.Wrap
                }
            }
        }

        ScrollView {
            id: scrollArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            contentHeight: rulesColumn.implicitHeight

            ColumnLayout {
                id: rulesColumn
                width: scrollArea.availableWidth
                spacing: 6

                Repeater {
                    objectName: "organizerRuleRepeater"
                    model: draftModel

                    delegate: Rectangle {
                        id: rowItem
                        objectName: "organizerRuleRow"
                        required property int index
                        required property string field
                        required property string op
                        required property string pattern
                        required property int folder_id

                        Layout.fillWidth: true
                        implicitHeight: rowCol.implicitHeight + 12
                        color: Theme.elevate1
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm

                        ColumnLayout {
                            id: rowCol
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 5

                                Text {
                                    text: String(rowItem.index + 1)
                                    color: Theme.textDim
                                    font.pixelSize: Theme.fontSizeSm
                                    Layout.preferredWidth: 16
                                    horizontalAlignment: Text.AlignHCenter
                                }

                                PxComboBox {
                                    objectName: "organizerRuleField"
                                    Layout.preferredWidth: 76
                                    model: root.fieldOptions
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: root.findFieldIndex(rowItem.field)
                                    onActivated: draftModel.setProperty(rowItem.index, "field", currentValue)
                                }

                                PxComboBox {
                                    objectName: "organizerRuleOperator"
                                    Layout.preferredWidth: 72
                                    model: root.opOptions
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: root.findOpIndex(rowItem.op)
                                    onActivated: draftModel.setProperty(rowItem.index, "op", currentValue)
                                }

                                PxTextField {
                                    objectName: "organizerRulePattern"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 60
                                    text: rowItem.pattern
                                    placeholderText: "Pattern..."
                                    onTextEdited: draftModel.setProperty(rowItem.index, "pattern", text)
                                }

                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 24
                                spacing: 5
                                Text {
                                    text: "→"
                                    color: Theme.textMuted
                                }

                                PxComboBox {
                                    objectName: "organizerRuleFolder"
                                    Layout.preferredWidth: 120
                                    Layout.minimumWidth: 90
                                    model: root.ruleFolders
                                    textRole: "name"
                                    valueRole: "id"
                                    currentIndex: root.findFolderIndex(rowItem.folder_id)
                                    onActivated: draftModel.setProperty(rowItem.index, "folder_id", currentValue)
                                }

                                PxButton {
                                    objectName: "organizerRuleUp"
                                    variant: "ghost"
                                    iconName: "chevron-down"
                                    iconRotation: 180
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 30
                                    ToolTip.text: "Move rule up"
                                    enabled: rowItem.index > 0
                                    onClicked: root.moveUp(rowItem.index)
                                }

                                PxButton {
                                    objectName: "organizerRuleDown"
                                    variant: "ghost"
                                    iconName: "chevron-down"
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 30
                                    ToolTip.text: "Move rule down"
                                    enabled: rowItem.index < draftModel.count - 1
                                    onClicked: root.moveDown(rowItem.index)
                                }

                                PxButton {
                                    objectName: "organizerRuleRemove"
                                    variant: "danger"
                                    iconName: "trash"
                                    Layout.preferredWidth: 28
                                    Layout.preferredHeight: 30
                                    ToolTip.text: "Remove rule"
                                    onClicked: root.removeRule(rowItem.index)
                                }
                            }

                            Text {
                                objectName: "organizerRuleHits"
                                Layout.leftMargin: 24
                                text: {
                                    if (root.previewPending)
                                        return "Updating matches…"
                                    var hits = root.preview.counts[rowItem.index] || 0
                                    return hits + (hits === 1 ? " mod matches first here" : " mods match first here")
                                }
                                color: Theme.textDim
                                font.pixelSize: Theme.fontSizeSm
                            }
                        }
                    }
                }
            }
        }

        Text {
            objectName: "organizerRuleAssignable"
            Layout.fillWidth: true
            text: root.previewPending ? "Updating matches…" :
                root.preview.assignable + " currently ungrouped "
                + (root.preview.assignable === 1 ? "mod would" : "mods would")
                + " be assigned by these rules"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSm
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: draftModel.count === 0
            text: root.hasFolders ? "No rules configured. Click 'Add Rule'." : "No folders available."
            color: Theme.textDim
            font.pixelSize: Theme.fontSizeMd
        }
    }

    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        spacing: 8
        padding: 12
        leftPadding: 16
        rightPadding: 16

        background: Item {
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Theme.border
            }
        }

        PxButton {
            text: "Cancel"
            variant: "ghost"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }

        PxButton {
            objectName: "organizerSaveRules"
            text: "Save"
            variant: "primary"
            DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
            onClicked: root.submitRules()
        }
    }
}
