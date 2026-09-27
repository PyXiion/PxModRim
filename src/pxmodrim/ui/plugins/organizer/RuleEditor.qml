import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

PxDialog {
    id: root
    objectName: "organizerRuleEditor"
    width: Math.min(540, parent ? parent.width - 24 : 380)
    height: Math.min(540, parent ? parent.height - 24 : 480)
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
        onDataChanged: root.draftRevision++
        onRowsInserted: root.draftRevision++
        onRowsRemoved: root.draftRevision++
        onRowsMoved: root.draftRevision++
        onModelReset: root.draftRevision++
    }

    function loadDraft() {
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
    }

    onRuleRowsChanged: {
        if (visible && draftMatches(loadedRows) && !draftMatches(ruleRows))
            loadDraft()
    }

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

    function submitRules() {
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
        root.saveRequested(rules)
    }

    function cancelDraft() {
        loadDraft()
        root.close()
    }

    Connections {
        target: organizerPanel
        function onRulesSaved() {
            root.close()
        }
    }

    onAboutToShow: loadDraft()
    onRejected: cancelDraft()

    contentItem: ColumnLayout {
        anchors.margins: 12
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "Rules match mods to folders in order from top to bottom."
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

            Item {
                Layout.fillWidth: true
            }

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
                        implicitHeight: rowCol.implicitHeight + 14
                        color: Theme.elevate1
                        border.color: Theme.border
                        border.width: 1
                        radius: Theme.radiusSm

                        ColumnLayout {
                            id: rowCol
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                PxComboBox {
                                    objectName: "organizerRuleField"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 70
                                    model: root.fieldOptions
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: root.findFieldIndex(rowItem.field)
                                    onActivated: draftModel.setProperty(rowItem.index, "field", currentValue)
                                }

                                PxComboBox {
                                    objectName: "organizerRuleOperator"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 65
                                    model: root.opOptions
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: root.findOpIndex(rowItem.op)
                                    onActivated: draftModel.setProperty(rowItem.index, "op", currentValue)
                                }

                                PxComboBox {
                                    objectName: "organizerRuleFolder"
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 80
                                    model: root.ruleFolders
                                    textRole: "name"
                                    valueRole: "id"
                                    currentIndex: root.findFolderIndex(rowItem.folder_id)
                                    onActivated: draftModel.setProperty(rowItem.index, "folder_id", currentValue)
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 4

                                PxTextField {
                                    objectName: "organizerRulePattern"
                                    Layout.fillWidth: true
                                    text: rowItem.pattern
                                    placeholderText: "Pattern..."
                                    onTextEdited: draftModel.setProperty(rowItem.index, "pattern", text)
                                }

                                PxButton {
                                    objectName: "organizerRuleUp"
                                    variant: "ghost"
                                    iconName: "chevron-down"
                                    iconRotation: 180
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    ToolTip.text: "Move rule up"
                                    enabled: rowItem.index > 0
                                    onClicked: root.moveUp(rowItem.index)
                                }

                                PxButton {
                                    objectName: "organizerRuleDown"
                                    variant: "ghost"
                                    iconName: "chevron-down"
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    ToolTip.text: "Move rule down"
                                    enabled: rowItem.index < draftModel.count - 1
                                    onClicked: root.moveDown(rowItem.index)
                                }

                                PxButton {
                                    objectName: "organizerRuleRemove"
                                    variant: "danger"
                                    iconName: "trash"
                                    Layout.preferredWidth: 32
                                    Layout.preferredHeight: 32
                                    ToolTip.text: "Remove rule"
                                    onClicked: root.removeRule(rowItem.index)
                                }
                            }
                        }
                    }
                }
            }
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
