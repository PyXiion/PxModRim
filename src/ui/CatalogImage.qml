import QtQuick
import "thumbs.js" as Thumbs

Image {
    property string previewUrl: ""
    property int widthStep: 64
    property int heightStep: 32
    // Qt updates sourceSize during reloads; URL dimensions must stay independent.
    readonly property int pixelWidth: Thumbs.bucket(width * Screen.devicePixelRatio, widthStep)
    readonly property int pixelHeight: Thumbs.bucket(height * Screen.devicePixelRatio, heightStep)

    asynchronous: true
    fillMode: Image.PreserveAspectCrop
    sourceSize.width: pixelWidth
    sourceSize.height: pixelHeight
    source: visible && width > 0 && height > 0
        ? Thumbs.sized(previewUrl, pixelWidth, pixelHeight) : ""
}
