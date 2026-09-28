import QtQuick
import QtQuick.Controls

ProgressBar {
    id: control

    property int thickness: 4

    implicitWidth: 200
    implicitHeight: thickness

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: control.thickness
        radius: Theme.radiusXs
        color: Theme.elevate3
    }

    contentItem: Item {
        implicitWidth: 200
        implicitHeight: control.thickness
        clip: true

        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            height: parent.height
            radius: Theme.radiusXs
            color: Theme.primary
        }

        Rectangle {
            id: sweep
            visible: control.indeterminate
            width: parent.width * 0.3
            height: parent.height
            radius: Theme.radiusXs
            color: Theme.primary

            XAnimator on x {
                running: control.indeterminate && control.visible
                loops: Animation.Infinite
                from: -sweep.width
                to: sweep.parent.width
                duration: 1200
            }
        }
    }
}
