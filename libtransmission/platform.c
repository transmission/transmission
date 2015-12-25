/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#define _XOPEN_SOURCE 600  /* needed for recursive locks. */
#ifndef __USE_UNIX98
 #define __USE_UNIX98 /* some older Linuxes need it spelt out for them */
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#ifdef __HAIKU__
 #include <limits.h> /* PATH_MAX */
#endif

#ifdef _WIN32
 #include <process.h> /* _beginthreadex (), _endthreadex () */
 #include <windows.h>
 #include <shlobj.h> /* SHGetKnownFolderPath (), FOLDERID_... */
#else
#include <unistd.h> /* getuid() */
 #ifdef BUILD_MAC_CLIENT
  #include <CoreFoundation/CoreFoundation.h>
 #endif
 #ifdef __HAIKU__
  #include <FindDirectory.h>
 #endif
 #include <pthread.h>
#endif

#include "transmission.h"
#include "file.h"
#include "list.h"
#include "log.h"
#include "platform.h"
#include "session.h"
#include "utils.h"

/***
****  THREADS
***/

#ifdef _WIN32
typedef DWORD tr_thread_id;
#else
typedef pthread_t tr_thread_id;
#endif

static tr_thread_id
tr_getCurrentThread (void)
{
#ifdef _WIN32
  return GetCurrentThreadId ();
#else
  return pthread_self ();
#endif
}

static bool
tr_areThreadsEqual (tr_thread_id a, tr_thread_id b)
{
#ifdef _WIN32
  return a == b;
#else
  return pthread_equal (a, b) != 0;
#endif
}

/** @brief portability wrapper around OS-dependent threads */
struct tr_thread
{
  void          (* func)(void *);
  void           * arg;
  tr_thread_id     thread;
#ifdef _WIN32
  HANDLE           thread_handle;
#endif
};

bool
tr_amInThread (const tr_thread * t)
{
  return tr_areThreadsEqual (tr_getCurrentThread (), t->thread);
}

#ifdef _WIN32
 #define ThreadFuncReturnType unsigned WINAPI
#else
 #define ThreadFuncReturnType void
#endif

static ThreadFuncReturnType
ThreadFunc (void * _t)
{
  tr_thread * t = _t;

  t->func (t->arg);

  tr_free (t);
#ifdef _WIN32
  _endthreadex (0);
  return 0;
#endif
}

tr_thread *
tr_threadNew (void (*func)(void *), void * arg)
{
  tr_thread * t = tr_new0 (tr_thread, 1);

  t->func = func;
  t->arg  = arg;

#ifdef _WIN32
  {
    unsigned int id;
    t->thread_handle = (HANDLE) _beginthreadex (NULL, 0, &ThreadFunc, t, 0, &id);
    t->thread = (DWORD) id;
  }
#else
  pthread_create (&t->thread, NULL, (void* (*)(void*))ThreadFunc, t);
  pthread_detach (t->thread);
#endif

  return t;
}

/***
****  LOCKS
***/

/** @brief portability wrapper around OS-dependent thread mutexes */
struct tr_lock
{
  int                 depth;
#ifdef _WIN32
  CRITICAL_SECTION    lock;
  DWORD               lockThread;
#else
  pthread_mutex_t     lock;
  pthread_t           lockThread;
#endif
};

tr_lock*
tr_lockNew (void)
{
  tr_lock * l = tr_new0 (tr_lock, 1);

#ifdef _WIN32
  InitializeCriticalSection (&l->lock); /* supports recursion */
#else
  pthread_mutexattr_t attr;
  pthread_mutexattr_init (&attr);
  pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&l->lock, &attr);
#endif

  return l;
}

void
tr_lockFree (tr_lock * l)
{
#ifdef _WIN32
    DeleteCriticalSection (&l->lock);
#else
    pthread_mutex_destroy (&l->lock);
#endif
    tr_free (l);
}

void
tr_lockLock (tr_lock * l)
{
#ifdef _WIN32
  EnterCriticalSection (&l->lock);
#else
  pthread_mutex_lock (&l->lock);
#endif

  assert (l->depth >= 0);
  assert (!l->depth || tr_areThreadsEqual (l->lockThread, tr_getCurrentThread ()));
  l->lockThread = tr_getCurrentThread ();
  ++l->depth;
}

bool
tr_lockHave (const tr_lock * l)
{
  return (l->depth > 0) &&
         (tr_areThreadsEqual (l->lockThread, tr_getCurrentThread ()));
}

void
tr_lockUnlock (tr_lock * l)
{
  assert (l->depth > 0);
  assert (tr_areThreadsEqual (l->lockThread, tr_getCurrentThread ()));

  --l->depth;
  assert (l->depth >= 0);
#ifdef _WIN32
  LeaveCriticalSection (&l->lock);
#else
  pthread_mutex_unlock (&l->lock);
#endif
}

/***
****  PATHS
***/

#ifndef _WIN32
 #include <pwd.h>
#endif

#ifdef _WIN32

