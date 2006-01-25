/*
  Copyright (c) 2005-2006 Joshua Elsasser. All rights reserved.
   
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   
   1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
   
  THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS "AS IS" AND
  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/

#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "conf.h"
#include "transmission.h"
#include "util.h"

#define FILE_LOCK               "gtk_lock"
#define FILE_PREFS              "gtk_prefs"
#define FILE_PREFS_TMP          "gtk_prefs.tmp"
#define FILE_STATE              "gtk_state"
#define FILE_STATE_TMP          "gtk_state.tmp"
#define PREF_SEP_KEYVAL         '\t'
#define PREF_SEP_LINE           '\n'
#define STATE_SEP               '\n'

static int
lockfile(const char *file, char **errstr);
static gboolean
writefile_traverse(gpointer key, gpointer value, gpointer data);
static char *
getstateval(struct cf_torrentstate *state, char *line);

static char *confdir = NULL;
static GTree *prefs = NULL;

static int
lockfile(const char *file, char **errstr) {
  int fd, savederr;
  struct flock lk;

  *errstr = NULL;

  if(0 > (fd = open(file, O_RDWR | O_CREAT, 0666))) {
    savederr = errno;
    *errstr = g_strdup_printf("Error opening file %s for writing:\n%s",
                              file, strerror(errno));
    errno = savederr;
    return -1;
  }

  bzero(&lk, sizeof(lk));
  lk.l_start = 0;
  lk.l_len = 0;
  lk.l_type = F_WRLCK;
  lk.l_whence = SEEK_SET;
  if(-1 == fcntl(fd, F_SETLK, &lk)) {
    savederr = errno;
    if(EAGAIN == errno)
      *errstr = g_strdup_printf("Another copy of %s is already running.",
                                g_get_application_name());
    else
      *errstr = g_strdup_printf("Error obtaining lock on file %s:\n%s",
                                file, strerror(errno));
    close(fd);
    errno = savederr;
    return -1;
  }

  return fd;
}

gboolean
cf_init(const char *dir, char **errstr) {
  struct stat sb;

  *errstr = NULL;
  confdir = g_strdup(dir);

  if(0 > stat(dir, &sb)) {
    if(ENOENT != errno)
      *errstr = g_strdup_printf("Failed to check directory %s:\n%s",
                                dir, strerror(errno));
    else {
      if(0 == mkdir(dir, 0777))
        return TRUE;
      else
        *errstr = g_strdup_printf("Failed to create directory %s:\n%s",
                                  dir, strerror(errno));
    }
    return FALSE;
  }

  if(S_IFDIR & sb.st_mode)
    return TRUE;

  *errstr = g_strdup_printf("%s is not a directory", dir);
  return FALSE;
}

gboolean
cf_lock(char **errstr) {
  char *path = g_build_filename(confdir, FILE_LOCK, NULL);
  int fd = lockfile(path, errstr);

  g_free(path);
  return 0 <= fd;
}

gboolean
cf_loadprefs(char **errstr) {
  char *path = g_build_filename(confdir, FILE_PREFS, NULL);
  GIOChannel *io;
  GError *err;
  char *line, *sep;
  gsize len, termpos;
  char term = PREF_SEP_LINE;

  *errstr = NULL;

  if(NULL != prefs)
    g_tree_destroy(prefs);

  prefs = g_tree_new_full((GCompareDataFunc)g_ascii_strcasecmp, NULL,
                          g_free, g_free);

  err = NULL;
  io = g_io_channel_new_file(path, "r", &err);
  if(NULL != err) {
    if(!g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
      *errstr = g_strdup_printf("Error opening file %s for reading:\n%s",
                                path, err->message);
    goto done;
  }
  g_io_channel_set_line_term(io, &term, 1);

  err = NULL;
  for(;;) {
    assert(NULL == err) ;
    switch(g_io_channel_read_line(io, &line, &len, &termpos, &err)) {
      case G_IO_STATUS_ERROR:
        *errstr = g_strdup_printf("Error reading file %s:\n%s",
                                  path, err->message);
        goto done;
      case G_IO_STATUS_NORMAL:
        if(NULL != line) {
          if(NULL != (sep = strchr(line, PREF_SEP_KEYVAL)) && sep > line) {
            *sep = '\0';
            line[termpos] = '\0';
            g_tree_insert(prefs, g_strcompress(line), g_strcompress(sep + 1));
          }
          g_free(line);
        }
        break;
      case G_IO_STATUS_EOF:
        goto done;
      default:
        assert(!"unknown return code");
        goto done;
    }
  }

 done:
  if(NULL != err)
    g_error_free(err);
  if(NULL != io)  
    g_io_channel_unref(io);
  return NULL == *errstr;
}

const char *
cf_getpref(const char *name) {
  assert(NULL != prefs);

  return g_tree_lookup(prefs, name);
}

void
cf_setpref(const char *name, const char *value) {
  assert(NULL != prefs);

  g_tree_insert(prefs, g_strdup(name), g_strdup(value));
}

struct writeinfo {
  GIOChannel *io;
  GError *err;
};

gboolean
cf_saveprefs(char **errstr) {
  char *file = g_build_filename(confdir, FILE_PREFS, NULL);
  char *tmpfile = g_build_filename(confdir, FILE_PREFS_TMP, NULL);
  GIOChannel *io = NULL;
  struct writeinfo info;
  int fd;

  assert(NULL != prefs);
  assert(NULL != errstr);

  *errstr = NULL;

  if(0 > (fd = lockfile(tmpfile, errstr))) {
    g_free(errstr);
    *errstr = g_strdup_printf("Error opening or locking file %s:\n%s",
                              tmpfile, strerror(errno));
    goto done;
  }

#ifdef NDEBUG
  ftruncate(fd, 0);
#else
  assert(0 == ftruncate(fd, 0));
#endif

  info.err = NULL;
  io = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(io, TRUE);

  info.io = io;
  info.err = NULL;
  g_tree_foreach(prefs, writefile_traverse, &info);
  if(NULL != info.err ||
     G_IO_STATUS_ERROR == g_io_channel_shutdown(io, TRUE, &info.err)) {
    *errstr = g_strdup_printf("Error writing to file %s:\n%s",
                              tmpfile, info.err->message);
    g_error_free(info.err);
    goto done;
  }

  if(0 > rename(tmpfile, file)) {
    *errstr = g_strdup_printf("Error renaming %s to %s:\n%s",
                              tmpfile, file, strerror(errno));
    goto done;
  }

 done:
  g_free(file);
  g_free(tmpfile);
  if(NULL != io)
    g_io_channel_unref(io);

  return NULL == *errstr;
}

static gboolean
writefile_traverse(gpointer key, gpointer value, gpointer data) {
  struct writeinfo *info = data;
  char *ekey, *eval, *line;
  char sep[2];
  int len;

  ekey = g_strescape(key, NULL);
  eval = g_strescape(value, NULL);
  sep[0] = PREF_SEP_KEYVAL;
  sep[1] = '\0';
  line = g_strjoin(sep, ekey, eval, NULL);
  len = strlen(line);
  line[len] = PREF_SEP_LINE;

  switch(g_io_channel_write_chars(info->io, line, len + 1, NULL, &info->err)) {
    case G_IO_STATUS_ERROR:
      goto done;
    case G_IO_STATUS_NORMAL:
      break;
    default:
      assert(!"unknown return code");
      goto done;
  }

 done:
  g_free(ekey);
  g_free(eval);
  g_free(line);
  return NULL != info->err;
}

GList *
cf_loadstate(char **errstr) {
  char *path = g_build_filename(confdir, FILE_STATE, NULL);
  GIOChannel *io;
  GError *err;
  char term = STATE_SEP;
  GList *ret = NULL;
  char *line, *ptr;
  gsize len, termpos;
  struct cf_torrentstate *ts;

  err = NULL;
  io = g_io_channel_new_file(path, "r", &err);
  if(NULL != err) {
    if(!g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
      *errstr = g_strdup_printf("Error opening file %s for reading:\n%s",
                                path, err->message);
    goto done;
  }
  g_io_channel_set_line_term(io, &term, 1);

  err = NULL;
  for(;;) {
    assert(NULL == err);
    switch(g_io_channel_read_line(io, &line, &len, &termpos, &err)) {
      case G_IO_STATUS_ERROR:
        *errstr = g_strdup_printf("Error reading file %s:\n%s",
                                  path, err->message);
        goto done;
      case G_IO_STATUS_NORMAL:
        if(NULL != line) {
          ts = g_new0(struct cf_torrentstate, 1);
          ptr = line;
          while(NULL != (ptr = getstateval(ts, ptr)))
            ;
          g_free(line);
          if(NULL != ts->ts_torrent && NULL != ts->ts_directory)
            ret = g_list_append(ret, ts);
          else
            cf_freestate(ts);
        }
        break;
      case G_IO_STATUS_EOF:
        goto done;
      default:
        assert(!"unknown return code");
        goto done;
    }
  }

 done:
  if(NULL != err)
    g_error_free(err);
  if(NULL != io)  
    g_io_channel_unref(io);
  if(NULL != *errstr && NULL != ret) {
    g_list_foreach(ret, (GFunc)g_free, NULL);
    g_list_free(ret);
    ret = NULL;
  }
  return ret;
}

static char *
getstateval(struct cf_torrentstate *state, char *line) {
  char *start, *end;

  /* skip any leading whitespace */
  while(isspace(*line))
    line++;

  /* walk over the key, which may be alphanumerics as well as - or _ */
  for(start = line; isalnum(*start) || '_' == *start || '-' == *start; start++)
    ;

  /* they key must be immediately followed by an = */
  if('=' != *start)
    return NULL;
  *(start++) = '\0';

  /* then the opening quote for the value */
  if('"' != *(start++))
    return NULL;

  /* walk over the value */
  for(end = start; '\0' != *end && '"' != *end; end++)
    /* skip over escaped quotes */
    if('\\' == *end && '\0' != *(end + 1))
      end++;

  /* make sure we didn't hit the end of the string */
  if('"' != *end)
    return NULL;
  *end = '\0';

  /* if it's a key we recognize then save the data */
  if(0 == strcmp(line, "torrent"))
    state->ts_torrent = g_strcompress(start);
  else if(0 == strcmp(line, "dir"))
    state->ts_directory = g_strcompress(start);
  else if(0 == strcmp(line, "paused"))
    state->ts_paused = strbool(start);

  /* return a pointer to just past the end of the value */
  return end + 1;
}

