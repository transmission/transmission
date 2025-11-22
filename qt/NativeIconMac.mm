// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#import <AppKit/AppKit.h>

#import <QImage>
#import <QPixmap>
#import <QApplication>
#import <QColor>
#import <QPalette>

// Source: https://stackoverflow.com/a/74756071
// Posted by Bri Bri
// Retrieved 2025-11-22, License - CC BY-SA 4.0
namespace bribri
{

CGBitmapInfo CGBitmapInfoForQImage(QImage const& image)
{
    CGBitmapInfo bitmapInfo = kCGImageAlphaNone;

    switch (image.format())
    {
    case QImage::Format_ARGB32:
        bitmapInfo = kCGImageAlphaFirst | kCGBitmapByteOrder32Host;
        break;
    case QImage::Format_RGB32:
        bitmapInfo = kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Host;
        break;
    case QImage::Format_RGBA8888_Premultiplied:
        bitmapInfo = kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
        break;
    case QImage::Format_RGBA8888:
        bitmapInfo = kCGImageAlphaLast | kCGBitmapByteOrder32Big;
        break;
    case QImage::Format_RGBX8888:
        bitmapInfo = kCGImageAlphaNoneSkipLast | kCGBitmapByteOrder32Big;
        break;
    case QImage::Format_ARGB32_Premultiplied:
        bitmapInfo = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host;
        break;
    default:
        break;
    }

    return bitmapInfo;
}

QImage CGImageToQImage(CGImageRef cgImage)
{
    size_t const width = CGImageGetWidth(cgImage);
    size_t const height = CGImageGetHeight(cgImage);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate((void*)image.bits(), image.width(), image.height(), 8, image.bytesPerLine(), colorSpace, CGBitmapInfoForQImage(image));

    // Scale the context so that painting happens in device-independent pixels
    qreal const devicePixelRatio = image.devicePixelRatio();
    CGContextScaleCTM(context, devicePixelRatio, devicePixelRatio);

    CGRect rect = CGRectMake(0, 0, width, height);
    CGContextDrawImage(context, rect, cgImage);

    CFRelease(colorSpace);
    CFRelease(context);

    return image;
}

} // namespace bribri

QPixmap loadSFSymbol(QString const symbol_name, int const pixel_size)
{
    if (NSImage* image = [NSImage imageWithSystemSymbolName:symbol_name.toNSString() accessibilityDescription:nil])
    {
        // use whatever color QPalette::ButtonText is using
        QColor const qfg = qApp->palette().color(QPalette::ButtonText);
        NSColor* nsfg = [NSColor colorWithCalibratedRed:qfg.redF() green:qfg.greenF() blue:qfg.blueF() alpha:qfg.alphaF()];
        auto* configuration = [NSImageSymbolConfiguration configurationWithHierarchicalColor:nsfg];
        image = [image imageWithSymbolConfiguration:configuration];

        // NSImage -> QPixmap
        NSRect image_rect = NSMakeRect(0, 0, pixel_size, pixel_size);
        CGImageRef cgimg = [image CGImageForProposedRect:&image_rect context:nil hints:nil];
        return QPixmap::fromImage(bribri::CGImageToQImage(cgimg));
    }

    return {};
}