static char *
win32_get_known_folder (REFKNOWNFOLDERID folder_id)
{
  char * ret = NULL;
  PWSTR path;

  if (SHGetKnownFolderPath (folder_id, KF_FLAG_DONT_UNEXPAND | KF_FLAG_DONT_VERIFY,
                            NULL, &path) == S_OK)
    {
      ret = tr_win32_native_to_utf8 (path, -1);
      CoTaskMemFree (path);
    }

  return ret;
}

#endif

static const char *
getHomeDir (void)
{
  static char * home = NULL;

  if (!home)
    {
      home = tr_env_get_string ("HOME", NULL);

      if (!home)
        {
#ifdef _WIN32
          home = win32_get_known_folder (&FOLDERID_Profile);
#else
          struct passwd * pw = getpwuid (getuid ());
          if (pw)
            home = tr_strdup (pw->pw_dir);
          endpwent ();
#endif
        }

      if (!home)
        home = tr_strdup ("");
    }

  return home;
}

#if defined (__APPLE__) || defined (_WIN32)
 #define RESUME_SUBDIR  "Resume"
 #define TORRENT_SUBDIR "Torrents"
#else
 #define RESUME_SUBDIR  "resume"
 #define TORRENT_SUBDIR "torrents"
#endif

void
tr_setConfigDir (tr_session * session, const char * configDir)
{
  char * path;

  session->configDir = tr_strdup (configDir);

  path = tr_buildPath (configDir, RESUME_SUBDIR, NULL);
  tr_sys_dir_create (path, TR_SYS_DIR_CREATE_PARENTS, 0777, NULL);
  session->resumeDir = path;

  path = tr_buildPath (configDir, TORRENT_SUBDIR, NULL);
  tr_sys_dir_create (path, TR_SYS_DIR_CREATE_PARENTS, 0777, NULL);
  session->torrentDir = path;
}

const char *
tr_sessionGetConfigDir (const tr_session * session)
{
  return session->configDir;
}

const char *
tr_getTorrentDir (const tr_session * session)
{
  return session->torrentDir;
}

const char *
tr_getResumeDir (const tr_session * session)
{
  return session->resumeDir;
}

const char*
tr_getDefaultConfigDir (const char * appname)
{
  static char * s = NULL;

  if (!appname || !*appname)
    appname = "Transmission";

  if (!s)
    {
      s = tr_env_get_string ("TRANSMISSION_HOME", NULL);

      if (s == NULL)
        {
#ifdef __APPLE__
          s = tr_buildPath (getHomeDir (), "Library", "Application Support", appname, NULL);
#elif defined (_WIN32)
          char * appdata = win32_get_known_folder (&FOLDERID_LocalAppData);
          s = tr_buildPath (appdata, appname, NULL);
          tr_free (appdata);
#elif defined (__HAIKU__)
          char buf[PATH_MAX];
          find_directory (B_USER_SETTINGS_DIRECTORY, -1, true, buf, sizeof (buf));
          s = tr_buildPath (buf, appname, NULL);
#else
          char * const xdg_config_home = tr_env_get_string ("XDG_CONFIG_HOME", NULL);
          if (xdg_config_home != NULL)
            {
              s = tr_buildPath (xdg_config_home, appname, NULL);
              tr_free (xdg_config_home);
            }
          else
            {
              s = tr_buildPath (getHomeDir (), ".config", appname, NULL);
            }
#endif
        }
    }

  return s;
}

const char*
tr_getDefaultDownloadDir (void)
{
  static char * user_dir = NULL;

  if (user_dir == NULL)
    {
      char * config_home;
      char * config_file;
      char * content;
      size_t content_len;

      /* figure out where to look for user-dirs.dirs */
      config_home = tr_env_get_string ("XDG_CONFIG_HOME", NULL);
      if (config_home && *config_home)
        config_file = tr_buildPath (config_home, "user-dirs.dirs", NULL);
      else
        config_file = tr_buildPath (getHomeDir (), ".config", "user-dirs.dirs", NULL);
      tr_free (config_home);

      /* read in user-dirs.dirs and look for the download dir entry */
      content = (char *) tr_loadFile (config_file, &content_len, NULL);
      if (content && content_len>0)
        {
          const char * key = "XDG_DOWNLOAD_DIR=\"";
          char * line = strstr (content, key);
          if (line != NULL)
            {
              char * value = line + strlen (key);
              char * end = strchr (value, '"');

              if (end)
                {
                  *end = '\0';

                  if (!memcmp (value, "$HOME/", 6))
                    user_dir = tr_buildPath (getHomeDir (), value+6, NULL);
                  else if (!strcmp (value, "$HOME"))
                    user_dir = tr_strdup (getHomeDir ());
                  else
                    user_dir = tr_strdup (value);
                }
            }
        }

#ifdef _WIN32
      if (user_dir == NULL)
        user_dir = win32_get_known_folder (&FOLDERID_Downloads);
#endif

      if (user_dir == NULL)
#ifdef __HAIKU__
        user_dir = tr_buildPath (getHomeDir (), "Desktop", NULL);
#else
        user_dir = tr_buildPath (getHomeDir (), "Downloads", NULL);
#endif

      tr_free (content);
      tr_free (config_file);
    }

  return user_dir;
}

