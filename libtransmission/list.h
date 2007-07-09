/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#ifndef TR_LIST_H
#define TR_LIST_H

typedef struct tr_list_s
{
    void              * data;
    struct tr_list_s  * next;
    struct tr_list_s  * prev;
}
tr_list_t;

void        tr_list_free        ( tr_list_t* );
tr_list_t*  tr_list_append      ( tr_list_t*, void * data );
tr_list_t*  tr_list_prepend     ( tr_list_t*, void * data );
tr_list_t*  tr_list_remove_data ( tr_list_t*, const void * data );
tr_list_t*  tr_list_pop         ( tr_list_t*, void ** setme );

typedef int (*TrListCompareFunc)(const void * a, const void * b);
tr_list_t*  tr_list_find        ( tr_list_t*, TrListCompareFunc func, const void * b );
tr_list_t*  tr_list_find_data   ( tr_list_t*, const void * data );

typedef void (*TrListForeachFunc)(void *);
void tr_list_foreach            ( tr_list_t*, TrListForeachFunc func );

#endif /* TR_LIST_H */

