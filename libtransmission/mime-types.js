#!/usr/bin/env node

const copyright =
`// This file Copyright © 2021-2023${new Date().getFullYear()} Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.`;

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

      const mime_type_lines = suffixes
        .map(([suffix, mime_type]) => `      { "${suffix}", "${mime_type}" }`)
        .sort()
        .join(',\n');
      fs.writeFileSync('mime-types.h', `${copyright}

#pragma once

#include <array>
#include <string_view>

struct mime_type_suffix
{
    std::string_view suffix;
    std::string_view mime_type;
};

inline auto constexpr mime_type_suffixes = std::array<mime_type_suffix, ${suffixes.length}>
{{
${mime_type_lines}
}};

`);
    } catch (e) {
      console.error(e.message);
    }
  });
});

