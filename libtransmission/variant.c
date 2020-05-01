/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#if defined(HAVE_USELOCALE) && (!defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 700)
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#if defined(HAVE_USELOCALE) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdlib.h> /* strtod(), realloc(), qsort() */
#include <string.h>

#ifdef _WIN32
#include <share.h>
#endif

#include <locale.h> /* setlocale() */

#if defined(HAVE_USELOCALE) && defined(HAVE_XLOCALE_H)
#include <xlocale.h>
#endif

#include <event2/buffer.h>

#define __LIBTRANSMISSION_VARIANT_MODULE__

#include "transmission.h"
#include "ConvertUTF.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "tr-assert.h"
#include "utils.h" /* tr_new(), tr_free() */
#include "variant.h"
#include "variant-common.h"

/* don't use newlocale/uselocale on old versions of uClibc because they're buggy.
 * https://trac.transmissionbt.com/ticket/6006 */
#if defined(__UCLIBC__) && !TR_UCLIBC_CHECK_VERSION(0, 9, 34)
#undef HAVE_USELOCALE
#endif

/**
***
**/

struct locale_context
{
#ifdef HAVE_USELOCALE
    locale_t new_locale;
    locale_t old_locale;
#else
#if defined(HAVE__CONFIGTHREADLOCALE) && defined(_ENABLE_PER_THREAD_LOCALE)
    int old_thread_config;
#endif
    int category;
    char old_locale[128];
#endif
};

static void use_numeric_locale(struct locale_context* context, char const* locale_name)
{
#ifdef HAVE_USELOCALE

    context->new_locale = newlocale(LC_NUMERIC_MASK, locale_name, NULL);
    context->old_locale = uselocale(context->new_locale);

#else

#if defined(HAVE__CONFIGTHREADLOCALE) && defined(_ENABLE_PER_THREAD_LOCALE)
    context->old_thread_config = _configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
#endif

    context->category = LC_NUMERIC;
    tr_strlcpy(context->old_locale, setlocale(context->category, NULL), sizeof(context->old_locale));
    setlocale(context->category, locale_name);

#endif
}

static void restore_locale(struct locale_context* context)
{
#ifdef HAVE_USELOCALE

    uselocale(context->old_locale);
    freelocale(context->new_locale);

#else

    setlocale(context->category, context->old_locale);

#if defined(HAVE__CONFIGTHREADLOCALE) && defined(_ENABLE_PER_THREAD_LOCALE)
    _configthreadlocale(context->old_thread_config);
#endif

#endif
}

/***
****
***/

static bool tr_variantIsContainer(tr_variant const* v)
{
    return tr_variantIsList(v) || tr_variantIsDict(v);
}

static bool tr_variantIsSomething(tr_variant const* v)
{
    return tr_variantIsContainer(v) || tr_variantIsInt(v) || tr_variantIsString(v) || tr_variantIsReal(v) ||
        tr_variantIsBool(v);
}

void tr_variantInit(tr_variant* v, char type)
{
    v->type = type;
    memset(&v->val, 0, sizeof(v->val));
}

/***
****
***/

static struct tr_variant_string const STRING_INIT =
{
    .type = TR_STRING_TYPE_QUARK,
    .quark = TR_KEY_NONE,
    .len = 0,
    .str.str = ""
};

static void tr_variant_string_clear(struct tr_variant_string* str)
{
    if (str->type == TR_STRING_TYPE_HEAP)
    {
        tr_free((char*)(str->str.str));
    }

    *str = STRING_INIT;
}

/* returns a const pointer to the variant's string */
static char const* tr_variant_string_get_string(struct tr_variant_string const* str)
{
    char const* ret;

    switch (str->type)
    {
    case TR_STRING_TYPE_BUF:
        ret = str->str.buf;
        break;

    case TR_STRING_TYPE_HEAP:
        ret = str->str.str;
        break;

    case TR_STRING_TYPE_QUARK:
        ret = str->str.str;
        break;

    default:
        ret = NULL;
    }

    return ret;
}

