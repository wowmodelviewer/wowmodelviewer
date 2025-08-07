/****************************************************************************
**
** Copyright (C) 2019 Julian Sherollari <jdotsh@gmail.com>
** Copyright (C) 2019 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

import QtQuick 2.11
import QtQuick.Controls 1.4 as C1
import QtQuick.Controls.Styles 1.4
import Qt.labs.platform 1.1
import QtQuick.Layouts 1.11
import QtQuick.Window 2.11
import QtPositioning 5.12
import QtLocation 5.12
import Qt.labs.qmlmodels 1.0
import Qt.GeoJson 1.0

C1.ApplicationWindow {
    visible: true
    width: 1024
    height: 1024
    menuBar: mainMenu
    title: qsTr("GeoJSON Viewer")

    FileDialog {
        visible: false
        id: fileDialog
        title: "Choose a GeoJSON file"
        fileMode: FileDialog.OpenFile
        folder: dataPath
        onAccepted: {
            geoJsoner.load(fileDialog.file)
        }
    }

    FileDialog {
        visible: false
        id: fileWriteDialog
        title: "Write your geometry to a file"
        fileMode: FileDialog.SaveFile
        folder: StandardPaths.writableLocation(StandardPaths.TempLocation)
        onAccepted: {
            geoJsoner.dumpGeoJSON(geoJsoner.toGeoJson(miv), fileWriteDialog.file);
        }
    }

    FileDialog {
        visible: false
        id: debugWriteDialog
        title: "Write Qvariant debug view"
        fileMode: FileDialog.SaveFile
        folder: StandardPaths.writableLocation(StandardPaths.TempLocation)
        onAccepted: {
            geoJsoner.writeDebug(geoJsoner.toGeoJson(miv), debugWriteDialog.file);
        }
    }

    C1.MenuBar {
        id: mainMenu

        C1.Menu {
            title: "&File"
            id : geoJsonMenu
            C1.MenuItem {
                text: "&Open"
                shortcut: StandardKey.Open
                onTriggered: {
                    fileDialog.open()
                }
            }
            C1.MenuItem {
                text: "&Export"
                shortcut: StandardKey.Save
                onTriggered: {
                    fileWriteDialog.open()
                }
            }
            C1.MenuItem {
                text: "E&xit"
                shortcut: StandardKey.Quit
                onTriggered: Qt.quit()
            }
        }
        C1.Menu {
            title: "&Debug"
            id : debugMenu
            C1.MenuItem {
                text: "Print debug data to &file"
                onTriggered: {
                    debugWriteDialog.open()
                }
            }
            C1.MenuItem {
                text: "&Print debug data"
                onTriggered: {
                    geoJsoner.print(miv)
                }
            }
        }

    }

    GeoJsoner {
        id: geoJsoner
    }

    Map {
        id: map
        anchors.fill: parent
        center: QtPositioning.coordinate(43.59, 13.50) // Starting coordinates: Ancona, the city where I am studying :)
        plugin: Plugin { name: "mapboxgl" }
        zoomLevel: 4

        MapItemView {
            id: miv
            model: geoJsoner.model
            delegate: GeoJsonDelegate {}
        }
    }
}
