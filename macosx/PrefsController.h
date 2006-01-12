/******************************************************************************
 * Copyright (c) 2005 Eric Petit
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <Cocoa/Cocoa.h>
#include <transmission.h>

@interface PrefsController : NSObject

{
    tr_handle_t * fHandle;

    IBOutlet NSWindow      * fWindow;
    IBOutlet NSWindow      * fPrefsWindow;
    IBOutlet NSMatrix      * fFolderMatrix;
    IBOutlet NSPopUpButton * fFolderPopUp;
    IBOutlet NSTextField   * fPortField;
    IBOutlet NSButton      * fUploadCheck;
    IBOutlet NSTextField   * fUploadField;

    NSString               * fDownloadFolder;
}

- (void) setHandle: (tr_handle_t *) handle;
- (void) show:      (id) sender;
- (void) ratio:     (id) sender;
- (void) check:     (id) sender;
- (void) cancel:    (id) sender;
- (void) save:      (id) sender;

@end
