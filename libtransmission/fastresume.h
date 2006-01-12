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

/***********************************************************************
 * Fast resume
 ***********************************************************************
 * Format of the resume file:
 *  - 4 bytes: format version (currently 0)
 *  - 4 bytes * number of files: mtimes of files
 *  - 1 bit * number of blocks: whether we have the block or not
 *  - 4 bytes * number of pieces (byte aligned): the pieces that have
 *    been completed or started in each slot
 *
 * The name of the resume file is "resume.<hash>".
 *
 * All values are stored in the native endianness. Moving a
 * libtransmission resume file from an architecture to another will not
 * work, although it will not hurt either (the mtimes will be wrong,
 * so the files will be scanned).
 **********************************************************************/

static char * fastResumeFileName( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    char * ret, * p;
    int i;

    asprintf( &ret, "%s/resume.%40d", tor->prefsDirectory, 0 );

    p = &ret[ strlen( ret ) - 2 * SHA_DIGEST_LENGTH ];
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        sprintf( p, "%02x", io->tor->info.hash[i] );
        p += 2;
    }

    return ret;
}

static int fastResumeMTimes( tr_io_t * io, int * tab )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    int           i;
    char        * path;
    struct stat   sb;

    for( i = 0; i < inf->fileCount; i++ )
    {
        asprintf( &path, "%s/%s", tor->destination, inf->files[i].name );
        if( stat( path, &sb ) )
        {
            tr_err( "Could not stat '%s'", path );
            free( path );
            return 1;
        }
        if( ( sb.st_mode & S_IFMT ) != S_IFREG )
        {
            tr_err( "Wrong st_mode for '%s'", path );
            free( path );
            return 1;
        }
        free( path );

#ifdef SYS_DARWIN
        tab[i] = ( sb.st_mtimespec.tv_sec & 0x7FFFFFFF );
#else
        tab[i] = ( sb.st_mtime & 0x7FFFFFFF );
#endif
    }

    return 0;
}

static void fastResumeSave( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;
    
    FILE    * file;
    int       version = 0;
    char    * path;
    int     * fileMTimes;
    int       i;
    uint8_t * blockBitfield;

    /* Get file sizes */
    fileMTimes = malloc( inf->fileCount * 4 );
    if( fastResumeMTimes( io, fileMTimes ) )
    {
        free( fileMTimes );
        return;
    }

    /* Create/overwrite the resume file */
    path = fastResumeFileName( io );
    if( !( file = fopen( path, "w" ) ) )
    {
        tr_err( "Could not open '%s' for writing", path );
        free( fileMTimes );
        free( path );
        return;
    }
    
    /* Write format version */
    fwrite( &version, 4, 1, file );

    /* Write file mtimes */
    fwrite( fileMTimes, 4, inf->fileCount, file );
    free( fileMTimes );

    /* Build and write the bitfield for blocks */
    blockBitfield = calloc( ( tor->blockCount + 7 ) / 8, 1 );
    for( i = 0; i < tor->blockCount; i++ )
    {
        if( tor->blockHave[i] < 0 )
        {
            tr_bitfieldAdd( blockBitfield, i );
        }
    }
    fwrite( blockBitfield, ( tor->blockCount + 7 ) / 8, 1, file );
    free( blockBitfield );

    /* Write the 'slotPiece' table */
    fwrite( io->slotPiece, 4, inf->pieceCount, file );

    fclose( file );

    tr_dbg( "Resume file '%s' written", path );
    free( path );
}

static int fastResumeLoad( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;
    
    FILE    * file;
    int       version = 0;
    char    * path;
    int     * fileMTimes1, * fileMTimes2;
    int       i, j;
    uint8_t * blockBitfield;

    int size;

    /* Open resume file */
    path = fastResumeFileName( io );
    if( !( file = fopen( path, "r" ) ) )
    {
        tr_inf( "Could not open '%s' for reading", path );
        free( path );
        return 1;
    }
    tr_dbg( "Resume file '%s' loaded", path );
    free( path );

    /* Check the size */
    size = 4 + 4 * inf->fileCount + 4 * inf->pieceCount +
        ( tor->blockCount + 7 ) / 8;
    fseek( file, 0, SEEK_END );
    if( ftell( file ) != size )
    {
        tr_inf( "Wrong size for resume file (%d bytes, %d expected)",
                ftell( file ), size );
        fclose( file );
        return 1;
    }
    fseek( file, 0, SEEK_SET );

    /* Check format version */
    fread( &version, 4, 1, file );
    if( version != 0 )
    {
        tr_inf( "Resume file has version %d, not supported",
                version );
        fclose( file );
        return 1;
    }

    /* Compare file mtimes */
    fileMTimes1 = malloc( inf->fileCount * 4 );
    if( fastResumeMTimes( io, fileMTimes1 ) )
    {
        free( fileMTimes1 );
        fclose( file );
        return 1;
    }
    fileMTimes2 = malloc( inf->fileCount * 4 );
    fread( fileMTimes2, 4, inf->fileCount, file );
    if( memcmp( fileMTimes1, fileMTimes2, inf->fileCount * 4 ) )
    {
        tr_inf( "File mtimes don't match" );
        free( fileMTimes1 );
        free( fileMTimes2 );
        fclose( file );
        return 1;
    }
    free( fileMTimes1 );
    free( fileMTimes2 );

    /* Load the bitfield for blocks and fill blockHave */
    blockBitfield = calloc( ( tor->blockCount + 7 ) / 8, 1 );
    fread( blockBitfield, ( tor->blockCount + 7 ) / 8, 1, file );
    tor->blockHaveCount = 0;
    for( i = 0; i < tor->blockCount; i++ )
    {
        if( tr_bitfieldHas( blockBitfield, i ) )
        {
            tor->blockHave[i] = -1;
            (tor->blockHaveCount)++;
        }
    }
    free( blockBitfield );

    /* Load the 'slotPiece' table */
    fread( io->slotPiece, 4, inf->pieceCount, file );

    fclose( file );

    /* Update io->pieceSlot, io->slotsUsed, and tor->bitfield */
    io->slotsUsed = 0;
    for( i = 0; i < inf->pieceCount; i++ )
    {
        io->pieceSlot[i] = -1;
        for( j = 0; j < inf->pieceCount; j++ )
        {
            if( io->slotPiece[j] == i )
            {
                // tr_dbg( "Has piece %d in slot %d", i, j );
                io->pieceSlot[i] = j;
                io->slotsUsed = MAX( io->slotsUsed, j + 1 );
                break;
            }
        }

        for( j = tr_pieceStartBlock( i );
             j < tr_pieceStartBlock( i ) + tr_pieceCountBlocks( i );
             j++ )
        {
            if( tor->blockHave[j] > -1 )
            {
                break;
            }
        }
        if( j >= tr_pieceStartBlock( i ) + tr_pieceCountBlocks( i ) )
        {
            // tr_dbg( "Piece %d is complete", i );
            tr_bitfieldAdd( tor->bitfield, i );
        }
    }
    // tr_dbg( "Slot used: %d", io->slotsUsed );

    tr_inf( "Fast resuming successful" );
    
    return 0;
}
