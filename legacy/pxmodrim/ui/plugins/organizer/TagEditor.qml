import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

PxDialog {
    id: editor
    objectName: "organizerTagEditor"
    width: Math.min(520, parent ? parent.width - 32 : 520)
    height: Math.min(570, parent ? parent.height - 32 : 570)
    title: "Manage Tags"
    standardButtons: Dialog.Close

    property var tagRows: []
    property int selectedCount: 0
    function clearInputs() {
        nameInput.text = ""
        colorInput.text = String(Theme.primary)
    }
    property string errorMessage: ""
    signal createRequested(string name, string color)
    signal updateRequested(int id, string name, string color)
    signal deleteRequested(int id)
    signal assignmentRequested(int id, bool add)

    onOpened: clearInputs()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: editor.selectedCount
                  ? "Toggle tags for " + editor.selectedCount + " selected "
                    + (editor.selectedCount === 1 ? "mod" : "mods") + ". Changes apply immediately."
                  : "Create and edit tags. Select mods to assign tags."
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.WordWrap
        }

        ScrollView {
            ScrollBar.vertical: PxScrollBar {}
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: 6
                Repeater {
                    objectName: "organizerTagRepeater"
                    model: editor.tagRows.length
                    delegate: RowLayout {
                        id: tagRow
                        required property int index
                        readonly property var row: editor.tagRows[index]
                        Layout.fillWidth: true
                        property bool editing: false
                        property string editName: tagRow.row ? tagRow.row.name : ""
                        property string editColor: tagRow.row ? tagRow.row.color : ""

                        PxCheckBox {
                            objectName: "organizerTagAssignment"
                            visible: editor.selectedCount > 0
                            tristate: true
                            checkState: tagRow.row.checkState === 2 ? Qt.PartiallyChecked : tagRow.row.checkState === 1 ? Qt.Checked : Qt.Unchecked
                            nextCheckState: function() { return checkState === Qt.Checked ? Qt.Unchecked : Qt.Checked }
                            onClicked: editor.assignmentRequested(tagRow.row.id, checkState === Qt.Checked)
                            PxToolTip {
                                visible: parent.hovered
                                text: parent.checkState === Qt.PartiallyChecked ? "Assigned to some selected mods" : "Assign or remove for selected mods"
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            radius: 7
                            color: tagRow.row.color
                        }
                        PxTextField {
                            id: tagName
                            objectName: "organizerTagName"
                            Layout.fillWidth: true
                            text: tagRow.editing ? tagRow.editName : (tagRow.row ? tagRow.row.name : "")
                            onTextEdited: tagRow.editName = text
                            readOnly: !tagRow.editing
                        }
                        PxTextField {
                            id: tagColor
                            objectName: "organizerTagColor"
                            visible: tagRow.editing
                            Layout.preferredWidth: 90
                            monospace: true
                            text: tagRow.editing ? tagRow.editColor : (tagRow.row ? tagRow.row.color : "")
                            onTextEdited: tagRow.editColor = text
                            placeholderText: "#RRGGBB"
                        }
                        PxButton {
                            objectName: "organizerEditTag"
                            text: tagRow.editing ? "Save" : "Edit"
                            variant: tagRow.editing ? "primary" : "secondary"
                            onClicked: {
                                if (tagRow.editing) {
                                    editor.updateRequested(tagRow.row.id, tagName.text, tagColor.text)
                                } else {
                                    tagRow.editName = tagRow.row.name
                                    tagRow.editColor = tagRow.row.color
                                    tagRow.editing = true
                                }
                            }
                        }
                        PxButton {
                            objectName: "organizerDeleteTag"
                            text: "Delete"
                            variant: "danger"
                            onClicked: {
                                deleteConfirm.tagId = tagRow.row.id
                                deleteConfirm.tagName = tagRow.row.name
                                deleteConfirm.open()
                            }
                        }
                        Connections {
                            target: organizerPanel
                            function onTagUpdated(id) {
                                if (id === tagRow.row.id)
                                    tagRow.editing = false
                            }
                        }
                    }
                }
            }
        }

        Text {
            text: "New tag"
            color: Theme.textMain
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            font.weight: Font.Medium
        }
        RowLayout {
            Layout.fillWidth: true
            PxTextField {
                id: nameInput
                objectName: "organizerNewTagName"
                Layout.fillWidth: true
                placeholderText: "Tag name"
                onAccepted: addTag.clicked()
            }
            PxTextField {
                id: colorInput
                objectName: "organizerNewTagColor"
                Layout.preferredWidth: 100
                monospace: true
                placeholderText: "#RRGGBB"
            }
            PxButton {
                id: addTag
                objectName: "organizerAddTag"
                text: "Add"
                variant: "secondary"
                enabled: nameInput.text.trim().length > 0
                onClicked: editor.createRequested(nameInput.text, colorInput.text)
            }
        }
        ErrorBanner {
            objectName: "organizerTagError"
            Layout.fillWidth: true
            text: editor.errorMessage
        }
    }

    PxDialog {
        id: deleteConfirm
        objectName: "organizerDeleteTagConfirm"
        width: Math.min(360, parent ? parent.width - 32 : 360)
        title: "Delete Tag?"
        property int tagId: -1
        property string tagName: ""

        Text {
            width: parent.width
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
            text: "Delete ‘" + deleteConfirm.tagName + "’ from all mods?"
            color: Theme.textMain
            wrapMode: Text.WordWrap
        }

        footer: PxDialogFooter {
            PxButton {
                text: "Cancel"
                variant: "ghost"
                onClicked: deleteConfirm.reject()
            }
            PxButton {
                objectName: "organizerDeleteTagConfirmButton"
                text: "Delete"
                variant: "dangerSolid"
                onClicked: deleteConfirm.accept()
            }
        }

        onAccepted: editor.deleteRequested(tagId)
    }
}
