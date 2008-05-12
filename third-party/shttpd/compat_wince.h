
#ifndef INCLUDE_WINCE_COMPAT_H
#define INCLUDE_WINCE_COMPAT_H

#ifdef __cplusplus
extern "C" {
#endif

/*** ANSI C library ***/

/* Missing ANSI C definitions */

#define BUFSIZ 4096

#define ENOMEM ERROR_NOT_ENOUGH_MEMORY
#define EBADF ERROR_INVALID_HANDLE
#define EINVAL ERROR_INVALID_PARAMETER
#define ENOENT ERROR_FILE_NOT_FOUND
#define ERANGE ERROR_INSUFFICIENT_BUFFER
#define EINTR WSAEINTR

/*
 *	Because we need a per-thread errno, we define a function
 *	pointer that we can call to return a pointer to the errno
 *	for the current thread.  Then we define a macro for errno
 *	that dereferences this function's result.
 *
 *	This makes it syntactically just like the "real" errno.
 *
 *	Using a function pointer allows us to use a very fast
 *	function when there are no threads running and a slower
 *	function when there are multiple threads running.
 */
void __WinCE_Errno_New_Thread(int *Errno_Pointer);
void __WinCE_Errno_Thread_Exit(void);
extern int *(*__WinCE_Errno_Pointer_Function)(void);

#define	errno (*(*__WinCE_Errno_Pointer_Function)())

char *strerror(int errnum);

struct tm {
	int tm_sec;     /* seconds after the minute - [0,59] */
	int tm_min;     /* minutes after the hour - [0,59] */
	int tm_hour;    /* hours since midnight - [0,23] */
	int tm_mday;    /* day of the month - [1,31] */
	int tm_mon;     /* months since January - [0,11] */
	int tm_year;    /* years since 1900 */
	int tm_wday;    /* days since Sunday - [0,6] */
	int tm_yday;    /* days since January 1 - [0,365] */
	int tm_isdst;   /* daylight savings time flag */
};

struct tm *gmtime(const time_t *TimeP); /* for future use */
struct tm *localtime(const time_t *TimeP);
time_t mktime(struct tm *tm);
time_t time(time_t *TimeP);

size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *tim_p);

int _wrename(const wchar_t *oldname, const wchar_t *newname);
int _wremove(const wchar_t *filename);

/* Environment variables are not supported */
#define getenv(x) (NULL)

/* Redefine fileno so that it returns an integer */
#undef fileno
#define fileno(f) (int)_fileno(f)

/* Signals are not supported */
#define signal(num, handler) (0)
#define SIGTERM 0
#define SIGINT 0


/*** POSIX API ***/

/* Missing POSIX definitions */

#define FILENAME_MAX MAX_PATH

struct _stat {
	unsigned long st_size;
	unsigned long st_ino;
	int st_mode;
	unsigned long st_atime;
	unsigned long st_mtime;
	unsigned long st_ctime;
	unsigned short st_dev;
	unsigned short st_nlink;
	unsigned short st_uid;
	unsigned short st_gid;
};

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IEXEC  0000100
#define S_IWRITE 0000200 
#define S_IREAD  0000400

#define _S_IFDIR S_IFDIR	/* MSVCRT compatibilit */

int _fstat(int handle, struct _stat *buffer);
int _wstat(const wchar_t *path, struct _stat *buffer);

#define stat _stat	/* NOTE: applies to _stat() and also struct _stat */
#define fstat _fstat

#define	O_RDWR		(1<<0)
#define	O_RDONLY	(2<<0)
#define	O_WRONLY	(3<<0)
#define	O_MODE_MASK	(3<<0)
#define	O_TRUNC		(1<<2)
#define	O_EXCL		(1<<3)
#define	O_CREAT		(1<<4)
#define O_BINARY 0

int _wopen(const wchar_t *filename, int oflag, ...);
int _close(int handle);
int _write(int handle, const void *buffer, unsigned int count);
int _read(int handle, void *buffer, unsigned int count);
long _lseek(int handle, long offset, int origin);

#define close _close
#define write _write
#define read _read
#define lseek _lseek

/* WinCE has only a Unicode version of this function */
FILE *fdopen(int handle, const char *mode);

int _wmkdir(const wchar_t *dirname);

/* WinCE has no concept of current directory so we return a constant path */
wchar_t *_wgetcwd(wchar_t *buffer, int maxlen);

#define freopen(path, mode, stream) assert(0)

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_WINCE_COMPAT_H */
