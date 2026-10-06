import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../components/controls"

PxComboBox {
    id: control
    property var options: []
    property string selectedTag: ""
    displayText: selectedTag || "All tags"
    model: options.filter(function (tag) {
        return tag === "All tags" || tag.toLowerCase().indexOf(tagSearch.text.toLowerCase()) >= 0
    })
    Accessible.name: "Tag filter"
    onActivated: selectedTag = currentText === "All tags" ? "" : currentText
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var model
        width: ListView.view ? ListView.view.width : control.width
        height: 32
        font: control.font
        text: model.modelData
        highlighted: optionsList.currentIndex === index
        onClicked: control.choose(index)
        contentItem: Text {
            text: option.text
            font: option.font
            color: option.text === control.displayText ? Theme.primary : Theme.textMain
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: option.highlighted ? Theme.elevate3 : "transparent"
        }
    }

    function choose(index) {
        if (index < 0 || index >= count)
            return
        currentIndex = index
        activated(index)
        popup.close()
    }

    popup: Popup {
        popupType: Popup.Window
        y: control.height + 4
        width: control.width
        padding: 8
        implicitHeight: contents.implicitHeight + 16
        onOpened: {
            tagSearch.text = ""
            tagSearch.forceActiveFocus()
        }
        onClosed: control.forceActiveFocus()
        contentItem: ColumnLayout {
            id: contents
            spacing: 8
            PxTextField {
                id: tagSearch
                Layout.fillWidth: true
                placeholderText: "Find a tag…"
                Accessible.name: "Search known tags"
                onAccepted: control.choose(control.count > 1 && text.length > 0 ? 1 : 0)
                Keys.onDownPressed: {
                    optionsList.currentIndex = 0
                    optionsList.forceActiveFocus()
                }
                Keys.onEscapePressed: control.popup.close()
            }
            ListView {
                id: optionsList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 240)
                clip: true
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: 0
                keyNavigationEnabled: true
                Keys.onReturnPressed: control.choose(currentIndex)
                Keys.onSpacePressed: control.choose(currentIndex)
                Keys.onEscapePressed: control.popup.close()
                ScrollBar.vertical: PxScrollBar {}
            }
        }
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.elevate2
            border.width: 1
            border.color: Theme.border
        }
    }
}
