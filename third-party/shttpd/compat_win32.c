/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#include "defs.h"

static void
fix_directory_separators(char *path)
{
	for (; *path != '\0'; path++) {
		if (*path == '/')
			*path = '\\';
		if (*path == '\\')
			while (path[1] == '\\' || path[1] == '/') 
				(void) memmove(path + 1,
				    path + 2, strlen(path + 2) + 1);
	}
}

static int
protect_against_code_disclosure(const char *path)
{
	WIN32_FIND_DATA	data;
	HANDLE		handle;
	const char	*p;

	/*
	 * Protect against CGI code disclosure under Windows.
	 * This is very nasty hole. Windows happily opens files with
	 * some garbage in the end of file name. So fopen("a.cgi    ", "r")
	 * actually opens "a.cgi", and does not return an error! And since
	 * "a.cgi    " does not have valid CGI extension, this leads to
	 * the CGI code disclosure.
	 * To protect, here we delete all fishy characters from the
	 * end of file name.
	 */

	if ((handle = FindFirstFile(path, &data)) == INVALID_HANDLE_VALUE)
		return (FALSE);

	FindClose(handle);

	for (p = path + strlen(path); p > path && p[-1] != '\\';)
		p--;
	
	if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	    strcmp(data.cFileName, p) != 0)
		return (FALSE);

	return (TRUE);
}

int
my_open(const char *path, int flags, int mode)
{
	char	buf[FILENAME_MAX];
	wchar_t	wbuf[FILENAME_MAX];

	my_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);
	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	if (protect_against_code_disclosure(buf) == FALSE)
		return (-1);

	return (_wopen(wbuf, flags));
}

int
my_stat(const char *path, struct stat *stp)
{
	char	buf[FILENAME_MAX], *p;
	wchar_t	wbuf[FILENAME_MAX];

	my_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);

	p = buf + strlen(buf) - 1;
	while (p > buf && *p == '\\' && p[-1] != ':')
		*p-- = '\0';

	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	return (_wstat(wbuf, (struct _stat *) stp));
}

int
my_remove(const char *path)
{
	char	buf[FILENAME_MAX];
	wchar_t	wbuf[FILENAME_MAX];

	my_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);

	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	return (_wremove(wbuf));
}

int
my_rename(const char *path1, const char *path2)
{
	char	buf1[FILENAME_MAX];
	char	buf2[FILENAME_MAX];
	wchar_t	wbuf1[FILENAME_MAX];
	wchar_t	wbuf2[FILENAME_MAX];

	my_strlcpy(buf1, path1, sizeof(buf1));
	my_strlcpy(buf2, path2, sizeof(buf2));
	fix_directory_separators(buf1);
	fix_directory_separators(buf2);

	MultiByteToWideChar(CP_UTF8, 0, buf1, -1, wbuf1, sizeof(wbuf1));
	MultiByteToWideChar(CP_UTF8, 0, buf2, -1, wbuf2, sizeof(wbuf2));

	return (_wrename(wbuf1, wbuf2));
}

int
my_mkdir(const char *path, int mode)
{
	char	buf[FILENAME_MAX];
	wchar_t	wbuf[FILENAME_MAX];

	my_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);

	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	return (_wmkdir(wbuf));
}

static char *
wide_to_utf8(const wchar_t *str)
{
	char *buf = NULL;
	if (str) {
		int nchar = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
		if (nchar > 0) {
			buf = malloc(nchar);
			if (!buf)
				errno = ENOMEM;
			else if (!WideCharToMultiByte(CP_UTF8, 0, str, -1, buf, nchar, NULL, NULL)) {
				free(buf);
				buf = NULL;
				errno = EINVAL;
			}
		} else
			errno = EINVAL;
	} else
		errno = EINVAL;
	return buf;
}

