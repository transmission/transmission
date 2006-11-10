/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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
 * The format of the resume file is a 4 byte format version (currently 1),
 * followed by several variable-sized blocks of data.  Each block is
 * preceded by a 1 byte ID and a 4 byte length.  The currently recognized
 * IDs are defined below by the FR_ID_* macros.  The length does not include
 * the 5 bytes for the ID and length.
 *
 * The name of the resume file is "resume.<hash>".
 *
 * All values are stored in the native endianness. Moving a
 * libtransmission resume file from an architecture to another will not
 * work, although it will not hurt either (the version will be wrong,
 * so the resume file will not be read).
 **********************************************************************/

/* progress data:
 *  - 4 bytes * number of files: mtimes of files
 *  - 1 bit * number of blocks: whether we have the block or not
 *  - 4 bytes * number of pieces (byte aligned): the pieces that have
 *    been completed or started in each slot
 */
#define FR_ID_PROGRESS          0x01
/* number of bytes downloaded */
#define FR_ID_DOWNLOADED        0x02
/* number of bytes uploaded */
#define FR_ID_UPLOADED          0x03
/* IPs and ports of connectable peers */
#define FR_ID_PEERS             0x04

/* macros for the length of various pieces of the progress data */
#define FR_MTIME_LEN( t ) \
  ( 4 * (t)->info.fileCount )
#define FR_BLOCK_BITFIELD_LEN( t ) \
  ( ( (t)->blockCount + 7 ) / 8 )
#define FR_SLOTPIECE_LEN( t ) \
  ( 4 * (t)->info.pieceCount )
#define FR_PROGRESS_LEN( t ) \
  ( FR_MTIME_LEN( t ) + FR_BLOCK_BITFIELD_LEN( t ) + FR_SLOTPIECE_LEN( t ) )

static char * fastResumeFileName( tr_io_t * io )
{
    char * ret;

    asprintf( &ret, "%s/resume.%s", tr_getCacheDirectory(),
              io->tor->info.hashString );

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
        if( ( sb.st_mode & S_IFMT ) == S_IFREG )
        {
#ifdef SYS_DARWIN
            tab[i] = ( sb.st_mtimespec.tv_sec & 0x7FFFFFFF );
#else
            tab[i] = ( sb.st_mtime & 0x7FFFFFFF );
#endif
        }
        else
        {
            /* Empty folder */
            tab[i] = 0;
        }
        free( path );
    }

    return 0;
}

static inline void fastResumeWriteData( uint8_t id, void * data, uint32_t size,
                                        uint32_t count, FILE * file )
{
    uint32_t  datalen = size * count;

    fwrite( &id, 1, 1, file );
    fwrite( &datalen, 4, 1, file );
    fwrite( data, size, count, file );
}

static void fastResumeSave( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    
    FILE    * file;
    int       version = 1;
    char    * path;
    uint8_t * buf;
    uint64_t  total;
    int       size;

    buf = malloc( FR_PROGRESS_LEN( tor ) );

    /* Get file sizes */
    if( fastResumeMTimes( io, (int*)buf ) )
    {
        free( buf );
        return;
    }

    /* Create/overwrite the resume file */
    path = fastResumeFileName( io );
    if( !( file = fopen( path, "w" ) ) )
    {
        tr_err( "Could not open '%s' for writing", path );
        free( buf );
        free( path );
        return;
    }
    
    /* Write format version */
    fwrite( &version, 4, 1, file );

    /* Build and copy the bitfield for blocks */
    memcpy(buf + FR_MTIME_LEN( tor ), tr_cpBlockBitfield( tor->completion ),
           FR_BLOCK_BITFIELD_LEN( tor ) );

    /* Copy the 'slotPiece' table */
    memcpy(buf + FR_MTIME_LEN( tor ) + FR_BLOCK_BITFIELD_LEN( tor ),
           io->slotPiece, FR_SLOTPIECE_LEN( tor ) );

    /* Write progress data */
    fastResumeWriteData( FR_ID_PROGRESS, buf, 1, FR_PROGRESS_LEN( tor ), file );
    free( buf );

    /* Write download and upload totals */
    total = tor->downloadedCur + tor->downloadedPrev;
    fastResumeWriteData( FR_ID_DOWNLOADED, &total, 8, 1, file );
    total = tor->uploadedCur + tor->uploadedPrev;
    fastResumeWriteData( FR_ID_UPLOADED, &total, 8, 1, file );

    /* Write IPs and ports of connectable peers, if any */
    if( ( size = tr_peerGetConnectable( tor, &buf ) ) > 0 )
    {
        fastResumeWriteData( FR_ID_PEERS, buf, size, 1, file );
        free( buf );
    }

    fclose( file );

    tr_dbg( "Resume file '%s' written", path );
    free( path );
}

