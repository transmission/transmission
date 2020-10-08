#!/usr/bin/env node

const copyright =
`/*
 * This file Copyright (C) ${new Date().getFullYear()} Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 */`;

const fs = require('fs');
const https = require('https');

// https://github.com/jshttp/mime-db
// > If you're crazy enough to use this in the browser, you can just grab
// > the JSON file using jsDelivr. It is recommended to replace master with
// > a release tag as the JSON format may change in the future.
//
// This script keeps it at `master` to pick up any fixes that may have landed.
// If the format changes, we'll just update this script.
const url = 'https://cdn.jsdelivr.net/gh/jshttp/mime-db@master/db.json';

https.get(url, (res) => {
  res.setEncoding('utf8');
  const chunks = [];
  res.on('data', (chunk) => chunks.push(chunk));
  res.on('end', () => {
    try {
      const suffixes = [];
      const mime_types = JSON.parse(chunks.join(''));
      for (const [mime_type, info] of Object.entries(mime_types)) {
        for (const suffix of info?.extensions || []) {
          suffixes.push([ suffix, mime_type ]);
        }
      }

      const max_suffix_len = suffixes
        .reduce((acc, [suffix]) => Math.max(acc, suffix.length), 0);

      const mime_type_lines = suffixes
        .map(([suffix, mime_type]) => `    { "${suffix}", "${mime_type}" }`)
        .sort()
        .join(',\n');
      fs.writeFileSync('mime-types.c', `${copyright}

#include "mime-types.h"

struct mime_type_suffix const mime_type_suffixes[MIME_TYPE_SUFFIX_COUNT] =
{
${mime_type_lines}
};
`);
      fs.writeFileSync('mime-types.h', `${copyright}

#pragma once

#define MIME_TYPE_SUFFIX_MAXLEN ${max_suffix_len}
#define MIME_TYPE_SUFFIX_COUNT ${suffixes.length}

struct mime_type_suffix
{
    char const* suffix;
    char const* mime_type;
};

extern struct mime_type_suffix const mime_type_suffixes[MIME_TYPE_SUFFIX_COUNT];
`);
    } catch (e) {
      console.error(e.message);
    }
  });
});
