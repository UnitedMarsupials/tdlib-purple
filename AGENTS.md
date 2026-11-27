# Notes for AI agents (and humans) working on this tree

Read `README.md` first, for what the plugin does, how it is built and how its
automated tests are run, and `GUITEST.md`, for the tests done by hand in a
real Pidgin. `CHANGELOG.md` says what each release changed, and why.

## What this is

A libpurple protocol plugin for Telegram, on TDLib. This repository,
UnitedMarsupials/tdlib-purple, is a fork of ars3niy's, where releases and
pull requests (Herrie82's among them) are made now.

The plugin is meant for any libpurple front-end: Pidgin, Finch, bitlbee,
Spectrum and others. What needs Pidgin's own window (reaction lines, menus of
a message, pasting and dropping files, telling which tabs are shown) is in
`pidgin-ui.cpp`, and must leave the rest working without it:

* Pidgin's and GTK's functions are not linked against; they are looked up at
  run time (`FRONT_END_FUNCTION` and `LOOK_UP` in `pidgin-ui.cpp`), and a
  feature whose functions are missing is left out, with a debug message.
* `-DNoPidgin=True` builds without Pidgin's headers altogether, and CI builds
  that too.

## How it runs

libpurple is single-threaded, on GLib's main loop. TDLib runs threads of its
own; `transceiver.cpp` polls it on one and hands every response and update to
the main loop with `g_idle_add`. Everything else runs on the main loop: do not
call libpurple, GLib's signals or GTK from anywhere else.

Some things about TDLib and Telegram that are easy to get wrong:

* TDLib's message ids are the server's shifted left by 20 bits: 13336838144
  is message 12719 of its chat, as links to messages have it.
* Reactions in channels and big groups are not pushed: TDLib polls them, for
  the last 25 messages it was told were viewed (`viewMessages`) in a chat that
  is open, and only while the account is online. Which messages are passed to
  `viewMessages`, and in what order, decides which counts stay up to date.
* A chat's history comes in pieces, of whatever size TDLib chooses; keep
  asking from the oldest message received.

## Testing

The automated tests (`test/`, Google Test) drive the plugin through mocks of
libpurple (`test/libpurple-mock.cpp`) and of TDLib's transceiver: every
request the plugin makes and every libpurple call it causes is checked, in
order. A new behaviour needs a test there; a libpurple function newly called
needs its mock. Run them under valgrind after touching memory handling.

The mocks cannot show what GTK, Pidgin and Telegram's servers do, and failures
have kept turning up that only a person at the GUI saw. Whenever such a
failure is found and fixed, add a test of it to `GUITEST.md`. When a change
touches what a test there names, say which of them should be done again.

Pidgin's debug log (`pidgin -d`) has the plugin's lines, as
`telegram-tdlib:`, and TDLib's, as `tdlib:`; most problems can be followed
there. It also holds messages, names and phone numbers: nothing of it goes
into an issue, a commit or a pull request without the private parts taken
out. Do not test in other people's chats: `GUITEST.md` says how to set up a
group for it.

## Code

* C++14 (`CMakeLists.txt`), four spaces, no TABs, camelCase; members of
  classes start with `m_`. Braces as in the code around: a function's on a
  line of its own.
* `nullptr`, not `NULL`; no `else` after `return` (or `continue`, `break`);
  unused parameters left unnamed, `[[maybe_unused]]` if used in some builds
  only. CI checks these with `.clang-tidy` (`clang-tidy -p <build> file.cpp`,
  on a build configured with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`).
* Comments say why, briefly, in plain sentences. Match the density and the
  idiom of the surrounding code.
* What the user sees is translated: wrap it in `_()` (`translate.h`), and
  update `po/` with `po/refresh-potfile.sh` when it changes. Do not tell
  Pidgin's own lines by their words, which it translates and may change:
  find a trait of their place or form instead. `g_dgettext` is one of the
  template's keywords, so a literal passed to it would join the template.
* Ask before adding a dependency, an option to the build or a new tool.

## Changelog, commits and pull requests

* Every change the user can notice gets an entry in `CHANGELOG.md`, under the
  release being prepared: a sentence in bold saying what is different now, in
  the user's terms, then what was wrong before or how it works. A
  contributor's change ends with "By **name**." Read a few entries before
  writing one.
* Commit subjects say what changed, in plain words, with no `[tag]:`
  prefixes; refer to other commits by subject, not by hash.
* Trailers (`Co-Authored-By:`, `Signed-off-by:` and the like) have exactly
  one TAB after the colon. An AI names its model and reasoning effort in
  `Co-Authored-By:`, with no e-mail address.
* A pull request is reviewed by CodeRabbit (ask with `@coderabbitai review`)
  and built by CI on Linux and the BSDs. Answer every finding: fix it, or say
  why not.
