#!/usr/bin/env node
import fs from 'node:fs';

const copyright =
  `// This file was generated with libtransmission/mime-types.js
// DO NOT EDIT MANUALLY

// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.`;

// https://github.com/jshttp/mime-db
// > If you're crazy enough to use this in the browser, you can just grab
// > the JSON file using jsDelivr. It is recommended to replace master with
// > a release tag as the JSON format may change in the future.
//
// This script keeps it at `master` to pick up any fixes that may have landed.
// If the format changes, we'll just update this script.
const url = 'https://cdn.jsdelivr.net/gh/jshttp/mime-db@master/db.json';

async function main() {
  const response = await fetch(url);
  const mime_types = await response.json()

  const extensions = Object.entries(mime_types)
    .reduce((acc, [mime_type, info]) => {
      const { extensions, ...rest } = info;
      for (const extension of extensions || [])
        acc.push({ mime_type, extension, info: rest });
      return acc;
    }, [])
    .sort((lhs, rhs) => {
      // Sort by extension
      const extension_order = lhs.extension.localeCompare(rhs.extension);
      if (extension_order !== 0)
        return extension_order;

      // Prefer entries that has a trusted source
      const lhs_has_source = lhs.info?.source ? 0 : 1;
      const rhs_has_source = rhs.info?.source ? 0 : 1;
      const has_source = lhs_has_source - rhs_has_source;
      if (has_source !== 0)
        return has_source;

      // Prefer a specific type over the generic octet stream
      const lhs_is_octet = lhs.mime_type === 'application/octet-stream' ? 1 : 0;
      const rhs_is_octet = rhs.mime_type === 'application/octet-stream' ? 1 : 0;
      const is_octet = lhs_is_octet - rhs_is_octet;
      if (is_octet !== 0)
        return is_octet;

      // Prefer iana source
      const lhs_is_iana = lhs.info?.source === 'iana' ? 0 : 1;
      const rhs_is_iana = rhs.info?.source === 'iana' ? 0 : 1;
      return lhs_is_iana - rhs_is_iana;
    })
    .filter(({ extension }, pos, arr) => pos === 0 || extension !== arr[pos - 1].extension);

  const mime_type_lines = extensions
    .map(({ extension, mime_type }) => `      { R"(${extension})", R"(${mime_type})" }`)
    .join(',\n')
    .trim();

  fs.writeFileSync('mime-types.h', `${copyright}

#pragma once

#include <array>
#include <string_view>

struct mime_type_suffix
{
    std::string_view suffix;
    std::string_view mime_type;
};

inline auto constexpr MimeTypeSuffixes = std::array<mime_type_suffix, ${extensions.length}>{
    { ${mime_type_lines} }
};
`);
}

main().catch(console.error);
