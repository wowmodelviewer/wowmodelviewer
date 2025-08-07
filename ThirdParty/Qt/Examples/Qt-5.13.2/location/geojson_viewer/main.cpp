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

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QVariantMap>
#include <QQmlContext>
#include <QtLocation/private/qgeojson_p.h>
#include <QGeoCircle>
#include <QGeoPath>
#include <QGeoPolygon>
#include <QtLocation/private/qdeclarativegeomapitemview_p.h>
#include <QtLocation/private/qdeclarativegeomapquickitem_p.h>
#include <QtLocation/private/qdeclarativecirclemapitem_p.h>
#include <QtLocation/private/qdeclarativepolylinemapitem_p.h>
#include <QtLocation/private/qdeclarativepolygonmapitem_p.h>
#include <QtLocation/private/qdeclarativerectanglemapitem_p.h>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QtCore/qobjectdefs.h>

class extractor
{
public:
    extractor();

    static bool hasProperties(QQuickItem *item)
    {
        QVariant props = item->property("props");
        return !props.isNull();
    }

    static bool isFeatureCollection(QQuickItem *item)
    {
        QVariant geoJsonType = item->property("geojsonType");
        return geoJsonType.toString() == QStringLiteral("FeatureCollection");
    }

    static bool isGeoJsonEntry(QQuickItem *item)
    {
        QVariant geoJsonType = item->property("geojsonType");
        return !geoJsonType.toString().isEmpty();
    }

    static QVariantMap toVariant(QDeclarativePolygonMapItem *mapPolygon)
    {
        QVariantMap ls;
        ls["type"] = "Polygon";
        ls["data"] = QVariant::fromValue(mapPolygon->geoShape());
        if (hasProperties(mapPolygon))
            ls["properties"] = mapPolygon->property("props").toMap();
        return ls;
    }
    static QVariantMap toVariant(QDeclarativePolylineMapItem *mapPolyline)
    {
        QVariantMap ls;
        ls["type"] = "LineString";
        ls["data"] = QVariant::fromValue(mapPolyline->geoShape());
        if (hasProperties(mapPolyline))
            ls["properties"] = mapPolyline->property("props").toMap();
        return ls;
    }
    static QVariantMap toVariant(QDeclarativeCircleMapItem *mapCircle)
    {
        QVariantMap pt;
        pt["type"] = "Point";
        pt["data"] = QVariant::fromValue(mapCircle->geoShape());
        if (hasProperties(mapCircle))
            pt["properties"] = mapCircle->property("props").toMap();
        return pt;
    }

    static QVariantMap toVariant(QDeclarativeGeoMapItemView *mapItemView)
    {
        // bool featureCollecton = isFeatureCollection(mapItemView);

        // If not a feature collection, this must be a geometry collection,
        // or a multilinestring/multipoint/multipolygon.
        // To disambiguate, one could check for heterogeneity.
        // For simplicity, in this example, we expect the property "geojsonType" to be injected in the mapItemView
        // by the delegate, and to be correct.

        QString nodeType = mapItemView->property("geojsonType").toString();
        QVariantMap root;
        if (!nodeType.isEmpty())  // Empty nodeType can happen only for the root MIV
            root["type"] = nodeType;
        if (hasProperties(mapItemView)) // Features are converted to regular types w properties.
            root["properties"] = mapItemView->property("props").toMap();

        QVariantList features;
        const QList<QQuickItem *> &quickChildren = mapItemView->childItems();
        for (auto kid : quickChildren) {
            QVariant entry;
            if (QDeclarativeGeoMapItemView *miv = qobject_cast<QDeclarativeGeoMapItemView *>(kid)) {
                // Handle nested miv
                entry = toVariant(miv);
            } else if (QDeclarativePolylineMapItem *polyline = qobject_cast<QDeclarativePolylineMapItem *>(kid)) {
                entry = toVariant(polyline);
            } else if (QDeclarativePolygonMapItem *polygon = qobject_cast<QDeclarativePolygonMapItem *>(kid)) {
                entry = toVariant(polygon);
            } else if (QDeclarativeCircleMapItem *circle = qobject_cast<QDeclarativeCircleMapItem *>(kid)) {
                entry = toVariant(circle); // If GeoJSON Point type is visualized in other ways, handle those types here instead.
            }
            features.append(entry);
        }
        if (nodeType.isEmpty()) // Dirty hack to handle (=skip) the first MIV used to process the fictitious list with 1 element
            return features.first().toMap();
        root["data"] = features;
        return root;
    }
};