static int fastResumeLoadProgress( tr_io_t * io, FILE * file )
{
    tr_torrent_t * tor = io->tor;
    tr_info_t    * inf = &tor->info;

    int     * fileMTimes;
    int       i, j;
    uint8_t * buf;
    size_t    len;

    len = FR_PROGRESS_LEN( tor );
    buf = calloc( len, 1 );
    if( len != fread( buf, 1, len, file ) )
    {
        tr_inf( "Could not read from resume file" );
        free( buf );
        return 1;
    }

    /* Compare file mtimes */
    fileMTimes = malloc( FR_MTIME_LEN( tor ) );
    if( fastResumeMTimes( io, fileMTimes ) )
    {
        free( buf );
        free( fileMTimes );
        return 1;
    }
    if( memcmp( fileMTimes, buf, FR_MTIME_LEN( tor ) ) )
    {
        tr_inf( "File mtimes don't match" );
        free( buf );
        free( fileMTimes );
        return 1;
    }
    free( fileMTimes );

    /* Copy the bitfield for blocks and fill blockHave */
    tr_cpBlockBitfieldSet( tor->completion, buf + FR_MTIME_LEN( tor ) );

    /* Copy the 'slotPiece' table */
    memcpy( io->slotPiece, buf + FR_MTIME_LEN( tor ) +
            FR_BLOCK_BITFIELD_LEN( tor ), FR_SLOTPIECE_LEN( tor ) );

    free( buf );

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
    }
    // tr_dbg( "Slot used: %d", io->slotsUsed );

    return 0;
}

static int fastResumeLoadOld( tr_io_t * io, FILE * file )
{
    tr_torrent_t * tor = io->tor;
    
    int size;

    /* Check the size */
    size = 4 + FR_PROGRESS_LEN( tor );
    fseek( file, 0, SEEK_END );
    if( ftell( file ) != size )
    {
        tr_inf( "Wrong size for resume file (%d bytes, %d expected)",
                (int)ftell( file ), size );
        fclose( file );
        return 1;
    }

    /* load progress information */
    fseek( file, 4, SEEK_SET );
    if( fastResumeLoadProgress( io, file ) )
    {
        fclose( file );
        return 1;
    }

    fclose( file );

    tr_inf( "Fast resuming successful (version 0)" );
    
    return 0;
}

static int fastResumeLoad( tr_io_t * io )
{
    tr_torrent_t * tor = io->tor;
    
    FILE    * file;
    int       version = 0;
    char    * path;
    uint8_t   id;
    uint32_t  len;
    int       ret;

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

    /* Check format version */
    fread( &version, 4, 1, file );
    if( 0 == version )
    {
        return fastResumeLoadOld( io, file );
    }
    if( 1 != version )
    {
        tr_inf( "Resume file has version %d, not supported", version );
        fclose( file );
        return 1;
    }

    ret = 1;
    /* read each block of data */
    while( 1 == fread( &id, 1, 1, file ) && 1 == fread( &len, 4, 1, file ) )
    {
        switch( id )
        {
            case FR_ID_PROGRESS:
                /* read progress data */
                if( (uint32_t)FR_PROGRESS_LEN( tor ) == len )
                {
                    if( fastResumeLoadProgress( io, file ) )
                    {
                        if( feof( file ) || ferror( file ) )
                        {
                            fclose( file );
                            return 1;
                        }
                    }
                    else
                    {
                        ret = 0;
                    }
                    continue;
                }
                break;

            case FR_ID_DOWNLOADED:
                /* read download total */
                if( 8 == len)
                {
                    if( 1 != fread( &tor->downloadedPrev, 8, 1, file ) )
                    {
                        fclose( file );
                        return 1;
                    }
                    continue;
                }
                break;

            case FR_ID_UPLOADED:
                /* read upload total */
                if( 8 == len)
                {
                    if( 1 != fread( &tor->uploadedPrev, 8, 1, file ) )
                    {
                        fclose( file );
                        return 1;
                    }
                    continue;
                }
                break;

            case FR_ID_PEERS:
            {
                uint8_t * buf = malloc( len );
                if( 1 != fread( buf, len, 1, file ) )
                {
                    free( buf );
                    fclose( file );
                    return 1;
                }
                tr_peerAddCompactMany( tor, buf, len );
                free( buf );
                continue;
            }

            default:
                break;
        }

        /* if we didn't read the data, seek past it */
        tr_inf( "Skipping resume data type %02x, %u bytes", id, len );
        fseek( file, len, SEEK_CUR );
    }

    fclose( file );

    if( !ret )
    {
        tr_inf( "Fast resuming successful" );
    }
    
    return ret;
}