/***
****
***/

static bool
isWebClientDir (const char * path)
{
  char * tmp = tr_buildPath (path, "index.html", NULL);
  const bool ret = tr_sys_path_exists (tmp, NULL);
  tr_logAddInfo (_("Searching for web interface file \"%s\""), tmp);
  tr_free (tmp);

  return ret;
}

const char *
tr_getWebClientDir (const tr_session * session UNUSED)
{
  static char * s = NULL;

  if (!s)
    {
      s = tr_env_get_string ("CLUTCH_HOME", NULL);
      if (s == NULL)
        s = tr_env_get_string ("TRANSMISSION_WEB_HOME", NULL);
      if (s == NULL)
        {

#ifdef BUILD_MAC_CLIENT /* on Mac, look in the Application Support folder first, then in the app bundle. */

          /* Look in the Application Support folder */
          s = tr_buildPath (tr_sessionGetConfigDir (session), "web", NULL);

          if (!isWebClientDir (s))
            {
              tr_free (s);

              CFURLRef appURL = CFBundleCopyBundleURL (CFBundleGetMainBundle ());
              CFStringRef appRef = CFURLCopyFileSystemPath (appURL,
                                                            kCFURLPOSIXPathStyle);
              const CFIndex appStringLength = CFStringGetMaximumSizeOfFileSystemRepresentation (appRef);

              char * appString = tr_malloc (appStringLength);
              const bool success = CFStringGetFileSystemRepresentation (appRef, appString, appStringLength);
              assert (success);

              CFRelease (appURL);
              CFRelease (appRef);

              /* Fallback to the app bundle */
              s = tr_buildPath (appString, "Contents", "Resources", "web", NULL);
              if (!isWebClientDir (s))
                {
                  tr_free (s);
                  s = NULL;
                }

              tr_free (appString);
            }

#elif defined (_WIN32)

          /* Generally, Web interface should be stored in a Web subdir of
           * calling executable dir. */

          static REFKNOWNFOLDERID known_folder_ids[] =
            {
              &FOLDERID_LocalAppData,
              &FOLDERID_RoamingAppData,
              &FOLDERID_ProgramData
            };

          for (size_t i = 0; s == NULL && i < ARRAYSIZE (known_folder_ids); ++i)
            {
              char * dir = win32_get_known_folder (known_folder_ids[i]);
              s = tr_buildPath (dir, "Transmission", "Web", NULL);
              tr_free (dir);
              if (!isWebClientDir (s))
                {
                  tr_free (s);
                  s = NULL;
                }
            }

            if (s == NULL) /* check calling module place */
              {
                wchar_t wide_module_path[MAX_PATH];
                char * module_path;
                char * dir;
                GetModuleFileNameW (NULL, wide_module_path,
                                    sizeof (wide_module_path) / sizeof (*wide_module_path));
                module_path = tr_win32_native_to_utf8 (wide_module_path, -1);
                dir = tr_sys_path_dirname (module_path, NULL);
                tr_free (module_path);
                s = tr_buildPath (dir, "Web", NULL);
                tr_free (dir);
                if (!isWebClientDir (s))
                  {
                    tr_free (s);
                    s = NULL;
                  }
            }

#else /* everyone else, follow the XDG spec */

          tr_list *candidates = NULL, *l;
          char * tmp;

          /* XDG_DATA_HOME should be the first in the list of candidates */
          tmp = tr_env_get_string ("XDG_DATA_HOME", NULL);
          if (tmp && *tmp)
            {
              tr_list_append (&candidates, tmp);
            }
          else
            {
              char * dhome = tr_buildPath (getHomeDir (), ".local", "share", NULL);
              tr_list_append (&candidates, dhome);
              tr_free (tmp);
            }

          /* XDG_DATA_DIRS are the backup directories */
          {
            const char * pkg = PACKAGE_DATA_DIR;
            char * xdg = tr_env_get_string ("XDG_DATA_DIRS", NULL);
            const char * fallback = "/usr/local/share:/usr/share";
            char * buf = tr_strdup_printf ("%s:%s:%s", (pkg?pkg:""), (xdg?xdg:""), fallback);
            tr_free (xdg);
            tmp = buf;
            while (tmp && *tmp)
              {
                const char * end = strchr (tmp, ':');
                if (end)
                  {
                    if ((end - tmp) > 1)
                      tr_list_append (&candidates, tr_strndup (tmp, (size_t) (end - tmp)));
                    tmp = (char *) end + 1;
                  }
                else if (tmp && *tmp)
                  {
                    tr_list_append (&candidates, tr_strdup (tmp));
                    break;
                  }
              }
            tr_free (buf);
          }

          /* walk through the candidates & look for a match */
          for (l=candidates; l; l=l->next)
            {
              char * path = tr_buildPath (l->data, "transmission", "web", NULL);
              const int found = isWebClientDir (path);
              if (found)
                {
                  s = path;
                  break;
                }
              tr_free (path);
            }

          tr_list_free (&candidates, tr_free);

#endif

        }
    }

  return s;
}
