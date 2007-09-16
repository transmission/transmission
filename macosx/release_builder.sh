#Transmission OS X Release Builder (Universal)
#/bin/sh
cd ../
echo STARTING OS X RELEASE BUILDER
echo CLEANING TRANSMISSION
xcodebuild -project Transmission.xcodeproj clean
echo BUILDING TRANSMISSION
xcodebuild -project Transmission.xcodeproj -target Transmission -configuration Release
echo DELETING INTERFACE BUILDER DATA
rm -f macosx/Transmission.app/Contents/Resources/*.nib/info.nib
rm -f macosx/Transmission.app/Contents/Resources/*.nib/classes.nib
rm -f macosx/Transmission.app/Contents/Resources/*.lproj/*.nib/info.nib
rm -f macosx/Transmission.app/Contents/Resources/*.lproj/*.nib/classes.nib
rm -f macosx/Transmission.app/Contents/Frameworks/*.framework/Versions/[A-Z]/Resources/*.nib/info.nib
rm -f macosx/Transmission.app/Contents/Frameworks/*.framework/Versions/[A-Z]/Resources/*.nib/classes.nib
rm -f macosx/Transmission.app/Contents/Frameworks/*.framework/Versions/[A-Z]/Resources/*.lproj/*.nib/info.nib
rm -f macosx/Transmission.app/Contents/Frameworks/*.framework/Versions/[A-Z]/Resources/*.lproj/*.nib/classes.nib
echo DELETING FRAMEWORK HEADERS
rm -rf macosx/Transmission.app/Contents/Frameworks/*.framework/Versions/[A-Z]/Headers/*
echo CREATING RELEASE DIR
rm -rf release
mkdir -p release/Transmission
echo COPYING TRANSMISSION.APP
cp -R macosx/Transmission.app ./release/Transmission/
echo CREATING DMG
#create a BZ2 Compressed DMG
hdiutil create -srcfolder release/Transmission/ -format UDBZ -noanyowners -fs HFS+ release/Transmission.dmg
echo MAKING THE DMG INTERNET-ENABLED
hdiutil internet-enable -yes release/Transmission.dmg
cd macosx
echo Release Build Complete!