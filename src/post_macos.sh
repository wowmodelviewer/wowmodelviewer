#!/bin/bash

# copy need file for package
mkdir -p wowmodelviewer.app/Contents/Frameworks
cp /sw/lib/libexpat.1.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libGLEW.1.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libiconv.2.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libjpeg.62.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libjpeg.8.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libpng12.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libpng.3.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libtiff.3.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_base_carbon-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_base_carbon_net-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_base_carbon_xml-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_mac_adv-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_mac_aui-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_mac_core-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
cp /sw/lib/libwx_mac_gl-2.8.0.dylib wowmodelviewer.app/Contents/Frameworks/
mkdir -p wowmodelviewer.app/Contents/Resources/
cp ../bin_support/Icons/wmv.icns wowmodelviewer.app/Contents/Resources/
cp ../bin_support/Splash/Splash_001.png wowmodelviewer.app/Contents/MacOS/Splash.png
mkdir -p wowmodelviewer.app/Contents/MacOS/userSettings/
#cp ../bin/userSettings/Skins.txt wowmodelviewer.app/Contents/MacOS/userSettings/
rm -f wowmodelviewer.app/Contents/MacOS/userSettings/log.txt
ver=`grep APP_VERSION modelviewer.h | awk -F'"' '{print $2}' | awk '{print $2}'`
rm -f wowmv-mac10_6-beta_intel32_$ver.zip
zip -r wowmv-mac10_6-beta_intel32_$ver wowmodelviewer.app