char *
my_getcwd(char *buffer, int maxlen)
{
	char *result = NULL;
	wchar_t *wbuffer, *wresult;

	if (buffer) {
		/* User-supplied buffer */
		wbuffer = malloc(maxlen * sizeof(wchar_t));
		if (wbuffer == NULL)
			return NULL;
	} else
		/* Dynamically allocated buffer */
		wbuffer = NULL;
	wresult = _wgetcwd(wbuffer, maxlen);
	if (wresult) {
		int err = errno;
		if (buffer) {
			/* User-supplied buffer */
			int n = WideCharToMultiByte(CP_UTF8, 0, wresult, -1, buffer, maxlen, NULL, NULL);
			if (n == 0)
				err = ERANGE;
			free(wbuffer);
			result = buffer;
		} else {
			/* Buffer allocated by _wgetcwd() */
			result = wide_to_utf8(wresult);
			err = errno;
			free(wresult);
		}
		errno = err;
	}
	return result;
}

DIR *
opendir(const char *name)
{
	DIR		*dir = NULL;
	char		path[FILENAME_MAX];
	wchar_t		wpath[FILENAME_MAX];

	if (name == NULL || name[0] == '\0') {
		errno = EINVAL;
	} else if ((dir = malloc(sizeof(*dir))) == NULL) {
		errno = ENOMEM;
	} else {
		my_snprintf(path, sizeof(path), "%s/*", name);
		fix_directory_separators(path);
		MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, sizeof(wpath));
		dir->handle = FindFirstFileW(wpath, &dir->info);

		if (dir->handle != INVALID_HANDLE_VALUE) {
			dir->result.d_name[0] = '\0';
		} else {
			free(dir);
			dir = NULL;
		}
	}

	return (dir);
}

int
closedir(DIR *dir)
{
	int result = -1;

	if (dir != NULL) {
		if (dir->handle != INVALID_HANDLE_VALUE)
			result = FindClose(dir->handle) ? 0 : -1;

		free(dir);
	}

	if (result == -1) 
		errno = EBADF;

	return (result);
}

struct dirent *
readdir(DIR *dir)
{
	struct dirent *result = 0;

	if (dir && dir->handle != INVALID_HANDLE_VALUE) {
		if(!dir->result.d_name ||
		    FindNextFileW(dir->handle, &dir->info)) {
			result = &dir->result;

			WideCharToMultiByte(CP_UTF8, 0, dir->info.cFileName,
			    -1, result->d_name,
			    sizeof(result->d_name), NULL, NULL);
		}
	} else {
		errno = EBADF;
	}

	return (result);
}

int
set_non_blocking_mode(int fd)
{
	unsigned long	on = 1;

	return (ioctlsocket(fd, FIONBIO, &on));
}

void
set_close_on_exec(int fd)
{
	fd = 0;	/* Do nothing. There is no FD_CLOEXEC on Windows */
}

#if !defined(NO_CGI)

struct threadparam {
	SOCKET	s;
	HANDLE	hPipe;
	big_int_t content_len;
};

/*
 * Thread function that reads POST data from the socket pair
 * and writes it to the CGI process.
 */
static void//DWORD WINAPI
stdoutput(void *arg)
{
	struct threadparam	*tp = arg;
	int			n, sent, stop = 0;
	big_int_t		total = 0;
	DWORD k;
	char			buf[BUFSIZ];
	size_t			max_recv;

	max_recv = min(sizeof(buf), tp->content_len - total);
	while (!stop && max_recv > 0 && (n = recv(tp->s, buf, max_recv, 0)) > 0) {
		for (sent = 0; !stop && sent < n; sent += k)
			if (!WriteFile(tp->hPipe, buf + sent, n - sent, &k, 0))
				stop++;
		total += n;
		max_recv = min(sizeof(buf), tp->content_len - total);
	}
	
	CloseHandle(tp->hPipe);	/* Suppose we have POSTed everything */
	free(tp);
}

/*
 * Thread function that reads CGI output and pushes it to the socket pair.
 */
