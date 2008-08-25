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

static SERVICE_STATUS		ss; 
static SERVICE_STATUS_HANDLE	hStatus; 
static SERVICE_DESCRIPTION	service_descr = {"Web server"};

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
protect_against_code_disclosure(const wchar_t *path)
{
	WIN32_FIND_DATAW	data;
	HANDLE			handle;
	const wchar_t		*p;

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

	if ((handle = FindFirstFileW(path, &data)) == INVALID_HANDLE_VALUE)
		return (FALSE);

	FindClose(handle);

	for (p = path + wcslen(path); p > path && p[-1] != L'\\';)
		p--;
	
	if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
	    wcscmp(data.cFileName, p) != 0)
		return (FALSE);

	return (TRUE);
}

int
_shttpd_open(const char *path, int flags, int mode)
{
	char	buf[FILENAME_MAX];
	wchar_t	wbuf[FILENAME_MAX];

	_shttpd_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);
	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	if (protect_against_code_disclosure(wbuf) == FALSE)
		return (-1);

	return (_wopen(wbuf, flags));
}

int
_shttpd_stat(const char *path, struct stat *stp)
{
	char	buf[FILENAME_MAX], *p;
	wchar_t	wbuf[FILENAME_MAX];

	_shttpd_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);

	p = buf + strlen(buf) - 1;
	while (p > buf && *p == '\\' && p[-1] != ':')
		*p-- = '\0';

	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	return (_wstat(wbuf, (struct _stat *) stp));
}

int
_shttpd_remove(const char *path)
{
	char	buf[FILENAME_MAX];
	wchar_t	wbuf[FILENAME_MAX];

	_shttpd_strlcpy(buf, path, sizeof(buf));
	fix_directory_separators(buf);

	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, sizeof(wbuf));

	return (_wremove(wbuf));
}

int
_shttpd_rename(const char *path1, const char *path2)
{
	char	buf1[FILENAME_MAX];
	char	buf2[FILENAME_MAX];
	wchar_t	wbuf1[FILENAME_MAX];
	wchar_t	wbuf2[FILENAME_MAX];

	_shttpd_strlcpy(buf1, path1, sizeof(buf1));
	_shttpd_strlcpy(buf2, path2, sizeof(buf2));
	fix_directory_separators(buf1);
	fix_directory_separators(buf2);

	MultiByteToWideChar(CP_UTF8, 0, buf1, -1, wbuf1, sizeof(wbuf1));
	MultiByteToWideChar(CP_UTF8, 0, buf2, -1, wbuf2, sizeof(wbuf2));

	return (_wrename(wbuf1, wbuf2));
}

