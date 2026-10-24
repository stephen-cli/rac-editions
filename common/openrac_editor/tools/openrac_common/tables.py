# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
The repository's tab-separated tables, read the way every existing reader reads them.

  from openrac_common import tables
  rows = tables.read_tsv(path)                                  # lists of cells, as written
  rows = tables.read_tsv(path, strip=True)                      # each cell stripped
  rows = tables.read_tsv(path, ["name", "class", "kind"])       # dicts by column name
  names = tables.header(path)                                   # the "# a<TAB>b" header's names

The tables are port/game/*/libraries.tsv, no_c.tsv, split_places.tsv and fcheck.tsv, a
decompilation's functions.tsv, and the SDK's locks. Their conventions, and the quirks every
reader keeps, so a caller moved onto this module reads the same rows:

- A line that starts with "#" in its first column is a comment. A "#" after leading spaces is
  not: that line is data.
- A line that is empty or holds only whitespace is skipped.
- Cells are split on tabs alone. Spaces in a cell are kept unless STRIP is asked for.
- The column names are written in a comment line holding tabs (`# symbol<TAB>library<TAB>...`),
  read by header(); no_c.tsv instead writes them as its first uncommented line, which read_tsv
  with COLUMNS skips (port/tools/hostgen/no_c.py:163, keep_rows).
- A table that does not exist reads as no rows, unless REQUIRED.

Written from port/tools/versionmap/carry_functions.py:104 (read_tsv) and
port/tools/hostgen/no_c.py:63 (read_tsv_names); hostgen.py:234-282 (library_api, host_table,
wrapped) and sdk/tools/gen_binding.py:699 (hook_rows) read libraries.tsv by the same rules.
tools/fcheck_gate.py, port.py, xmap.py and versions.py read their tables through it (P1-05).
"""
from collections.abc import Sequence
from pathlib import Path

COMMENT = "#"
SEPARATOR = "\t"


def is_data(line: str) -> bool:
    """Whether LINE is a row: not blank, and not a comment ("#" in the first column)."""
    return bool(line.strip()) and not line.startswith(COMMENT)


def split_row(line: str, strip: bool = False) -> list[str]:
    """LINE's cells, split on tabs; each stripped of surrounding whitespace when STRIP."""
    cells = line.split(SEPARATOR)
    return [cell.strip() for cell in cells] if strip else cells


def read_tsv(path: Path | str, columns: Sequence[str] | None = None, *, strip: bool = False,
             required: bool = False) -> list[list[str]] | list[dict[str, str]]:
    """The rows of the table at PATH, in file order.

    Without COLUMNS, each row is the list of its cells, as many as the line has. With COLUMNS,
    each row is a dict from those names to its cells: a short row has "" for the columns it lacks,
    and cells past the last named column are left out. A first row of several cells whose first is
    COLUMNS[0] is the table's own header line (no_c.tsv's form) and is not returned. STRIP strips each cell.
    A missing file reads as no rows unless REQUIRED, which raises FileNotFoundError."""
    path = Path(path)
    if not path.is_file() and not required:
        return []
    lines = [line for line in path.read_text(encoding="utf-8").splitlines() if is_data(line)]
    rows = [split_row(line, strip) for line in lines]
    if columns is None:
        return rows
    # no_c.tsv's header line starts with its first column's name and a tab; keep_rows skips it so.
    if rows and len(rows[0]) > 1 and rows[0][0] == columns[0]:
        rows = rows[1:]
    return [{name: (row[i] if i < len(row) else "") for i, name in enumerate(columns)} for row in rows]


def header(path: Path | str) -> list[str]:
    """The column names of the table at PATH: the last comment line with a tab in it before the
    first row (`# symbol<TAB>library<TAB>name`), without its "#", each name stripped. [] when the
    table has no such line or does not exist."""
    path = Path(path)
    if not path.is_file():
        return []
    names: list[str] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if is_data(line):
            break
        if line.startswith(COMMENT) and SEPARATOR in line:
            names = split_row(line[len(COMMENT):], strip=True)
    return names
