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

static NSString * stringForFileSize( uint64_t size )
{
    if( size < 1024 )
    {
        return [NSString stringWithFormat: @"%lld bytes", size];
    }
    if( size < 1048576 )
    {
        return [NSString stringWithFormat: @"%lld.%lld KB",
                size / 1024, ( size % 1024 ) / 103];
    }
    if( size < 1073741824 )
    {
        return [NSString stringWithFormat: @"%lld.%lld MB",
                size / 1048576, ( size % 1048576 ) / 104858];
    }
    return [NSString stringWithFormat: @"%lld.%lld GB",
            size / 1073741824, ( size % 1073741824 ) / 107374183];
}

static float widthForString( NSString * string, float fontSize )
{
    NSMutableDictionary * attributes =
        [NSMutableDictionary dictionaryWithCapacity: 1];
    [attributes setObject: [NSFont messageFontOfSize: fontSize]
        forKey: NSFontAttributeName];

    return [string sizeWithAttributes: attributes].width;
}

static NSString * stringFittingInWidth( char * string, float width,
                                        float fontSize )
{
    NSString * nsString = NULL;
    char     * foo      = strdup( string );
    int        i;

    for( i = strlen( string ); i > 0; i-- )
    {
        foo[i] = '\0';
        nsString = [NSString stringWithFormat: @"%s%@",
            foo, ( i - strlen( string ) ? [NSString
            stringWithUTF8String:"\xE2\x80\xA6"] : @"" )];

        if( widthForString( nsString, fontSize ) <= width )
        {
            break;
        }
    }
    free( foo );
    return nsString;
}