static void tr_variant_string_set_quark(struct tr_variant_string* str, tr_quark const quark)
{
    tr_variant_string_clear(str);

    str->type = TR_STRING_TYPE_QUARK;
    str->quark = quark;
    str->str.str = tr_quark_get_string(quark, &str->len);
}

static void tr_variant_string_set_string(struct tr_variant_string* str, char const* bytes, size_t len)
{
    tr_variant_string_clear(str);

    if (bytes == NULL)
    {
        len = 0;
    }
    else if (len == TR_BAD_SIZE)
    {
        len = strlen(bytes);
    }

    if (len < sizeof(str->str.buf))
    {
        str->type = TR_STRING_TYPE_BUF;
        memcpy(str->str.buf, bytes, len);
        str->str.buf[len] = '\0';
        str->len = len;
    }
    else
    {
        char* tmp = tr_new(char, len + 1);
        memcpy(tmp, bytes, len);
        tmp[len] = '\0';
        str->type = TR_STRING_TYPE_HEAP;
        str->str.str = tmp;
        str->len = len;
    }
}

/***
****
***/

static inline char const* getStr(tr_variant const* v)
{
    TR_ASSERT(tr_variantIsString(v));

    return tr_variant_string_get_string(&v->val.s);
}

static int dictIndexOf(tr_variant const* dict, tr_quark const key)
{
    if (tr_variantIsDict(dict))
    {
        for (size_t i = 0; i < dict->val.l.count; ++i)
        {
            if (dict->val.l.vals[i].key == key)
            {
                return (int)i;
            }
        }
    }

    return -1;
}

tr_variant* tr_variantDictFind(tr_variant* dict, tr_quark const key)
{
    int const i = dictIndexOf(dict, key);

    return i < 0 ? NULL : dict->val.l.vals + i;
}

static bool tr_variantDictFindType(tr_variant* dict, tr_quark const key, int type, tr_variant** setme)
{
    *setme = tr_variantDictFind(dict, key);
    return tr_variantIsType(*setme, type);
}

size_t tr_variantListSize(tr_variant const* list)
{
    return tr_variantIsList(list) ? list->val.l.count : 0;
}

tr_variant* tr_variantListChild(tr_variant* v, size_t i)
{
    tr_variant* ret = NULL;

    if (tr_variantIsList(v) && i < v->val.l.count)
    {
        ret = v->val.l.vals + i;
    }

    return ret;
}

bool tr_variantListRemove(tr_variant* list, size_t i)
{
    bool removed = false;

    if (tr_variantIsList(list) && i < list->val.l.count)
    {
        removed = true;
        tr_variantFree(&list->val.l.vals[i]);
        tr_removeElementFromArray(list->val.l.vals, i, sizeof(tr_variant), list->val.l.count);
        --list->val.l.count;
    }

    return removed;
}

bool tr_variantGetInt(tr_variant const* v, int64_t* setme)
{
    bool success = false;

    if (tr_variantIsInt(v))
    {
        if (setme != NULL)
        {
            *setme = v->val.i;
        }

        success = true;
    }

    if (!success && tr_variantIsBool(v))
    {
        if (setme != NULL)
        {
            *setme = v->val.b ? 1 : 0;
        }

        success = true;
    }

    return success;
}

bool tr_variantGetStr(tr_variant const* v, char const** setme, size_t* len)
{
    bool const success = tr_variantIsString(v);

    if (success)
    {
        *setme = getStr(v);
    }

    if (len != NULL)
    {
        *len = success ? v->val.s.len : 0;
    }

    return success;
}

bool tr_variantGetRaw(tr_variant const* v, uint8_t const** setme_raw, size_t* setme_len)
{
    bool const success = tr_variantIsString(v);

    if (success)
    {
        *setme_raw = (uint8_t*)getStr(v);
        *setme_len = v->val.s.len;
    }

    return success;
}

