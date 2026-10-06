import QtQuick
import QtQuick.Layouts
import "../../components/controls"

ColumnLayout {
    id: root
    required property QtObject section
    spacing: 10

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.preferredWidth: 150
            text: "Workshop catalog URL"
            color: Theme.textMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeMd
        }
        PxTextField {
            objectName: "catalogUrlField"
            Layout.fillWidth: true
            text: root.section.initial.url
            placeholderText: "https://api.modrim.pyxiion.dev"
            Accessible.name: "Workshop catalog URL"
            onTextEdited: root.section.set("url", text.trim())
        }
    }
    Text {
        Layout.fillWidth: true
        text: "Catalog service used by the native Workshop browser. Leave empty to disable catalog network requests. Applies on Save."
        wrapMode: Text.Wrap
        color: Theme.textDim
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
