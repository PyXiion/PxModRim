import QtQuick
import "../../components/controls"

Rectangle {
    id: thumb
    property string previewUrl: ""
    property var collage: []
    property int widthStep: 64
    property int heightStep: 32

    color: Theme.elevate3
    radius: Theme.radiusMd
    clip: true

    CatalogImage {
        id: preview
        anchors.fill: parent
        previewUrl: thumb.previewUrl
        widthStep: thumb.widthStep
        heightStep: thumb.heightStep
    }
    Grid {
        id: collageGrid
        anchors.fill: parent
        visible: thumb.previewUrl === "" && thumb.collage.length > 0
        columns: thumb.collage.length === 1 ? 1 : 2
        readonly property int rowCount: thumb.collage.length > 2 ? 2 : 1
        Repeater {
            model: collageGrid.visible ? thumb.collage : []
            CatalogImage {
                required property string modelData
                width: collageGrid.width / collageGrid.columns
                height: collageGrid.height / collageGrid.rowCount
                previewUrl: modelData
            }
        }
    }
    Image {
        anchors.centerIn: parent
        width: Math.min(32, parent.height * 0.6)
        height: width
        visible: preview.status !== Image.Ready && !collageGrid.visible
        source: "image://icons/grid?color=" + encodeURIComponent(Theme.textDim)
    }
}
