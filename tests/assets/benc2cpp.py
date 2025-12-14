#!/usr/bin/env python3
#
# Created by GitHub Copilot (GPT-5.2 (Preview)).
#
# License: Same terms as Transmission itself (see COPYING). Transmission
# permits redistribution/modification under GNU GPLv2, GPLv3, or any future
# license endorsed by Mnemosyne LLC.
#
# Purpose:
# Convert a bencoded (benc) file into a C++ concatenated string-literal
# fragment that preserves the exact original bytes. Output is whitespace-only
# formatted for readability (4-space indentation), similar in spirit to
# pretty-printed JSON.
#
# Usage:
#   tests/assets/benc2cpp.py path/to/file.benc > out.cppfrag

from __future__ import annotations

import sys
from pathlib import Path


def bytes_to_cpp_string_literal(data: bytes) -> str:
    r"""Return a single C++ string literal token for arbitrary bytes.

    Uses normal (non-raw) string literals and emits \xNN for bytes that are not
    safe/pleasant as-is.
    """

    out = '"'
    prev_was_hex_escape = False
    for b in data:
        ch = chr(b)

        # C/C++ rule: \x escapes consume *all following hex digits*.
        # If we emit "\xNN" and then a literal '0'..'9'/'a'..'f'/'A'..'F',
        # it becomes a single (larger) hex escape and may fail to compile.
        if (
            prev_was_hex_escape
            and (
                (ord('0') <= b <= ord('9'))
                or (ord('a') <= b <= ord('f'))
                or (ord('A') <= b <= ord('F'))
            )
        ):
            out += f"\\x{b:02x}"
            prev_was_hex_escape = True
            continue

        if ch == "\\":
            out += r"\\\\"
            prev_was_hex_escape = False
        elif ch == '"':
            out += r"\\\""
            prev_was_hex_escape = False
        elif 0x20 <= b <= 0x7E:
            out += ch
            prev_was_hex_escape = False
        else:
            out += f"\\x{b:02x}"
            prev_was_hex_escape = True
    out += '"'
    return out


def bencode_tokenize(data: bytes) -> list[bytes]:
    r"""Tokenize bencode into syntactic units without changing bytes.

    Tokens are:
    - b"d", b"l", b"e"
    - b"i...e" (entire integer token)
    - b"<len>:<payload>" (entire string token, including length and colon)

    This is a tokenizer only. It assumes the input is valid bencode.
    """

    tokens: list[bytes] = []
    i = 0
    n = len(data)

    def need(cond: bool, msg: str) -> None:
        if not cond:
            raise ValueError(f"Invalid bencode at offset {i}: {msg}")

    while i < n:
        b = data[i]

        if b in (ord('d'), ord('l'), ord('e')):
            tokens.append(bytes([b]))
            i += 1
            continue

        if b == ord('i'):
            j = data.find(b'e', i + 1)
            need(j != -1, "unterminated integer")
            tokens.append(data[i:j + 1])
            i = j + 1
            continue

        if ord('0') <= b <= ord('9'):
            j = i
            while j < n and ord('0') <= data[j] <= ord('9'):
                j += 1
            need(j < n and data[j] == ord(':'), "string length missing colon")
            strlen = int(data[i:j].decode('ascii'))
            start = j + 1
            end = start + strlen
            need(end <= n, "string payload truncated")
            tokens.append(data[i:end])
            i = end
            continue

        msg = f"Invalid bencode at offset {i}: unexpected byte 0x{b:02x}"
        raise ValueError(msg)

    return tokens


def render_bencode_tokens_pretty(
    tokens: list[bytes],
    *,
    base_indent: int = 4,
    indent_step: int = 4,
) -> list[str]:
    """Render bencode tokens into indented C++ string literal lines.

    Whitespace-only pretty-printing rules:
    - One token per line by default.
    - For dictionaries, if a key's value is a scalar (string or integer),
      render the key and value on the same line separated by a space.

    This changes only whitespace between C string fragments; the concatenated
    bytes are identical to the input.
    """

    lines: list[str] = []

    # Stack entries are either:
    #   ('list', None)
    #   ('dict', expecting_key: bool)
    stack: list[tuple[str, bool | None]] = []
    pending_dict_key: bytes | None = None

    def depth() -> int:
        return len(stack)

    def indent() -> str:
        return ' ' * (base_indent + depth() * indent_step)

    def is_scalar_token(t: bytes) -> bool:
        return t.startswith(b'i') or (t[:1].isdigit())

    i = 0
    while i < len(tokens):
        tok = tokens[i]

        if tok == b'e':
            if pending_dict_key is not None:
                key_lit = bytes_to_cpp_string_literal(pending_dict_key)
                lines.append(indent() + key_lit)
                pending_dict_key = None

            if stack:
                stack.pop()

            lines.append(indent() + bytes_to_cpp_string_literal(tok))

            # If this closed a value container in a dict,
            # the parent dict is now ready for next key.
            if stack and stack[-1][0] == 'dict' and stack[-1][1] is False:
                stack[-1] = ('dict', True)

            i += 1
            continue

        # Dict key collection
        if stack and stack[-1][0] == 'dict' and stack[-1][1] is True:
            pending_dict_key = tok
            stack[-1] = ('dict', False)
            i += 1
            continue

        # Dict value emission
        is_dict_value = (
            stack
            and stack[-1][0] == 'dict'
            and stack[-1][1] is False
            and pending_dict_key is not None
        )
        if is_dict_value:
            if is_scalar_token(tok):
                lines.append(
                    indent()
                    + bytes_to_cpp_string_literal(pending_dict_key)
                    + ' '
                    + bytes_to_cpp_string_literal(tok)
                )
                pending_dict_key = None
                stack[-1] = ('dict', True)
                i += 1
                continue

            # Non-scalar (container) value: key on its own line, then container
            # token.
            key_lit = bytes_to_cpp_string_literal(pending_dict_key)
            lines.append(indent() + key_lit)
            pending_dict_key = None

            lines.append(indent() + bytes_to_cpp_string_literal(tok))
            if tok == b'd':
                stack.append(('dict', True))
            elif tok == b'l':
                stack.append(('list', None))
            else:
                stack[-1] = ('dict', True)

            i += 1
            continue

        # Default emission
        lines.append(indent() + bytes_to_cpp_string_literal(tok))
        if tok == b'd':
            stack.append(('dict', True))
        elif tok == b'l':
            stack.append(('list', None))

        i += 1

    if pending_dict_key is not None:
        lines.append(indent() + bytes_to_cpp_string_literal(pending_dict_key))

    return lines


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(f"Usage: {Path(argv[0]).name} path/to/file.benc\n")
        return 2

    in_path = Path(argv[1])
    data = in_path.read_bytes()

    tokens = bencode_tokenize(data)
    pretty_lines = render_bencode_tokens_pretty(tokens)

    sys.stdout.write("// clang-format off\n")
    sys.stdout.write("constexpr std::string_view Benc =\n")
    if not pretty_lines:
        sys.stdout.write("    \"\";\n")
    else:
        for line in pretty_lines[:-1]:
            sys.stdout.write(line)
            sys.stdout.write("\n")
        sys.stdout.write(pretty_lines[-1])
        sys.stdout.write(";\n")
    sys.stdout.write("// clang-format on\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
