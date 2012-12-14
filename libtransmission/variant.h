/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#ifndef TR_VARIANT_H
#define TR_VARIANT_H 1

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h> /* for int64_t */

struct evbuffer;

/**
 * @addtogroup tr_variant Variant
 *
 * An object that acts like a union for
 * integers, strings, lists, dictionaries, booleans, and floating-point numbers.
 * The structure is named tr_variant due to the historical reason that it was
 * originally tightly coupled with bencoded data. It currently supports
 * being parsed from, and serialized to, both bencoded notation and json notation.
 *
 * @{
 */

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
enum
{
  TR_VARIANT_TYPE_INT  = 1,
  TR_VARIANT_TYPE_STR  = 2,
  TR_VARIANT_TYPE_LIST = 4,
  TR_VARIANT_TYPE_DICT = 8,
  TR_VARIANT_TYPE_BOOL = 16,
  TR_VARIANT_TYPE_REAL = 32
};

/* These are PRIVATE IMPLEMENTATION details that should not be touched.
 * I'll probably change them just to break your code! HA HA HA!
 * it's included in the header for inlining and composition */
typedef struct tr_variant
{
  char type;

  union
    {
      bool b;

      double d;

      int64_t i;

      struct
        {
          size_t len; /* the string length */
          union
            {
              char buf[16]; /* local buffer for short strings */
              char * ptr; /* alloc'ed pointer for long strings */
            }
          str;
        }
      s;

      struct
        {
          struct tr_variant * vals; /* nodes */
          size_t alloc; /* nodes allocated */
          size_t count; /* nodes used */
        } l;
    }
  val;
}
tr_variant;

void  tr_variantFree (tr_variant *);

/***
****  Serialization / Deserialization
***/

typedef enum
{
    TR_VARIANT_FMT_BENC,
    TR_VARIANT_FMT_JSON,
    TR_VARIANT_FMT_JSON_LEAN /* saves bandwidth by omitting all whitespace. */
}
tr_variant_fmt;

int tr_variantToFile (const tr_variant * variant,
                      tr_variant_fmt     fmt,
                      const char       * filename);

char* tr_variantToStr (const tr_variant * variant,
                       tr_variant_fmt     fmt,
                       int              * len);

struct evbuffer * tr_variantToBuf (const tr_variant * variant,
                                   tr_variant_fmt     fmt);

/* TR_VARIANT_FMT_JSON_LEAN and TR_VARIANT_FMT_JSON are equivalent here. */
int tr_variantFromFile (tr_variant      * setme,
                        tr_variant_fmt    fmt,
                        const char      * filename);

/* TR_VARIANT_FMT_JSON_LEAN and TR_VARIANT_FMT_JSON are equivalent here. */
int tr_variantFromBuf (tr_variant     * setme,
                       tr_variant_fmt   fmt,
                       const void     * buf,
                       size_t           buflen,
                       const char     * optional_source,
                       const char    ** setme_end);

static inline int
tr_variantFromBenc (tr_variant * setme,
                    const void * buf,
                    size_t       buflen)
{
  return tr_variantFromBuf (setme, TR_VARIANT_FMT_BENC,
                            buf, buflen, NULL, NULL);
}
static inline int
tr_variantFromBencFull (tr_variant  * setme,
                        const void  * buf,
                        size_t        buflen,
                        const char  * source,
                        const char ** setme_end)
{
  return tr_variantFromBuf (setme,
                            TR_VARIANT_FMT_BENC,
                            buf,
                            buflen,
                            source,
                            setme_end);
}
static inline int
tr_variantFromJsonFull (tr_variant  * setme,
                        const void  * buf,
                        size_t        buflen,
                        const char  * source,
                        const char ** setme_end)
{
  return tr_variantFromBuf (setme,
                            TR_VARIANT_FMT_JSON,
                            buf,
                            buflen,
                            source,
                            setme_end);
}
static inline int
tr_variantFromJson (tr_variant  * setme,
                    const void  * buf,
                    size_t        buflen)
{
  return tr_variantFromBuf (setme,
                            TR_VARIANT_FMT_JSON,
                            buf,
                            buflen,
                            NULL,
                            NULL);
}
static inline bool
tr_variantIsType (const tr_variant * b, int type)
{
  return (b != NULL) && (b->type == type);
}


/***
****  Strings
***/

static inline bool
tr_variantIsString (const tr_variant * b)
{
  return tr_variantIsType (b, TR_VARIANT_TYPE_STR);
}

bool         tr_variantGetStr          (const tr_variant * variant,
                                        const char      ** setme_str,
                                        size_t           * setme_len);
                                                                 
void         tr_variantInitStr         (tr_variant       * initme,
                                        const void       * str,
                                        int                str_len);

void         tr_variantInitRaw         (tr_variant       * initme,
                                        const void       * raw,
                                        size_t             raw_len);

bool         tr_variantGetRaw          (const tr_variant * variant,
                                        const uint8_t   ** raw_setme,
                                        size_t           * len_setme);
/***
****  Real Numbers
***/

static inline bool
tr_variantIsReal (const tr_variant * b)
{
  return tr_variantIsType (b, TR_VARIANT_TYPE_REAL);
}

void         tr_variantInitReal        (tr_variant       * initme,
                                        double             value);

