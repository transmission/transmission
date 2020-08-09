#pragma once

#include <QtGlobal>

#if (QT_VERSION >= QT_VERSION_CHECK(5, 13, 0))
# define TR_DISABLE_MOVE(Class) \
    Q_DISABLE_MOVE(Class)
# define TR_DISABLE_COPY_MOVE(Class) \
    Q_DISABLE_COPY_MOVE(Class)
#else
# define TR_DISABLE_MOVE(Class) \
    Class(Class &&) = delete; \
    Class& operator =(Class &&) = delete;
# define TR_DISABLE_COPY_MOVE(Class) \
    Q_DISABLE_COPY(Class) \
    TR_DISABLE_MOVE(Class)
#endif
