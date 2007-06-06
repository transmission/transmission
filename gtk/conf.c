/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "conf.h"
#include "util.h"

#define CONF_SUBDIR             "gtk"
#define FILE_LOCK               "lock"
#define FILE_SOCKET             "socket"
#define FILE_PREFS              "prefs"
#define FILE_PREFS_TMP          "prefs.tmp"
#define FILE_STATE              "state"
#define FILE_STATE_TMP          "state.tmp"
#define OLD_FILE_LOCK           "gtk_lock" /* remove this after next release */
#define OLD_FILE_PREFS          "gtk_prefs"
#define OLD_FILE_STATE          "gtk_state"
#define PREF_SEP_KEYVAL         '\t'
#define PREF_SEP_LINE           '\n'
#define STATE_SEP               '\n'

static int
lockfile(const char *file, char **errstr);
static void
cf_removelocks(void);
static char *
cf_readfile(const char *file, const char *oldfile, gsize *len,
            gboolean *usedold, char **errstr);
static void
cf_benc_append(benc_val_t *val, char type, int incsize);
static void
cf_writebenc(const char *file, const char *tmp, benc_val_t *data,
             char **errstr);
static gboolean
writefile_traverse(gpointer key, gpointer value, gpointer data);
static char *
getstateval(benc_val_t *state, char *line);

static char *gl_confdir = NULL;
static char *gl_old_confdir = NULL;
static GTree *gl_prefs = NULL;
static char *gl_lockpath = NULL;
static char *gl_old_lockpath = NULL;

