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

import QtQuick 2.12
import QtPositioning 5.12
import QtLocation 5.12
import Qt.labs.qmlmodels 1.0

DelegateChooser {
    id: dc
    role: "type"
    property color defaultColor: "grey"
    property real defaultOpacity: 0.7

    DelegateChoice {
        roleValue: "Point"
        delegate: MapCircle {
            property string geojsonType: "Point"
            property var props: modelData.properties
            geoShape: modelData.data
            radius: 20*1000
            border.width: 3
            opacity: dc.defaultOpacity
            /* The expression below is equivalent to:
               ((props !== undefined && props["color"] !== undefined) ? props["color"] :
               ((parent && parent.props !== undefined && parent.props["color"] !== undefined) ? parent.props["color"] : dc.defaultColor))
            */
            color: (props && props.color) || (parent && parent.props && parent.props.color) || dc.defaultColor
        }
    }

    DelegateChoice {
        roleValue: "LineString"
        delegate: MapPolyline {
            property string geojsonType: "LineString"
            property var props: modelData.properties
            geoShape: modelData.data
            line.width: 3
            opacity: dc.defaultOpacity
            line.color: (props && props.color) || (parent && parent.props && parent.props.color) || dc.defaultColor
        }
    }

    DelegateChoice {
        roleValue: "Polygon"
        delegate: MapPolygon {
            property string geojsonType: "Polygon"
            property var props: modelData.properties
            geoShape: modelData.data
            opacity: dc.defaultOpacity
            color: (props && props.color) || (parent && parent.props && parent.props.color) || dc.defaultColor
            border.width: 0
        }
    }

    DelegateChoice {
        roleValue: "MultiPoint"
        delegate: MapItemView {
            property string geojsonType: "MultiPoint"
            property var props: modelData.properties
            model: modelData.data
            delegate: dc
        }
    }

    DelegateChoice {
        roleValue: "MultiLineString"
        delegate: MapItemView {
            property string geojsonType: "MultiLineString"
            property var props: modelData.properties
            model: modelData.data
            delegate: dc
        }
    }

    DelegateChoice {
        roleValue: "MultiPolygon"
        delegate: MapItemView {
            property string geojsonType: "MultiPolygon"
            property var props: modelData.properties
            model: modelData.data
            delegate: dc
        }
    }

    DelegateChoice {
        roleValue: "GeometryCollection"
        delegate: MapItemView {
            property string geojsonType: "GeometryCollection"
            property var props: modelData.properties
            model: modelData.data
            delegate: dc
        }
    }

    // Features are explicitly not generated by the parser, but converted straight to their content + the properties.

    DelegateChoice {
        roleValue: "FeatureCollection"
        delegate: MapItemView {
            property string geojsonType: "FeatureCollection"
            property var props: modelData.properties
            model: modelData.data
            delegate: dc
        }
    }
}
