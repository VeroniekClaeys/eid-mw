#!/bin/bash

set -e


# Go to the mac installers directory
cd $(dirname $0)/../../installers/eid-mw/mac

#-----------------------------------------
# make sure scripts are executable
#-----------------------------------------
CURRDIR=`pwd`
chmod +x ./create_package.sh
#chmod +x ../../../plugins_tools/xpi/build.sh

#-----------------------------------------
# build the eID MW
#-----------------------------------------

pushd ../../..
echo "cleaning former project..."
xcodebuild -project beidmw.xcodeproj -target beidpkcs11 clean
xcodebuild -project beidmw.xcodeproj -target beidpkcs11 -configuration Release

# store the SDK version (with own dialogs)

xcodebuild -project beidmw.xcodeproj -target beid_ff_pkcs11 clean
xcodebuild -project beidmw.xcodeproj -target beid_ff_pkcs11 -configuration Release GCC_PREPROCESSOR_DEFINITIONS_NOT_USED_IN_PRECOMPS='NO_DIALOGS=1' 
popd


pushd "../../../plugins_tools/aboutmw/OSX/eID Middleware"
xcodebuild -project "eID Middleware.xcodeproj" clean
xcodebuild -project "eID Middleware.xcodeproj" -target "eID Middleware" -configuration Release
popd

pushd "../../../cardcomm/ctkToken"
xcodebuild -project "BEIDToken.xcodeproj" clean
xcodebuild -project "BEIDToken.xcodeproj" -target "BEIDTokenApp" -configuration Release
popd

#-----------------------------------------
# create the xpi
#-----------------------------------------
#echo "creating the XPI..."
#pushd ../../../plugins_tools/xpi
#./build.sh -f
#popd

#-----------------------------------------
# create the pkg files
#-----------------------------------------
echo "creating the beid package..."
sudo ./create_package.sh

echo "[Info ] Done..."
