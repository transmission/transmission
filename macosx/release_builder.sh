#Transmission OS X Release Builder (Universal)
#/bin/sh
cd ../
echo STARTING OS X RELEASE BUILDER
echo CLEANING TRANSMISSION
xcodebuild -project Transmission.xcodeproj clean
echo BUILDING TRANSMISSION
xcodebuild -project Transmission.xcodeproj -target Transmission -configuration Release
rm -rf release
echo CREATING RELEASE DIR
mkdir release
mkdir release/Transmission
echo COPYING TRANSMISSION.APP
cp -R macosx/Transmission.app ./release/Transmission/
echo CREATING DMG
#create a BZ2 Compressed DMG
hdiutil create -srcfolder release/Transmission/ -format UDBZ -noanyowners -fs HFS+ release/Transmission.dmg
echo MAKING THE DMG INTERNET-ENABLED
hdiutil internet-enable -yes release/Transmission.dmg
cd macosx
echo Release Build Complete!