static void
stdinput(void *arg)
{
	struct threadparam	*tp = arg;
	static			int ntotal;
	int			k, stop = 0;
	DWORD n, sent;
	char			buf[BUFSIZ];

	while (!stop && ReadFile(tp->hPipe, buf, sizeof(buf), &n, NULL)) {
		ntotal += n;
		for (sent = 0; !stop && sent < n; sent += k)
			if ((k = send(tp->s, buf + sent, n - sent, 0)) <= 0)
				stop++;
	}
	CloseHandle(tp->hPipe);
	
	/*
	 * Windows is a piece of crap. When this thread closes its end
	 * of the socket pair, the other end (get_cgi() function) may loose
	 * some data. I presume, this happens if get_cgi() is not fast enough,
	 * and the data written by this end does not "push-ed" to the other
	 * end socket buffer. So after closesocket() the remaining data is
	 * gone. If I put shutdown() before closesocket(), that seems to
	 * fix the problem, but I am not sure this is the right fix.
	 * XXX (submitted by James Marshall) we do not do shutdown() on UNIX.
	 * If fork() is called from user callback, shutdown() messes up things.
	 */
	shutdown(tp->s, 2);

	closesocket(tp->s);
	free(tp);

	_endthread();
}

static void
spawn_stdio_thread(int sock, HANDLE hPipe, void (*func)(void *),
		big_int_t content_len)
{
	struct threadparam	*tp;
	DWORD			tid;

	tp = malloc(sizeof(*tp));
	assert(tp != NULL);

	tp->s		= sock;
	tp->hPipe	= hPipe;
	tp->content_len = content_len;
	_beginthread(func, 0, tp);
}

int
spawn_process(struct conn *c, const char *prog, char *envblk,
		char *envp[], int sock, const char *dir)
{
	HANDLE			a[2], b[2], h[2], me;
	DWORD			flags;
	char			*p, cmdline[FILENAME_MAX], line[FILENAME_MAX];
	FILE			*fp;
	STARTUPINFOA	si;
	PROCESS_INFORMATION	pi;

	me = GetCurrentProcess();
	flags = DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS;

	/* FIXME add error checking code here */
	CreatePipe(&a[0], &a[1], NULL, 0);
	CreatePipe(&b[0], &b[1], NULL, 0);
	DuplicateHandle(me, a[0], me, &h[0], 0, TRUE, flags);
	DuplicateHandle(me, b[1], me, &h[1], 0, TRUE, flags);
	
	(void) memset(&si, 0, sizeof(si));
	(void) memset(&pi, 0, sizeof(pi));

	/* XXX redirect CGI errors to the error log file */
	si.cb		= sizeof(si);
	si.dwFlags	= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow	= SW_HIDE;
	si.hStdOutput	= si.hStdError = h[1];
	si.hStdInput	= h[0];

	/* If CGI file is a script, try to read the interpreter line */
	if (c->ctx->options[OPT_CGI_INTERPRETER] == NULL) {
		if ((fp = fopen(prog, "r")) != NULL) {
			(void) fgets(line, sizeof(line), fp);
			if (memcmp(line, "#!", 2) != 0)
				line[2] = '\0';
			/* Trim whitespaces from interpreter name */
			for (p = &line[strlen(line) - 1]; p > line &&
			    isspace(*p); p--)
				*p = '\0';
			(void) fclose(fp);
		}
		(void) my_snprintf(cmdline, sizeof(cmdline), "%s%s%s",
		    line + 2, line[2] == '\0' ? "" : " ", prog);
	} else {
		(void) my_snprintf(cmdline, sizeof(cmdline), "%s %s",
		    c->ctx->options[OPT_CGI_INTERPRETER], prog);
	}

	(void) my_snprintf(line, sizeof(line), "%s", dir);
	fix_directory_separators(line);
	fix_directory_separators(cmdline);

	/*
	 * Spawn reader & writer threads before we create CGI process.
	 * Otherwise CGI process may die too quickly, loosing the data
	 */
	spawn_stdio_thread(sock, b[0], stdinput, 0);
	spawn_stdio_thread(sock, a[1], stdoutput, c->rem.content_len);

	if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
	    CREATE_NEW_PROCESS_GROUP, envblk, line, &si, &pi) == 0) {
		elog(E_LOG, c,"redirect: CreateProcess(%s): %d",cmdline,ERRNO);
		return (-1);
	} else {
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}

	return (0);
}

#endif /* !NO_CGI */