bool tr_variantGetBool(tr_variant const* v, bool* setme)
{
    char const* str;
    bool success = false;

    if (tr_variantIsBool(v))
    {
        *setme = v->val.b;
        success = true;
    }

    if (!success && tr_variantIsInt(v))
    {
        if (v->val.i == 0 || v->val.i == 1)
        {
            *setme = v->val.i != 0;
            success = true;
        }
    }

    if (!success && tr_variantGetStr(v, &str, NULL))
    {
        if (strcmp(str, "true") == 0 || strcmp(str, "false") == 0)
        {
            *setme = strcmp(str, "true") == 0;
            success = true;
        }
    }

    return success;
}

bool tr_variantGetReal(tr_variant const* v, double* setme)
{
    bool success = false;

    if (tr_variantIsReal(v))
    {
        *setme = v->val.d;
        success = true;
    }

    if (!success && tr_variantIsInt(v))
    {
        *setme = v->val.i;
        success = true;
    }

    if (!success && tr_variantIsString(v))
    {
        char* endptr;
        struct locale_context locale_ctx;
        double d;

        /* the json spec requires a '.' decimal point regardless of locale */
        use_numeric_locale(&locale_ctx, "C");
        d = strtod(getStr(v), &endptr);
        restore_locale(&locale_ctx);

        if (getStr(v) != endptr && *endptr == '\0')
        {
            *setme = d;
            success = true;
        }
    }

    return success;
}

bool tr_variantDictFindInt(tr_variant* dict, tr_quark const key, int64_t* setme)
{
    tr_variant* child = tr_variantDictFind(dict, key);
    return tr_variantGetInt(child, setme);
}

bool tr_variantDictFindBool(tr_variant* dict, tr_quark const key, bool* setme)
{
    tr_variant* child = tr_variantDictFind(dict, key);
    return tr_variantGetBool(child, setme);
}

bool tr_variantDictFindReal(tr_variant* dict, tr_quark const key, double* setme)
{
    tr_variant* child = tr_variantDictFind(dict, key);
    return tr_variantGetReal(child, setme);
}

bool tr_variantDictFindStr(tr_variant* dict, tr_quark const key, char const** setme, size_t* len)
{
    tr_variant* child = tr_variantDictFind(dict, key);
    return tr_variantGetStr(child, setme, len);
}

bool tr_variantDictFindList(tr_variant* dict, tr_quark const key, tr_variant** setme)
{
    return tr_variantDictFindType(dict, key, TR_VARIANT_TYPE_LIST, setme);
}

bool tr_variantDictFindDict(tr_variant* dict, tr_quark const key, tr_variant** setme)
{
    return tr_variantDictFindType(dict, key, TR_VARIANT_TYPE_DICT, setme);
}