class GeoJsoner: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant model MEMBER m_importedGeoJson NOTIFY modelChanged)

public:
    GeoJsoner(QObject *parent = nullptr) : QObject(parent)
    {

    }

public slots:

    Q_INVOKABLE bool load(QUrl url)
    {
        // Reading GeoJSON file
        QFile loadFile(url.toLocalFile());
        if (!loadFile.open(QIODevice::ReadOnly)) {
            qWarning() << "Error while opening the file: " << url;
            qWarning() << loadFile.error() <<  loadFile.errorString();
            return false;
        }

        // Load the GeoJSON file using Qt's API
        QJsonParseError err;
        QJsonDocument loadDoc(QJsonDocument::fromJson(loadFile.readAll(), &err));
        if (err.error) {
             qWarning() << "Parsing while importing the JSON document:\n" << err.errorString();
             return false;
        }

        // Import geographic data to a QVariantList
        QVariantList modelList = QGeoJson::importGeoJson(loadDoc);
        m_importedGeoJson =  modelList;
        emit modelChanged();
        return true;
    }

    // Used by the MapItemView Extractor to identify a Feature
    Q_INVOKABLE QVariantList toGeoJson(QDeclarativeGeoMapItemView *mapItemView)
    {
        QVariantList res;
        QDeclarativeGeoMapItemView *root = mapItemView;
        QVariantMap miv = extractor::toVariant(root);
        if (!miv.isEmpty())
            res.append(miv);
        return res;
    }

    Q_INVOKABLE void dumpGeoJSON(QVariantList geoJson, QUrl url)
    {
        QJsonDocument json = QGeoJson::exportGeoJson(geoJson);
        QFile jsonFile(url.toLocalFile());
        jsonFile.open(QIODevice::WriteOnly);
        jsonFile.write(json.toJson());
        jsonFile.close();
    }

    Q_INVOKABLE void writeDebug(QVariantList geoJson, QUrl url)
    {
        QString prettyPrint = QGeoJson::toString(geoJson);
        QFile debugFile(url.toLocalFile());
        debugFile.open(QIODevice::WriteOnly);
        debugFile.write(prettyPrint.toUtf8());
        debugFile.close();
    }

    Q_INVOKABLE void print(QDeclarativeGeoMapItemView *view)
    {
        QVariantList list;
        list.append(extractor::toVariant(view));
        QString prettyPrint =
                QGeoJson::toString(list);
        qDebug().noquote() << prettyPrint;
    }

signals:
    void modelChanged();

public:
    QVariant m_importedGeoJson;
};

#include "main.moc"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QUrl absoluteFilePath = argc > 1 ?
                    QUrl(QStringLiteral("file://") + QFileInfo(argv[1]).absoluteFilePath()) :
                    QUrl();
    engine.rootContext()->setContextProperty("dataPath", QUrl(QStringLiteral("file://")
                                                              + qPrintable(QT_STRINGIFY(SRC_PATH))
                                                              + QStringLiteral("/data")));
    qmlRegisterType<GeoJsoner>("Qt.GeoJson", 1, 0, "GeoJsoner");
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty())
        return -1;
    if (!absoluteFilePath.isEmpty()) {
        GeoJsoner *geoJsoner = engine.rootObjects().first()->findChild<GeoJsoner*>();
        QMetaObject::invokeMethod(geoJsoner, "load", Qt::QueuedConnection, Q_ARG(QUrl, absoluteFilePath));
    }

    return app.exec();
}
