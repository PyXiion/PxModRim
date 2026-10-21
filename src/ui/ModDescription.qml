import QtQuick

TextEdit {
    id: root

    property string description: ""

    text: description.length > 0
        ? "<style>a { color: " + Theme.primary + "; }"
          + " h1 { font-size: 18px; color: " + Theme.textMain + "; }"
          + " h2 { font-size: 16px; color: " + Theme.textMain + "; }"
          + " h3 { font-size: 14px; color: " + Theme.textMain + "; }"
          + " blockquote { color: " + Theme.textDim + "; }</style>"
          + description.replace(/__IMG_WIDTH__/g, Math.max(1, Math.floor(width)))
        : ""
    textFormat: TextEdit.RichText
    readOnly: true
    selectByMouse: true
    selectedTextColor: Theme.onAccent
    selectionColor: Theme.primary
    color: Theme.textMuted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeMd
    wrapMode: TextEdit.WordWrap

    HoverHandler {
        cursorShape: root.hoveredLink.length > 0 ? Qt.PointingHandCursor : Qt.IBeamCursor
    }
}