/* errstr may be NULL, this might be called before GTK is initialized */
static int
lockfile(const char *file, char **errstr) {
  int fd, savederr;
  struct flock lk;

  if(NULL != errstr)
    *errstr = NULL;

  if(0 > (fd = open(file, O_RDWR | O_CREAT, 0666))) {
    savederr = errno;
    if(NULL != errstr)
      *errstr = g_strdup_printf(_("Failed to open the file %s for writing:\n%s"),
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
    if(NULL != errstr) {
      if(EAGAIN == errno)
        *errstr = g_strdup_printf(_("Another copy of %s is already running."),
                                  g_get_application_name());
      else
        *errstr = g_strdup_printf(_("Failed to lock the file %s:\n%s"),
                                  file, strerror(errno));
    }
    close(fd);
    errno = savederr;
    return -1;
  }

  return fd;
}

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_init(const char *dir, char **errstr) {
  if(NULL != errstr)
    *errstr = NULL;
  gl_old_confdir = g_strdup(dir);
  gl_confdir = g_build_filename(dir, CONF_SUBDIR, NULL);

  if(mkdir_p(gl_confdir, 0777))
    return TRUE;

  if(NULL != errstr)
    *errstr = g_strdup_printf(_("Failed to create the directory %s:\n%s"),
                              gl_confdir, strerror(errno));
  return FALSE;
}

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_lock(char **errstr) {
  char *path = g_build_filename(gl_old_confdir, OLD_FILE_LOCK, NULL);
  int fd = lockfile(path, errstr);

  if(0 > fd)
    g_free(path);
  else {
    gl_old_lockpath = path;
    path = g_build_filename(gl_confdir, FILE_LOCK, NULL);
    fd = lockfile(path, errstr);
    if(0 > fd)
      g_free(path);
    else
      gl_lockpath = path;
  }

  g_atexit(cf_removelocks);

  return 0 <= fd;
}

static void
cf_removelocks(void) {
  if(NULL != gl_lockpath) {
    unlink(gl_lockpath);
    g_free(gl_lockpath);
  }

  if(NULL != gl_old_lockpath) {
    unlink(gl_old_lockpath);
    g_free(gl_old_lockpath);
  }
}

char *
cf_sockname(void) {
  return g_build_filename(gl_confdir, FILE_SOCKET, NULL);
}

static char *
cf_readfile(const char *file, const char *oldfile, gsize *len,
            gboolean *usedold, char **errstr) {
  char *path;
  GIOChannel *io;
  GError *err;
  char *ret;

  *errstr = NULL;
  *usedold = FALSE;
  ret = NULL;
  err = NULL;
  *len = 0;

  path = g_build_filename(gl_confdir, file, NULL);
  io = g_io_channel_new_file(path, "r", &err);
  if(NULL != err && g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
    g_error_free(err);
    err = NULL;
    g_free(path);
    path = g_build_filename(gl_old_confdir, oldfile, NULL);
    io = g_io_channel_new_file(path, "r", &err);
    *usedold = TRUE;
  }
  if(NULL != err) {
    if(!g_error_matches(err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
      *errstr = g_strdup_printf(
        _("Failed to open the file %s for reading:\n%s"), path, err->message);
    goto done;
  }
  g_io_channel_set_encoding(io, NULL, NULL);

  if(G_IO_STATUS_ERROR == g_io_channel_read_to_end(io, &ret, len, &err)) {
    *errstr = g_strdup_printf(
      _("Error while reading from the file %s:\n%s"), path, err->message);
    goto done;
  }

 done:
  g_free (path);
  if(NULL != err)
    g_error_free(err);
  if(NULL != io)  
    g_io_channel_unref(io);
  return ret;
}

void
cf_loadprefs(char **errstr) {
  char *data, *line, *eol, *sep, *key;
  gsize len;
  benc_val_t val;
  gboolean usedold;
  int ii;

  *errstr = NULL;

  if(NULL != gl_prefs)
    g_tree_destroy(gl_prefs);

  gl_prefs = g_tree_new_full((GCompareDataFunc)g_ascii_strcasecmp, NULL,
                          g_free, g_free);

  data = cf_readfile(FILE_PREFS, OLD_FILE_PREFS, &len, &usedold, errstr);
  if(NULL != *errstr) {
    g_assert(NULL == data);
    return;
  }

  if(NULL == data)
    return;

  bzero(&val, sizeof(val));
  if(!usedold && !tr_bencLoad(data, len, &val, NULL)) {
    if(TYPE_DICT == val.type) {
      key = NULL;
      for(ii = 0; ii < val.val.l.count; ii++) {
        if(NULL == key) {
          g_assert(TYPE_STR == val.val.l.vals[ii].type);
          key = val.val.l.vals[ii].val.s.s;
        } else {
          if(TYPE_INT == val.val.l.vals[ii].type)
            g_tree_insert(gl_prefs, g_strdup(key),
                         g_strdup_printf("%"PRIu64, val.val.l.vals[ii].val.i));
          else if(TYPE_STR == val.val.l.vals[ii].type)
            g_tree_insert(gl_prefs, g_strdup(key),
                          g_strdup(val.val.l.vals[ii].val.s.s));
          key = NULL;
        }
      }
    }

  } else {
    /* XXX remove this in a release or two */
    for(line = data; NULL != (eol = strchr(line, PREF_SEP_LINE));
        line = eol + 1) {
      *eol = '\0';
      if(g_utf8_validate(line, -1, NULL) &&
         NULL != (sep = strchr(line, PREF_SEP_KEYVAL))) {
        *sep = '\0';
        g_tree_insert(gl_prefs, g_strcompress(line), g_strcompress(sep+1));
      }
    }
    cf_saveprefs(errstr);
  }

  tr_bencFree(&val);
  g_free(data);
}

benc_val_t *
cf_loadstate(char **errstr) {
  char *data, *line, *eol, *prog;
  gsize len;
  gboolean usedold;
  benc_val_t *state, *torstate;

  *errstr = NULL;

  data = cf_readfile(FILE_STATE, OLD_FILE_STATE, &len, &usedold, errstr);
  if(NULL != *errstr) {
    g_assert(NULL == data);
    return NULL;
  }

  if(NULL == data)
    return NULL;

  state = g_new0(benc_val_t, 1);
  if(usedold || tr_bencLoad(data, len, state, NULL)) {
    /* XXX all this evil compat code should go away at some point */
    tr_bencFree(state);
    bzero(state, sizeof(benc_val_t));
    state->type = TYPE_LIST;
    for(line = data; NULL != (eol = strchr(line, PREF_SEP_LINE));
        line = eol + 1) {
      *eol = '\0';
      if(g_utf8_validate(line, -1, NULL)) {
        cf_benc_append(state, TYPE_DICT, 10);
        torstate = state->val.l.vals + state->val.l.count - 1;
        prog = line;
        while(NULL != (prog = getstateval(torstate, prog)))
          ;
      }
    }
  }

  g_free(data);

  return state;
}

static void
cf_benc_append(benc_val_t *val, char type, int incsize) {
  if(++val->val.l.count > val->val.l.alloc) {
    val->val.l.alloc += incsize;
    val->val.l.vals = g_renew(benc_val_t, val->val.l.vals, val->val.l.alloc);
    bzero(val->val.l.vals + val->val.l.alloc - incsize,
          incsize * sizeof(benc_val_t));
  }
  val->val.l.vals[val->val.l.count-1].type = type;
}

const char *
cf_getpref(const char *name) {
  g_assert(NULL != gl_prefs);

  return g_tree_lookup(gl_prefs, name);
}

void
cf_setpref(const char *name, const char *value) {
  g_assert(NULL != gl_prefs);

  g_tree_insert(gl_prefs, g_strdup(name), g_strdup(value));
}

static void
cf_writebenc(const char *file, const char *tmp, benc_val_t *data,
             char **errstr) {
  char *path = g_build_filename(gl_confdir, file, NULL);
  char *pathtmp = g_build_filename(gl_confdir, tmp, NULL);
  GIOChannel *io = NULL;
  GError *err;
  char *datastr;
  int len;
  gsize written;

  *errstr = NULL;
  err = NULL;
  datastr = NULL;

  io = g_io_channel_new_file(pathtmp, "w", &err);
  if(NULL != err) {
    *errstr = g_strdup_printf(_("Failed to open the file %s for writing:\n%s"),
                              pathtmp, err->message);
    goto done;
  }
  g_io_channel_set_encoding(io, NULL, NULL);

  len = 0;
  datastr = tr_bencSaveMalloc(data, &len);

  written = 0;
  g_io_channel_write_chars(io, datastr, len, &written, &err);
  if(NULL != err)
    g_io_channel_flush(io, &err);
  if(NULL != err) {
    *errstr = g_strdup_printf(_("Error while writing to the file %s:\n%s"),
                              pathtmp, err->message);
    goto done;
  }

  if(0 > rename(pathtmp, path)) {
    *errstr = g_strdup_printf(_("Failed to rename the file %s to %s:\n%s"),
                              pathtmp, file, strerror(errno));
    goto done;
  }

 done:
  g_free(path);
  g_free(pathtmp);
  if(NULL != io)
    g_io_channel_unref(io);
  if(NULL != datastr)
    free(datastr);
}

void
cf_saveprefs(char **errstr) {
  benc_val_t val;
  benc_val_t *ptr;

  *errstr = NULL;

  bzero(&val, sizeof(val));
  val.type = TYPE_DICT;
  val.val.l.alloc = val.val.l.count = g_tree_nnodes(gl_prefs) * 2;
  val.val.l.vals = g_new0(benc_val_t, val.val.l.alloc);

  ptr = val.val.l.vals;
  g_tree_foreach(gl_prefs, writefile_traverse, &ptr);
  g_assert(ptr - val.val.l.vals == val.val.l.alloc);

  cf_writebenc(FILE_PREFS, FILE_PREFS_TMP, &val, errstr);
  tr_bencFree(&val);
}

static gboolean
writefile_traverse(gpointer key, gpointer value, gpointer data) {
  benc_val_t **ptr = data;
  benc_val_t *bkey = *ptr;
  benc_val_t *bval = (*ptr) + 1;

  *ptr = (*ptr) + 2;

  bkey->type = TYPE_STR;
  bkey->val.s.s = g_strdup(key);
  bkey->val.s.i = strlen(key);

  bval->type = TYPE_STR;
  bval->val.s.s = g_strdup(value);
  bval->val.s.i = strlen(value);

  return FALSE;
}

static char *
getstateval(benc_val_t *state, char *line) {
  char *start, *end;

  /* skip any leading whitespace */
  while(g_ascii_isspace(*line))
    line++;

  /* walk over the key, which may be alphanumerics as well as - or _ */
  for(start = line; g_ascii_isalnum(*start)
        || '_' == *start || '-' == *start; start++)
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
  if(0 == strcmp(line, "torrent") || 0 == strcmp(line, "dir") ||
     0 == strcmp(line, "paused")) {
    cf_benc_append(state, TYPE_STR, 6);
    state->val.l.vals[state->val.l.count-1].val.s.s = g_strdup(line);
    state->val.l.vals[state->val.l.count-1].val.s.i = strlen(line);
    if('p' == *line) {
      cf_benc_append(state, TYPE_INT, 6);
      state->val.l.vals[state->val.l.count-1].val.i = strbool(start);
    } else {
      cf_benc_append(state, TYPE_STR, 6);
      state->val.l.vals[state->val.l.count-1].val.s.s = g_strdup(start);
      state->val.l.vals[state->val.l.count-1].val.s.i = strlen(start);
    }
  }

  /* return a pointer to just past the end of the value */
  return end + 1;
}

void
cf_savestate(benc_val_t *state, char **errstr) {
  *errstr = NULL;
  cf_writebenc(FILE_STATE, FILE_STATE_TMP, state, errstr);
}

void
cf_freestate( benc_val_t * state )
{
    if( NULL != state )
    {
        tr_bencFree( state );
        g_free( state );
    }
}