int
_shttpd_mkdir(const char *path, int mode)
{
	char	buf[FILENAME_MAX];
	wchar_t	wbuf[FILENAME_MAX];

	_shttpd_strlcpy(buf, path, sizeof(buf));
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
_shttpd_getcwd(char *buffer, int maxlen)
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
		_shttpd_snprintf(path, sizeof(path), "%s/*", name);
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
_shttpd_set_non_blocking_mode(int fd)
{
	unsigned long	on = 1;

	return (ioctlsocket(fd, FIONBIO, &on));
}

void
_shttpd_set_close_on_exec(int fd)
{
	fd = 0;	/* Do nothing. There is no FD_CLOEXEC on Windows */
}

#if !defined(NO_CGI)

struct threadparam {
	SOCKET	s;
	HANDLE	hPipe;
	big_int_t content_len;
};


enum ready_mode_t {IS_READY_FOR_READ, IS_READY_FOR_WRITE};

/*
 * Wait until given socket is in ready state. Always return TRUE.
 */
static int
is_socket_ready(int sock, enum ready_mode_t mode)
{
	fd_set		read_set, write_set;

	FD_ZERO(&read_set);
	FD_ZERO(&write_set);

	if (mode == IS_READY_FOR_READ)
		FD_SET(sock, &read_set);
	else
		FD_SET(sock, &write_set);

	select(sock + 1, &read_set, &write_set, NULL, NULL);

	return (TRUE);
}

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
	while (!stop &&
	    max_recv > 0 &&
	    is_socket_ready(tp->s, IS_READY_FOR_READ) &&
	    (n = recv(tp->s, buf, max_recv, 0)) > 0) {
		if (n == -1 && ERRNO == EWOULDBLOCK)
			continue;
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
		for (sent = 0; !stop && sent < n; sent += k) {
			if (is_socket_ready(tp->s, IS_READY_FOR_WRITE) &&
			    (k = send(tp->s, buf + sent, n - sent, 0)) <= 0) {
				if (k == -1 && ERRNO == EWOULDBLOCK) {
					k = 0;
					continue;
				}
				stop++;
			}
		}
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
_shttpd_spawn_process(struct conn *c, const char *prog, char *envblk,
		char *envp[], int sock, const char *dir)
{
	HANDLE	a[2], b[2], h[2], me;
	DWORD	flags;
	char	*p, *interp, cmdline[FILENAME_MAX], line[FILENAME_MAX];
	FILE	*fp;
	STARTUPINFOA		si;
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
	si.hStdOutput	= h[1];
	si.hStdInput	= h[0];

	/* If CGI file is a script, try to read the interpreter line */
	interp = c->ctx->options[OPT_CGI_INTERPRETER];
	if (interp == NULL) {
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
		interp = line + 2;
		(void) _shttpd_snprintf(cmdline, sizeof(cmdline), "%s%s%s",
		    line + 2, line[2] == '\0' ? "" : " ", prog);
	}

	if ((p = strrchr(prog, '/')) != NULL)
		prog = p + 1;

	(void) _shttpd_snprintf(cmdline, sizeof(cmdline), "%s %s", interp, prog);

	(void) _shttpd_snprintf(line, sizeof(line), "%s", dir);
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
		_shttpd_elog(E_LOG, c,
		    "redirect: CreateProcess(%s): %d", cmdline, ERRNO);
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

#define	ID_TRAYICON	100
#define	ID_QUIT		101
static NOTIFYICONDATA	ni;

static LRESULT CALLBACK
WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	POINT	pt;
	HMENU	hMenu; 	 

	switch (msg) {
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_QUIT:
			exit(EXIT_SUCCESS);
			break;
		}
		break;
	case WM_USER:
		switch (lParam) {
		case WM_RBUTTONUP:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
			hMenu = CreatePopupMenu();
			AppendMenu(hMenu, 0, ID_QUIT, "Exit SHTTPD");
			GetCursorPos(&pt);
			TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hWnd, NULL);
			DestroyMenu(hMenu);
			break;
		}
		break;
	}

	return (DefWindowProc(hWnd, msg, wParam, lParam));
}

static void
systray(void *arg)
{
	WNDCLASS	cls;
	HWND		hWnd;
	MSG		msg;

	(void) memset(&cls, 0, sizeof(cls));

	cls.lpfnWndProc = (WNDPROC) WindowProc; 
	cls.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	cls.lpszClassName = "shttpd v." VERSION; 

	if (!RegisterClass(&cls)) 
		_shttpd_elog(E_FATAL, NULL, "RegisterClass: %d", ERRNO);
	else if ((hWnd = CreateWindow(cls.lpszClassName, "",
	    WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL, NULL, arg)) == NULL)
		_shttpd_elog(E_FATAL, NULL, "CreateWindow: %d", ERRNO);
	ShowWindow(hWnd, SW_HIDE);
	
	ni.cbSize = sizeof(ni);
	ni.uID = ID_TRAYICON;
	ni.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	ni.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	ni.hWnd = hWnd;
	_shttpd_snprintf(ni.szTip, sizeof(ni.szTip), "SHTTPD web server");
	ni.uCallbackMessage = WM_USER;
	Shell_NotifyIcon(NIM_ADD, &ni);

	while (GetMessage(&msg, hWnd, 0, 0)) { 
		TranslateMessage(&msg); 
		DispatchMessage(&msg); 
	}
}

int
_shttpd_set_systray(struct shttpd_ctx *ctx, const char *opt)
{
	HWND		hWnd;
	char		title[512];
	static WNDPROC	oldproc;

	if (!_shttpd_is_true(opt))
		return (TRUE);

	FreeConsole();
	GetConsoleTitle(title, sizeof(title));
	hWnd = FindWindow(NULL, title);
	ShowWindow(hWnd, SW_HIDE);
	_beginthread(systray, 0, hWnd);

	return (TRUE);
}