bool tr_variantDictFindRaw(tr_variant* dict, tr_quark const key, uint8_t const** setme_raw, size_t* setme_len)
{
    tr_variant* child = tr_variantDictFind(dict, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

/***
****
***/

void tr_variantInitRaw(tr_variant* v, void const* src, size_t byteCount)
{
    tr_variantInit(v, TR_VARIANT_TYPE_STR);
    tr_variant_string_set_string(&v->val.s, src, byteCount);
}

void tr_variantInitQuark(tr_variant* v, tr_quark const q)
{
    tr_variantInit(v, TR_VARIANT_TYPE_STR);
    tr_variant_string_set_quark(&v->val.s, q);
}

void tr_variantInitStr(tr_variant* v, void const* str, size_t len)
{
    tr_variantInit(v, TR_VARIANT_TYPE_STR);
    tr_variant_string_set_string(&v->val.s, str, len);
}

void tr_variantInitBool(tr_variant* v, bool value)
{
    tr_variantInit(v, TR_VARIANT_TYPE_BOOL);
    v->val.b = value != 0;
}

void tr_variantInitReal(tr_variant* v, double value)
{
    tr_variantInit(v, TR_VARIANT_TYPE_REAL);
    v->val.d = value;
}

void tr_variantInitInt(tr_variant* v, int64_t value)
{
    tr_variantInit(v, TR_VARIANT_TYPE_INT);
    v->val.i = value;
}

void tr_variantInitList(tr_variant* v, size_t reserve_count)
{
    tr_variantInit(v, TR_VARIANT_TYPE_LIST);
    tr_variantListReserve(v, reserve_count);
}

static void containerReserve(tr_variant* v, size_t count)
{
    TR_ASSERT(tr_variantIsContainer(v));

    size_t const needed = v->val.l.count + count;

    if (needed > v->val.l.alloc)
    {
        /* scale the alloc size in powers-of-2 */
        size_t n = v->val.l.alloc != 0 ? v->val.l.alloc : 8;

        while (n < needed)
        {
            n *= 2U;
        }

        v->val.l.vals = tr_renew(tr_variant, v->val.l.vals, n);
        v->val.l.alloc = n;
    }
}

void tr_variantListReserve(tr_variant* list, size_t count)
{
    TR_ASSERT(tr_variantIsList(list));

    containerReserve(list, count);
}

void tr_variantInitDict(tr_variant* v, size_t reserve_count)
{
    tr_variantInit(v, TR_VARIANT_TYPE_DICT);
    tr_variantDictReserve(v, reserve_count);
}

void tr_variantDictReserve(tr_variant* dict, size_t reserve_count)
{
    TR_ASSERT(tr_variantIsDict(dict));

    containerReserve(dict, reserve_count);
}

tr_variant* tr_variantListAdd(tr_variant* list)
{
    TR_ASSERT(tr_variantIsList(list));

    containerReserve(list, 1);

    tr_variant* child = &list->val.l.vals[list->val.l.count++];
    child->key = 0;
    tr_variantInit(child, TR_VARIANT_TYPE_INT);

    return child;
}

tr_variant* tr_variantListAddInt(tr_variant* list, int64_t val)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitInt(child, val);
    return child;
}

tr_variant* tr_variantListAddReal(tr_variant* list, double val)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitReal(child, val);
    return child;
}

tr_variant* tr_variantListAddBool(tr_variant* list, bool val)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitBool(child, val);
    return child;
}

tr_variant* tr_variantListAddStr(tr_variant* list, char const* val)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitStr(child, val, TR_BAD_SIZE);
    return child;
}

tr_variant* tr_variantListAddQuark(tr_variant* list, tr_quark const val)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitQuark(child, val);
    return child;
}

tr_variant* tr_variantListAddRaw(tr_variant* list, void const* val, size_t len)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitRaw(child, val, len);
    return child;
}

tr_variant* tr_variantListAddList(tr_variant* list, size_t reserve_count)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitList(child, reserve_count);
    return child;
}

tr_variant* tr_variantListAddDict(tr_variant* list, size_t reserve_count)
{
    tr_variant* child = tr_variantListAdd(list);
    tr_variantInitDict(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictAdd(tr_variant* dict, tr_quark const key)
{
    TR_ASSERT(tr_variantIsDict(dict));

    containerReserve(dict, 1);

    tr_variant* val = dict->val.l.vals + dict->val.l.count++;
    tr_variantInit(val, TR_VARIANT_TYPE_INT);
    val->key = key;

    return val;
}

static tr_variant* dictFindOrAdd(tr_variant* dict, tr_quark const key, int type)
{
    tr_variant* child;

    /* see if it already exists, and if so, try to reuse it */
    if ((child = tr_variantDictFind(dict, key)) != NULL)
    {
        if (!tr_variantIsType(child, type))
        {
            tr_variantDictRemove(dict, key);
            child = NULL;
        }
        else if (child->type == TR_VARIANT_TYPE_STR)
        {
            tr_variant_string_clear(&child->val.s);
        }
    }

    /* if it doesn't exist, create it */
    if (child == NULL)
    {
        child = tr_variantDictAdd(dict, key);
    }

    return child;
}

tr_variant* tr_variantDictAddInt(tr_variant* dict, tr_quark const key, int64_t val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_INT);
    tr_variantInitInt(child, val);
    return child;
}

tr_variant* tr_variantDictAddBool(tr_variant* dict, tr_quark const key, bool val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_BOOL);
    tr_variantInitBool(child, val);
    return child;
}

tr_variant* tr_variantDictAddReal(tr_variant* dict, tr_quark const key, double val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_REAL);
    tr_variantInitReal(child, val);
    return child;
}

