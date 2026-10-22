# Instructions for AI agents

These rules apply to any AI agent working in rac-editions, along with the
[README's rules](README.md#rules).

## Commits and pushes

- Commit finished, working changes locally, one logical change per commit.
- Never push, publish or release anything without being asked.
- Never rewrite history that has been pushed.
- Leave uncommitted changes you did not make alone, and commit only your own.

## Project rules

- **Saves stay retail-compatible.** A save made with any edition must load
  on the retail game. Retail code reads saved data as retail data, so keep
  edition-only values out of anything retail reads.
- **Done means tested on real saves.** Writing state into RAM is fine for a
  proof of concept, but a feature is done only once it works in-game on
  real save data. For a feature behind a condition (Challenge Mode, an item
  owned), also check it stays off on a save that does not meet it.
- **No legacy leftovers.** When an option, path or mechanism is renamed or
  replaced, remove the old one outright rather than keeping an alias, and
  update the docs and comments that name it in the same change. Dated
  records keep the names they had at the time.

## Writing: commit messages, docs and code comments

Write about the edition and the game, not about the work session.

- **State, not process.** Say what the product does, or for a fix, what was
  wrong and what it does now. Leave out how a bug was found, reproduced or
  narrowed down.
- **Direct description.** Name the behaviour itself: "In 16:9 mode, the
  countdown's backing disappears at some camera angles", not "Screenshots
  showed the backing disappearing".
- **Defects in the active voice.** "The boots slot accepts item 36, which
  crashes retail on load", not "It was observed that…" or "X found that…".
- **No meta-language.** No "the owner", "the user", "at X's request", "this
  commit", "as reported", "after investigating".
- **Changelogs** may say how an issue surfaced ("Screenshots showed…"), but
  never who reported it.

## Commit messages

Subject: `type(scope): summary`, for example
`fix(rac1-executive): countdowns no longer turn see-through over water`.

Body: clear, concise and useful to someone reading the history later.

- Lead with the problem, then what the change does. Two to four sentences
  are usually enough.
- Include the technical detail that explains the change: how the game does
  something, and why that caused the problem or makes the fix work. That is
  often the most valuable part. Leave out detail that only records the
  session: tool runs, audit counts, setup steps, dead ends.
- Mention testing in one line, stated as fact, only when it is useful
  ("Tested on Quartu in 16:9."). Say plainly when something is untested.
- A small, self-explanatory change needs no body.
- No `Co-Authored-By:` or other attribution lines.

Example:

```
fix(rac1-executive): draw the countdowns after the HUD

Blarg Station's, Quartu's and Drek's Fleet's countdowns could lose their
backing at some camera angles, and their digits turned see-through over
water. Retail draws them from a callback inside DrawWorld's 3D effects
pass, so they inherit the blending and depth test of whatever 3D was drawn
before them. The edition now draws them after the HUD instead, fit to 16:9.

Not yet tested at a countdown.
```

Not:

```
Retail draws the countdowns from a draw callback in DrawWorld's 3D effects
pass ... (shown with retail's own code in the owner's game). At the owner's
request the edition improves on it: the timer's registration names
ee_ws_timer_NN ... Verified: full pack.sh build on the merged OpenRAC
(after refreshing its generated asm/ with tools/setup_asm.sh; 1236
functions audited).
```

## Explaining the game

The edition's code and docs are also a record of how the games work, and
should help others understand and build on them. Explain the real game logic
rather than abstracting it away: name the game's functions and structures,
say what they do and why the edition hooks where it does. Aim for clarity
over brevity when the two conflict.

## Code style

The aim is code a reader understands quickly and correctly, with no loss
of speed. Style rules serve that aim: follow them by default, and know what
each one is for, so the same intent guides the cases no rule covers.

- **One statement per line**, so every step is visible and no `return` or
  error path hides at the end of a line.
- **Descriptive names**, so a reader need not remember what `h`, `p` or `st`
  stand for.
- **Lines up to 120 columns.** A line should read at a glance, but a table
  row, a struct layout or a message that explains the game often reads best
  whole. Wrap where the structure breaks naturally.
- **Named functions and a `main()`** rather than assigned lambdas and
  module-level script code: they show in tracebacks and can be reused.

Depart from a rule where following it would make that code harder to read:
aligned tables of addresses or offsets, a long message kept whole so it can
be searched for, an expression that mirrors the game's own formula. Prefer
restructuring to silencing a check, and give the reason next to any
`# noqa`. Tidy the code a change touches; leave the rest for its own change.

Python passes `ruff check` (the rules and the 120-column limit are in
[ruff.toml](ruff.toml)), and shell scripts pass
`shellcheck --severity=warning`. CI runs both; they are the floor, and
should rarely find anything.