int
_shttpd_set_nt_service(struct shttpd_ctx *ctx, const char *action)
{
	SC_HANDLE	hSCM, hService;
	char		path[FILENAME_MAX], key[128];
	HKEY		hKey;
	DWORD		dwData;


	if (!strcmp(action, "install")) {
		if ((hSCM = OpenSCManager(NULL, NULL,
		    SC_MANAGER_ALL_ACCESS)) == NULL)
			_shttpd_elog(E_FATAL, NULL, "Error opening SCM (%d)", ERRNO);

		GetModuleFileName(NULL, path, sizeof(path));

		hService = CreateService(hSCM, SERVICE_NAME, SERVICE_NAME,
		    SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
		    SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, path,
		    NULL, NULL, NULL, NULL, NULL);

		if (!hService)
			_shttpd_elog(E_FATAL, NULL,
			    "Error installing service (%d)", ERRNO);

		ChangeServiceConfig2(hService, SERVICE_CONFIG_DESCRIPTION,
		    &service_descr);
		_shttpd_elog(E_FATAL, NULL, "Service successfully installed");


	} else if (!strcmp(action, "uninstall")) {

		if ((hSCM = OpenSCManager(NULL, NULL,
		    SC_MANAGER_ALL_ACCESS)) == NULL) {
			_shttpd_elog(E_FATAL, NULL, "Error opening SCM (%d)", ERRNO);
		} else if ((hService = OpenService(hSCM,
		    SERVICE_NAME, DELETE)) == NULL) {
			_shttpd_elog(E_FATAL, NULL,
			    "Error opening service (%d)", ERRNO);
		} else if (!DeleteService(hService)) {
			_shttpd_elog(E_FATAL, NULL,
			    "Error deleting service (%d)", ERRNO);
		} else {
			_shttpd_elog(E_FATAL, NULL, "Service deleted");
		}

	} else {
		_shttpd_elog(E_FATAL, NULL, "Use -service <install|uninstall>");
	}

	/* NOTREACHED */
	return (TRUE);
}

static void WINAPI
ControlHandler(DWORD code) 
{ 
	if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
		ss.dwWin32ExitCode	= 0; 
		ss.dwCurrentState	= SERVICE_STOPPED; 
	} 
 
	SetServiceStatus(hStatus, &ss);
}

static void WINAPI
ServiceMain(int argc, char *argv[]) 
{
	char	path[MAX_PATH], *p, *av[] = {"shttpd_service", path, NULL};
	struct shttpd_ctx	*ctx;

	ss.dwServiceType      = SERVICE_WIN32; 
	ss.dwCurrentState     = SERVICE_RUNNING; 
	ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

	hStatus = RegisterServiceCtrlHandler(SERVICE_NAME, ControlHandler);
	SetServiceStatus(hStatus, &ss); 

	GetModuleFileName(NULL, path, sizeof(path));

	if ((p = strrchr(path, DIRSEP)) != NULL)
		*++p = '\0';

	strcat(path, CONFIG_FILE);	/* woo ! */

	ctx = shttpd_init(NELEMS(av) - 1, av);
	if ((ctx = shttpd_init(NELEMS(av) - 1, av)) == NULL)
		_shttpd_elog(E_FATAL, NULL, "Cannot initialize SHTTP context");

	while (ss.dwCurrentState == SERVICE_RUNNING)
		shttpd_poll(ctx, INT_MAX);
	shttpd_fini(ctx);

	ss.dwCurrentState  = SERVICE_STOPPED; 
	ss.dwWin32ExitCode = -1; 
	SetServiceStatus(hStatus, &ss); 
}

void
try_to_run_as_nt_service(void)
{
	static SERVICE_TABLE_ENTRY service_table[] = {
		{SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION) ServiceMain},
		{NULL, NULL}
	};

	if (StartServiceCtrlDispatcher(service_table))
		exit(EXIT_SUCCESS);
}
