#Transmission OS X Beta Builder (Universal)
#/bin/sh
cd ../../
echo STARTING OS X BETA BUILDER
echo CLEANING TRANSMISSION
xcodebuild -project Transmission.xcodeproj clean
echo BUILDING TRANSMISSION
xcodebuild -project Transmission.xcodeproj -target Transmission -configuration Release\ -\ Debug
echo CREATING BETA DIRECTORY
rm -rf beta
mkdir -p beta
echo COPYING TRANSMISSION.APP
cp -R build/Release\ -\ Debug/Transmission.app ./beta/
echo CREATING DMG
#create a BZ2 Compressed DMG
hdiutil create -volname Transmission -srcfolder beta/ -format UDBZ -noanyowners -fs HFS+ beta/Transmission-b.dmg
echo MAKING THE DMG INTERNET-ENABLED
hdiutil internet-enable -yes beta/Transmission-b.dmg
cd macosx
echo Beta Build Complete!