gboolean
cf_savestate(int count, tr_stat_t *torrents, char **errstr) {
  char *file = g_build_filename(confdir, FILE_STATE, NULL);
  char *tmpfile = g_build_filename(confdir, FILE_STATE_TMP, NULL);
  GIOChannel *io = NULL;
  GError *err;
  int fd, ii;
  char *torrentfile, *torrentdir, *line;
  gsize written;
  gboolean paused;
  GIOStatus res;

  *errstr = NULL;

  if(0 > (fd = lockfile(tmpfile, errstr))) {
    g_free(errstr);
    *errstr = g_strdup_printf("Error opening or locking file %s:\n%s",
                              tmpfile, strerror(errno));
    goto done;
  }

#ifdef NDEBUG
  ftruncate(fd, 0);
#else
  assert(0 == ftruncate(fd, 0));
#endif

  io = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref(io, TRUE);

  /* XXX what the hell should I be doing about unicode? */

  err = NULL;
  for(ii = 0; ii < count; ii++) {
    /* XXX need a better way to query running/stopped state */
    paused = ((TR_STATUS_STOPPING | TR_STATUS_PAUSE) & torrents[ii].status);
    torrentfile = g_strescape(torrents[ii].info.torrent, "");
    torrentdir = g_strescape(torrents[ii].folder, "");
    /* g_strcompress */
    line = g_strdup_printf("torrent=\"%s\" dir=\"%s\" paused=\"%s\"%c",
                           torrentfile, torrentdir, (paused ? "yes" : "no"),
                           STATE_SEP);
    res = g_io_channel_write_chars(io, line, strlen(line), &written, &err);
    g_free(torrentfile);
    g_free(torrentdir);
    g_free(line);
    switch(res) {
      case G_IO_STATUS_ERROR:
        goto done;
      case G_IO_STATUS_NORMAL:
        break;
      default:
        assert(!"unknown return code");
        goto done;
    }
  }
  if(NULL != err ||
     G_IO_STATUS_ERROR == g_io_channel_shutdown(io, TRUE, &err)) {
    *errstr = g_strdup_printf("Error writing to file %s:\n%s",
                              tmpfile, err->message);
    g_error_free(err);
    goto done;
  }

  if(0 > rename(tmpfile, file)) {
    *errstr = g_strdup_printf("Error renaming %s to %s:\n%s",
                              tmpfile, file, strerror(errno));
    goto done;
  }

 done:
  g_free(file);
  g_free(tmpfile);
  if(NULL != io)
    g_io_channel_unref(io);

  return NULL == *errstr;
}

void
cf_freestate(struct cf_torrentstate *state) {
  if(NULL != state->ts_torrent)
    g_free(state->ts_torrent);
  if(NULL != state->ts_directory)
    g_free(state->ts_directory);
  g_free(state);
}