bool         tr_variantGetReal         (const tr_variant * variant,
                                        double           * value_setme);

/***
****  Booleans
***/

static inline bool
tr_variantIsBool (const tr_variant * b)
{
  return tr_variantIsType (b, TR_VARIANT_TYPE_BOOL);
}

void         tr_variantInitBool        (tr_variant       * initme,
                                        bool               value);

bool         tr_variantGetBool         (const tr_variant * variant,
                                        bool             * setme);


/***
****  Ints
***/

static inline bool
tr_variantIsInt (const tr_variant * b)
{
  return tr_variantIsType (b, TR_VARIANT_TYPE_INT);
}

void         tr_variantInitInt         (tr_variant       * variant,
                                        int64_t            value);

bool         tr_variantGetInt          (const tr_variant * val,
                                        int64_t          * setme);

/***
****  Lists
***/

static inline bool
tr_variantIsList (const tr_variant * b)
{
  return tr_variantIsType (b, TR_VARIANT_TYPE_LIST);
}

int          tr_variantInitList        (tr_variant       * list,
                                        size_t             reserve_count);

int          tr_variantListReserve     (tr_variant       * list,
                                        size_t             reserve_count);

tr_variant * tr_variantListAdd         (tr_variant       * list);

tr_variant * tr_variantListAddBool     (tr_variant       * list,
                                        bool               addme);

tr_variant * tr_variantListAddInt      (tr_variant       * list,
                                        int64_t            addme);

tr_variant * tr_variantListAddReal     (tr_variant       * list,
                                        double             addme);

tr_variant * tr_variantListAddStr      (tr_variant       * list,
                                        const char       * addme);

tr_variant * tr_variantListAddRaw      (tr_variant       * list,
                                        const void       * addme_value,
                                        size_t             addme_len);

tr_variant * tr_variantListAddList     (tr_variant       * list,
                                        size_t             reserve_count);

tr_variant * tr_variantListAddDict     (tr_variant       * list,
                                        size_t             reserve_count);

tr_variant * tr_variantListChild       (tr_variant       * list,
                                        size_t             pos);

int          tr_variantListRemove      (tr_variant       * list,
                                        size_t             pos);

size_t       tr_variantListSize        (const tr_variant * list);


/***
****  Dictionaries
***/

static inline bool
tr_variantIsDict (const tr_variant * b)
{
  return tr_variantIsType (b, TR_VARIANT_TYPE_DICT);
}

int          tr_variantInitDict        (tr_variant       * initme,
                                        size_t             reserve_count);

int          tr_variantDictReserve     (tr_variant       * dict,
                                        size_t             reserve_count);

int          tr_variantDictRemove      (tr_variant       * dict,
                                        const char       * key);

tr_variant * tr_variantDictAdd         (tr_variant       * dict,
                                        const char       * key);

tr_variant * tr_variantDictAddReal     (tr_variant       * dict,
                                        const char       * key,
                                        double             value);

tr_variant * tr_variantDictAddInt      (tr_variant       * dict,
                                        const char       * key,
                                        int64_t            value);

tr_variant * tr_variantDictAddBool     (tr_variant       * dict,
                                        const char       * key,
                                        bool               value);

tr_variant * tr_variantDictAddStr      (tr_variant       * dict,
                                        const char       * key,
                                        const char       * value);

tr_variant * tr_variantDictAddList     (tr_variant       * dict,
                                        const char       * key,
                                        size_t             reserve_count);

tr_variant * tr_variantDictAddDict     (tr_variant       * dict,
                                        const char       * key,
                                        size_t             reserve_count);

tr_variant * tr_variantDictAddRaw      (tr_variant       * dict,
                                        const char       * key,
                                        const void       * value,
                                        size_t             len);

bool         tr_variantDictChild       (tr_variant       * dict,
                                        size_t             pos,
                                        const char      ** setme_key,
                                        tr_variant      ** setme_value);

tr_variant * tr_variantDictFind        (tr_variant       * dict,
                                        const char       * key);

bool         tr_variantDictFindList    (tr_variant       * dict,
                                        const char       * key,
                                        tr_variant      ** setme);

bool         tr_variantDictFindDict    (tr_variant       * dict,
                                        const char       * key,
                                        tr_variant      ** setme_value);

bool         tr_variantDictFindInt     (tr_variant       * dict,
                                        const char       * key,
                                        int64_t          * setme);

bool         tr_variantDictFindReal    (tr_variant       * dict,
                                        const char       * key,
                                        double           * setme);

bool         tr_variantDictFindBool    (tr_variant       * dict,
                                        const char       * key,
                                        bool             * setme);

bool         tr_variantDictFindStr     (tr_variant       * dict,
                                        const char       * key,
                                        const char      ** setme,
                                        size_t           * len);

bool         tr_variantDictFindRaw     (tr_variant       * dict,
                                        const char       * key,
                                        const uint8_t   ** setme_raw,
                                        size_t           * setme_len);

/* this is only quasi-supported. don't rely on it too heavily outside of libT */
void         tr_variantMergeDicts      (tr_variant       * dict_target,
                                        const tr_variant * dict_source);

/***
****
****
***/

/**
***
**/

/* @} */

#ifdef __cplusplus
}
#endif

#endif