tr_variant* tr_variantDictAddQuark(tr_variant* dict, tr_quark const key, tr_quark const val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitQuark(child, val);
    return child;
}

tr_variant* tr_variantDictAddStr(tr_variant* dict, tr_quark const key, char const* val)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitStr(child, val, TR_BAD_SIZE);
    return child;
}

tr_variant* tr_variantDictAddRaw(tr_variant* dict, tr_quark const key, void const* src, size_t len)
{
    tr_variant* child = dictFindOrAdd(dict, key, TR_VARIANT_TYPE_STR);
    tr_variantInitRaw(child, src, len);
    return child;
}

tr_variant* tr_variantDictAddList(tr_variant* dict, tr_quark const key, size_t reserve_count)
{
    tr_variant* child = tr_variantDictAdd(dict, key);
    tr_variantInitList(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictAddDict(tr_variant* dict, tr_quark const key, size_t reserve_count)
{
    tr_variant* child = tr_variantDictAdd(dict, key);
    tr_variantInitDict(child, reserve_count);
    return child;
}

tr_variant* tr_variantDictSteal(tr_variant* dict, tr_quark const key, tr_variant* value)
{
    tr_variant* child = tr_variantDictAdd(dict, key);
    *child = *value;
    child->key = key;
    tr_variantInit(value, value->type);
    return child;
}

bool tr_variantDictRemove(tr_variant* dict, tr_quark const key)
{
    bool removed = false;
    int const i = dictIndexOf(dict, key);

    if (i >= 0)
    {
        int const last = dict->val.l.count - 1;

        tr_variantFree(&dict->val.l.vals[i]);

        if (i != last)
        {
            dict->val.l.vals[i] = dict->val.l.vals[last];
        }

        --dict->val.l.count;

        removed = true;
    }

    return removed;
}

/***
****  BENC WALKING
***/

struct KeyIndex
{
    char const* keystr;
    tr_variant* val;
};

static int compareKeyIndex(void const* va, void const* vb)
{
    struct KeyIndex const* a = va;
    struct KeyIndex const* b = vb;

    return strcmp(a->keystr, b->keystr);
}

struct SaveNode
{
    tr_variant const* v;
    tr_variant* sorted;
    size_t childIndex;
    bool isVisited;
};

static void nodeConstruct(struct SaveNode* node, tr_variant const* v, bool sort_dicts)
{
    node->isVisited = false;
    node->childIndex = 0;

    if (sort_dicts && tr_variantIsDict(v))
    {
        /* make node->sorted a sorted version of this dictionary */

        size_t const n = v->val.l.count;
        struct KeyIndex* tmp = tr_new(struct KeyIndex, n);

        for (size_t i = 0; i < n; i++)
        {
            tmp[i].val = v->val.l.vals + i;
            tmp[i].keystr = tr_quark_get_string(tmp[i].val->key, NULL);
        }

        qsort(tmp, n, sizeof(struct KeyIndex), compareKeyIndex);

        node->sorted = tr_new(tr_variant, 1);
        tr_variantInitDict(node->sorted, n);

        for (size_t i = 0; i < n; ++i)
        {
            node->sorted->val.l.vals[i] = *tmp[i].val;
        }

        node->sorted->val.l.count = n;

        tr_free(tmp);

        v = node->sorted;
    }
    else
    {
        node->sorted = NULL;
    }

    node->v = v;
}

static void nodeDestruct(struct SaveNode* node)
{
    TR_ASSERT(node != NULL);

    if (node->sorted != NULL)
    {
        tr_free(node->sorted->val.l.vals);
        tr_free(node->sorted);
    }
}

/**
 * This function's previous recursive implementation was
 * easier to read, but was vulnerable to a smash-stacking
 * attack via maliciously-crafted data. (#667)
 */
void tr_variantWalk(tr_variant const* v, struct VariantWalkFuncs const* walkFuncs, void* user_data, bool sort_dicts)
{
    int stackSize = 0;
    int stackAlloc = 64;
    struct SaveNode* stack = tr_new(struct SaveNode, stackAlloc);

    nodeConstruct(&stack[stackSize++], v, sort_dicts);

    while (stackSize > 0)
    {
        struct SaveNode* node = &stack[stackSize - 1];
        tr_variant const* v;

        if (!node->isVisited)
        {
            v = node->v;
            node->isVisited = true;
        }
        else if (tr_variantIsContainer(node->v) && node->childIndex < node->v->val.l.count)
        {
            int const index = node->childIndex;
            ++node->childIndex;

            v = node->v->val.l.vals + index;

            if (tr_variantIsDict(node->v))
            {
                tr_variant tmp;
                tr_variantInitQuark(&tmp, v->key);
                walkFuncs->stringFunc(&tmp, user_data);
            }
        }
        else /* done with this node */
        {
            if (tr_variantIsContainer(node->v))
            {
                walkFuncs->containerEndFunc(node->v, user_data);
            }

            --stackSize;
            nodeDestruct(node);
            continue;
        }

        if (v != NULL)
        {
            switch (v->type)
            {
            case TR_VARIANT_TYPE_INT:
                walkFuncs->intFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_BOOL:
                walkFuncs->boolFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_REAL:
                walkFuncs->realFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_STR:
                walkFuncs->stringFunc(v, user_data);
                break;

            case TR_VARIANT_TYPE_LIST:
                if (v == node->v)
                {
                    walkFuncs->listBeginFunc(v, user_data);
                }
                else
                {
                    if (stackAlloc == stackSize)
                    {
                        stackAlloc *= 2;
                        stack = tr_renew(struct SaveNode, stack, stackAlloc);
                    }

                    nodeConstruct(&stack[stackSize++], v, sort_dicts);
                }

                break;

            case TR_VARIANT_TYPE_DICT:
                if (v == node->v)
                {
                    walkFuncs->dictBeginFunc(v, user_data);
                }
                else
                {
                    if (stackAlloc == stackSize)
                    {
                        stackAlloc *= 2;
                        stack = tr_renew(struct SaveNode, stack, stackAlloc);
                    }

                    nodeConstruct(&stack[stackSize++], v, sort_dicts);
                }

                break;

            default:
                /* did caller give us an uninitialized val? */
                tr_logAddError("%s", _("Invalid metadata"));
                break;
            }
        }
    }

    tr_free(stack);
}

/****
*****
****/

static void freeDummyFunc(tr_variant const* v UNUSED, void* buf UNUSED)
{
}

static void freeStringFunc(tr_variant const* v, void* unused UNUSED)
{
    tr_variant_string_clear(&((tr_variant*)v)->val.s);
}

static void freeContainerEndFunc(tr_variant const* v, void* unused UNUSED)
{
    tr_free(v->val.l.vals);
}

static struct VariantWalkFuncs const freeWalkFuncs =
{
    freeDummyFunc,
    freeDummyFunc,
    freeDummyFunc,
    freeStringFunc,
    freeDummyFunc,
    freeDummyFunc,
    freeContainerEndFunc
};

void tr_variantFree(tr_variant* v)
{
    if (tr_variantIsSomething(v))
    {
        tr_variantWalk(v, &freeWalkFuncs, NULL, false);
    }
}

/***
****
***/

static void tr_variantListCopy(tr_variant* target, tr_variant const* src)
{
    int i = 0;
    tr_variant const* val;

    while ((val = tr_variantListChild((tr_variant*)src, i)) != NULL)
    {
        if (tr_variantIsBool(val))
        {
            bool boolVal = false;
            tr_variantGetBool(val, &boolVal);
            tr_variantListAddBool(target, boolVal);
        }
        else if (tr_variantIsReal(val))
        {
            double realVal = 0;
            tr_variantGetReal(val, &realVal);
            tr_variantListAddReal(target, realVal);
        }
        else if (tr_variantIsInt(val))
        {
            int64_t intVal = 0;
            tr_variantGetInt(val, &intVal);
            tr_variantListAddInt(target, intVal);
        }
        else if (tr_variantIsString(val))
        {
            size_t len;
            char const* str;
            tr_variantGetStr(val, &str, &len);
            tr_variantListAddRaw(target, str, len);
        }
        else if (tr_variantIsDict(val))
        {
            tr_variantMergeDicts(tr_variantListAddDict(target, 0), val);
        }
        else if (tr_variantIsList(val))
        {
            tr_variantListCopy(tr_variantListAddList(target, 0), val);
        }
        else
        {
            tr_logAddError("tr_variantListCopy skipping item");
        }

        ++i;
    }
}

static size_t tr_variantDictSize(tr_variant const* dict)
{
    return tr_variantIsDict(dict) ? dict->val.l.count : 0;
}

bool tr_variantDictChild(tr_variant* dict, size_t n, tr_quark* key, tr_variant** val)
{
    TR_ASSERT(tr_variantIsDict(dict));

    bool success = false;

    if (tr_variantIsDict(dict) && n < dict->val.l.count)
    {
        *key = dict->val.l.vals[n].key;
        *val = dict->val.l.vals + n;
        success = true;
    }

    return success;
}

void tr_variantMergeDicts(tr_variant* target, tr_variant const* source)
{
    TR_ASSERT(tr_variantIsDict(target));
    TR_ASSERT(tr_variantIsDict(source));

    size_t const sourceCount = tr_variantDictSize(source);

    tr_variantDictReserve(target, sourceCount + tr_variantDictSize(target));

    for (size_t i = 0; i < sourceCount; ++i)
    {
        tr_quark key;
        tr_variant* val;
        tr_variant* t;

        if (tr_variantDictChild((tr_variant*)source, i, &key, &val))
        {
            if (tr_variantIsBool(val))
            {
                bool boolVal;
                tr_variantGetBool(val, &boolVal);
                tr_variantDictAddBool(target, key, boolVal);
            }
            else if (tr_variantIsReal(val))
            {
                double realVal = 0;
                tr_variantGetReal(val, &realVal);
                tr_variantDictAddReal(target, key, realVal);
            }
            else if (tr_variantIsInt(val))
            {
                int64_t intVal = 0;
                tr_variantGetInt(val, &intVal);
                tr_variantDictAddInt(target, key, intVal);
            }
            else if (tr_variantIsString(val))
            {
                size_t len;
                char const* str;
                tr_variantGetStr(val, &str, &len);
                tr_variantDictAddRaw(target, key, str, len);
            }
            else if (tr_variantIsDict(val) && tr_variantDictFindDict(target, key, &t))
            {
                tr_variantMergeDicts(t, val);
            }
            else if (tr_variantIsList(val))
            {
                if (tr_variantDictFind(target, key) == NULL)
                {
                    tr_variantListCopy(tr_variantDictAddList(target, key, tr_variantListSize(val)), val);
                }
            }
            else if (tr_variantIsDict(val))
            {
                tr_variant* target_dict = tr_variantDictFind(target, key);

                if (target_dict == NULL)
                {
                    target_dict = tr_variantDictAddDict(target, key, tr_variantDictSize(val));
                }

                if (tr_variantIsDict(target_dict))
                {
                    tr_variantMergeDicts(target_dict, val);
                }
            }
            else
            {
                tr_logAddDebug("tr_variantMergeDicts skipping \"%s\"", tr_quark_get_string(key, NULL));
            }
        }
    }
}

/***
****
***/

struct evbuffer* tr_variantToBuf(tr_variant const* v, tr_variant_fmt fmt)
{
    struct locale_context locale_ctx;
    struct evbuffer* buf = evbuffer_new();

    /* parse with LC_NUMERIC="C" to ensure a "." decimal separator */
    use_numeric_locale(&locale_ctx, "C");

    evbuffer_expand(buf, 4096); /* alloc a little memory to start off with */

    switch (fmt)
    {
    case TR_VARIANT_FMT_BENC:
        tr_variantToBufBenc(v, buf);
        break;

    case TR_VARIANT_FMT_JSON:
        tr_variantToBufJson(v, buf, false);
        break;

    case TR_VARIANT_FMT_JSON_LEAN:
        tr_variantToBufJson(v, buf, true);
        break;
    }

    /* restore the previous locale */
    restore_locale(&locale_ctx);
    return buf;
}

char* tr_variantToStr(tr_variant const* v, tr_variant_fmt fmt, size_t* len)
{
    struct evbuffer* buf = tr_variantToBuf(v, fmt);
    return evbuffer_free_to_str(buf, len);
}

int tr_variantToFile(tr_variant const* v, tr_variant_fmt fmt, char const* filename)
{
    char* tmp;
    tr_sys_file_t fd;
    int err = 0;
    char* real_filename;
    tr_error* error = NULL;

    /* follow symlinks to find the "real" file, to make sure the temporary
     * we build with tr_sys_file_open_temp() is created on the right partition */
    if ((real_filename = tr_sys_path_resolve(filename, NULL)) != NULL)
    {
        filename = real_filename;
    }

    /* if the file already exists, try to move it out of the way & keep it as a backup */
    tmp = tr_strdup_printf("%s.tmp.XXXXXX", filename);
    fd = tr_sys_file_open_temp(tmp, &error);

    if (fd != TR_BAD_SYS_FILE)
    {
        uint64_t nleft;

        /* save the variant to a temporary file */
        {
            struct evbuffer* buf = tr_variantToBuf(v, fmt);
            char const* walk = (char const*)evbuffer_pullup(buf, -1);
            nleft = evbuffer_get_length(buf);

            while (nleft > 0)
            {
                uint64_t n;

                if (!tr_sys_file_write(fd, walk, nleft, &n, &error))
                {
                    err = error->code;
                    break;
                }

                nleft -= n;
                walk += n;
            }

            evbuffer_free(buf);
        }

        tr_sys_file_close(fd, NULL);

        if (nleft > 0)
        {
            tr_logAddError(_("Couldn't save temporary file \"%1$s\": %2$s"), tmp, error->message);
            tr_sys_path_remove(tmp, NULL);
            tr_error_free(error);
        }
        else
        {
            tr_error_clear(&error);

            if (tr_sys_path_rename(tmp, filename, &error))
            {
                tr_logAddInfo(_("Saved \"%s\""), filename);
            }
            else
            {
                err = error->code;
                tr_logAddError(_("Couldn't save file \"%1$s\": %2$s"), filename, error->message);
                tr_sys_path_remove(tmp, NULL);
                tr_error_free(error);
            }
        }
    }
    else
    {
        err = error->code;
        tr_logAddError(_("Couldn't save temporary file \"%1$s\": %2$s"), tmp, error->message);
        tr_error_free(error);
    }

    tr_free(tmp);
    tr_free(real_filename);
    return err;
}

/***
****
***/

bool tr_variantFromFile(tr_variant* setme, tr_variant_fmt fmt, char const* filename, tr_error** error)
{
    bool ret = false;
    uint8_t* buf;
    size_t buflen;

    buf = tr_loadFile(filename, &buflen, error);

    if (buf != NULL)
    {
        if (tr_variantFromBuf(setme, fmt, buf, buflen, filename, NULL) == 0)
        {
            ret = true;
        }
        else
        {
            tr_error_set_literal(error, 0, _("Unable to parse file content"));
        }

        tr_free(buf);
    }

    return ret;
}

int tr_variantFromBuf(tr_variant* setme, tr_variant_fmt fmt, void const* buf, size_t buflen, char const* optional_source,
    char const** setme_end)
{
    int err;
    struct locale_context locale_ctx;

    /* parse with LC_NUMERIC="C" to ensure a "." decimal separator */
    use_numeric_locale(&locale_ctx, "C");

    switch (fmt)
    {
    case TR_VARIANT_FMT_JSON:
    case TR_VARIANT_FMT_JSON_LEAN:
        err = tr_jsonParse(optional_source, buf, buflen, setme, setme_end);
        break;

    default /* TR_VARIANT_FMT_BENC */:
        err = tr_variantParseBenc(buf, (char const*)buf + buflen, setme, setme_end);
        break;
    }

    /* restore the previous locale */
    restore_locale(&locale_ctx);
    return err;
}
