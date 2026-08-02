#!/usr/bin/env python3

import json
import os
import re
import sys


def main() -> int:
    database_path, pattern, root = sys.argv[1:]
    matcher = re.compile(pattern)
    with open(database_path, encoding="utf-8") as database_file:
        commands = json.load(database_file)

    seen = set()
    for command in commands:
        filename = command["file"]
        relative = os.path.relpath(filename, root).replace("\\", "/")
        if matcher.match(relative) and filename not in seen:
            seen.add(filename)
            print(filename)

    return 0


if __name__ == "__main__":
    sys.exit(main())
