#ifndef _CUSTOMIZATIONGEOSETTEST_H_
#define _CUSTOMIZATIONGEOSETTEST_H_

#include <functional>

#include <QString>

class ModelViewer;

// -customizationtest, every client: the geosets customization choices show, on the loaded client's own characters
// and records (see CustomizationGeosetTest.cpp). check(what, ok, detail) and skip(what, why) are the test's own.
void checkCustomizationGeosets(ModelViewer * frame, const std::function<void(const QString &, bool, const QString &)> & check,
                               const std::function<void(const QString &, const QString &)> & skip);

#endif
