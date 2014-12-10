/*
 * This file Copyright (C) 2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef TR_ERROR_TYPES_H
#define TR_ERROR_TYPES_H

#ifdef _WIN32

#include <windows.h>

#define TR_ERROR_IS_ENOSPC(code) ((code) == ERROR_DISK_FULL)

#define TR_ERROR_EINVAL ERROR_INVALID_PARAMETER

#else /* _WIN32 */

#include <errno.h>

#define TR_ERROR_IS_ENOSPC(code) ((code) == ENOSPC)

#define TR_ERROR_EINVAL EINVAL

#endif /* _WIN32 */

#endif /* TR_ERROR_TYPES_H */
