/*
 * Copyright (C) 2016 - Florent Revest <revestflo@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick
import org.asteroid.controls
import org.asteroid.utils
import LocalMusic 1.0

Application {
    id: app

    centerColor: "#2577B3"
    outerColor: "#000619"

    LocalPlayback {
        id: player
    }

    StatusPage {
        text: player.status
        icon: "ios-sync"
        visible: player.trackCount === 0 && text !== ""
    }

    Item {
        anchors.fill: parent
        visible: player.trackCount > 0

        Image {
            id: coverArt
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            opacity: 0.7
            source: ""
        }

        Rectangle {
            id: textBackgroundDarkener
            visible: coverArt.status == Image.Ready
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: artistLabel.bottom
            anchors.bottomMargin: -Dims.h(0.5)
            color: "black"
            opacity: 0.5
        }

        Text {
            id: outputLabel
            anchors.top: parent.top
            anchors.topMargin: Dims.h(3)
            anchors.horizontalCenter: parent.horizontalCenter
            color: "white"
            opacity: 0.6
            font.pixelSize: Dims.l(6)
            font.capitalization: Font.AllUppercase
            text: player.output
            visible: text !== ""
        }

        Marquee {
            id: songLabel
            font.bold: true
            anchors.top: parent.top
            anchors.topMargin: Dims.h(7)
            anchors.horizontalCenter: parent.horizontalCenter
            height: Dims.h(10)
            width: DeviceSpecs.hasRoundScreen ? Dims.w(60) : Dims.w(80)

            text: player.title
        }

        Marquee {
            id: artistLabel
            anchors.top: songLabel.bottom
            anchors.topMargin: -Dims.h(1)
            anchors.horizontalCenter: parent.horizontalCenter
            height: Dims.h(10)
            width: DeviceSpecs.hasRoundScreen ? Dims.w(70) : Dims.w(80)

            text: player.artist
        }

        IconButton {
            id: previousButton
            anchors {
                left: parent.left
                verticalCenter: parent.verticalCenter
                leftMargin: Dims.iconButtonMargin
            }
            iconName: "ios-arrow-dropleft"
            onClicked: player.previous()
        }

        IconButton {
            id: playButton
            anchors.centerIn: parent
            width: Dims.w(40)
            height: width
            iconName: player.playing ? "ios-pause" : "ios-play"
            onClicked: player.playPause()
        }

        IconButton {
            id: nextButton
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: Dims.iconButtonMargin
            }
            iconName: "ios-arrow-dropright"
            onClicked: player.next()
        }

        Text {
            anchors.top: playButton.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: Dims.w(65)
            text: player.status
            color: "white"
            font.pixelSize: Dims.l(4)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        IconButton {
            id: volDownButton
            anchors.left: parent.left
            anchors.leftMargin: Dims.w(16)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Dims.h(12)
            iconName: "ios-volume-down"
            onClicked: player.volume = Math.max(player.volume - 0.05, 0)
        }

        Rectangle {
            id: volumeSlider
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Dims.h(21)
            anchors.horizontalCenter: parent.horizontalCenter
            height: Dims.h(2)
            width: Dims.w(28)
            color: "white"
            opacity: 0.6
            radius: 12
        }

        Rectangle {
            anchors.top: volumeSlider.top
            anchors.left: volumeSlider.left
            anchors.bottom: volumeSlider.bottom
            width: volumeSlider.width * player.volume
            color: "white"
            radius: 12
            opacity: 1.0
        }

        IconButton {
            id: volUpButton
            anchors.right: parent.right
            anchors.rightMargin: Dims.w(16)
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Dims.h(12)
            iconName: "ios-volume-up"
            onClicked: player.volume = Math.min(player.volume + 0.05, 1)
        }
    }
}
