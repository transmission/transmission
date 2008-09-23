/* File ggets.h  - goodgets is a safe alternative to gets */
/* By C.B. Falconer.  Public domain 2002-06-22            */
/*    attribution appreciated.                            */


/* Revised 2002-06-26  New prototype.
           2002-06-27  Incomplete final lines
 */

/* fggets and ggets [which is fggets(ln, stdin)] set *ln to
   a buffer filled with the next complete line from the text
   stream f.  The storage has been allocated within fggets,
   and is normally reduced to be an exact fit.  The trailing
   \n has been removed, so the resultant line is ready for
   dumping with puts.  The buffer will be as large as is
   required to hold the complete line.

   Note: this means a final file line without a \n terminator
   has an effective \n appended, as EOF occurs within the read.

   If no error occurs fggets returns 0.  If an EOF occurs on
   the input file, EOF is returned.  For memory allocation
   errors some positive value is returned.  In this case *ln
   may point to a partial line.  For other errors memory is
   freed and *ln is set to NULL.

   Freeing of assigned storage is the callers responsibility
 */

#ifndef ggets_h_
#define ggets_h_

#ifdef __cplusplus
extern "C" {
#endif

int fggets( char* *ln,
            FILE * f );

#define ggets( ln ) fggets( ln, stdin )

#ifdef __cplusplus
}
#endif
#endif
/* END ggets.h */
