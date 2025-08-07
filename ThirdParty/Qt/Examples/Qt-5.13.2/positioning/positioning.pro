TEMPLATE = subdirs

qtHaveModule(widgets): SUBDIRS += logfilepositionsource
qtHaveModule(quick) {
    SUBDIRS += satelliteinfo

    qtHaveModule(xmlpatterns): SUBDIRS += geoflickr
    qtHaveModule(network): SUBDIRS += weatherinfo
}
