import QtQuick
import QtQuick.Layouts
import "../../components/controls"

ColumnLayout {
    id: root
    required property QtObject section
    readonly property var initial: section.initial
    spacing: 10

    component Label: Text {
        Layout.preferredWidth: 150
        color: Theme.textMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeMd
    }
    component Hint: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.textDim
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Auto-update mods" }
        PxComboBox {
            Layout.preferredWidth: 180
            Accessible.name: "Auto-update Workshop mods"
            textRole: "text"
            valueRole: "value"
            model: [
                { text: "Off", value: 0 },
                { text: "Every 6 hours", value: 6 },
                { text: "Every 12 hours", value: 12 },
                { text: "Daily", value: 24 },
                { text: "Every 3 days", value: 72 },
                { text: "Weekly", value: 168 }
            ]
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.initial.auto_update_hours))
            onActivated: root.section.set("auto_update_hours", currentValue)
        }
    }
    Hint { text: "Re-syncs mods downloaded by PxModRim while the app is running." }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Parallel mods" }
        PxSpinBox {
            from: 1
            to: 8
            value: root.initial.parallel_items
            Accessible.name: "Parallel Workshop mod downloads"
            toolTipText: "How many mods are downloaded at the same time. Raise it for many small mods; lower it if Steam throttles or drops the connection."
            onValueModified: root.section.set("parallel_items", value)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Threads per mod" }
        PxSpinBox {
            from: 1
            to: 16
            value: root.initial.threads_per_item
            Accessible.name: "Download threads per Workshop mod"
            toolTipText: "Connections used to fetch chunks of a single mod. Raise it for large mods on a fast link; total load is parallel mods \u00d7 threads."
            onValueModified: root.section.set("threads_per_item", value)
        }
    }
    Hint { text: "Total connections = parallel mods \u00d7 threads per mod. Applies to the next download." }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Proxy" }
        PxTextField {
            Layout.fillWidth: true
            monospace: true
            text: root.initial.proxy
            placeholderText: "Use system proxy"
            Accessible.name: "Workshop download proxy"
            onTextEdited: root.section.set("proxy", text.trim())
        }
    }
    Hint { text: "URL such as http://host:3128 or socks5h://host:1080. Empty uses https_proxy / all_proxy from the environment." }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Connect timeout (s)" }
        PxSpinBox {
            from: 1
            to: 120
            value: root.initial.connect_timeout
            Accessible.name: "Workshop connect timeout in seconds"
            toolTipText: "How long to wait for a connection before trying another server."
            onValueModified: root.section.set("connect_timeout", value)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label { text: "Stall timeout (s)" }
        PxSpinBox {
            from: 1
            to: 300
            value: root.initial.stall_timeout
            Accessible.name: "Workshop stall timeout in seconds"
            toolTipText: "How long a transfer may stay below 1 byte/s before it is abandoned and retried."
            onValueModified: root.section.set("stall_timeout", value)
        }
    }
}
