# Changelog

## 0.9.1 (2026-10-04)

### Fixes

- **A group chat you administer is no longer missing from the buddy list.** The same gap
  dropped every administrator from the member list of every group chat.
- **Joining a public group by its link now shows the group.**
- **Translations are found wherever the plugin is installed, and display correctly in
  locales that are not UTF-8.**

### Translations

- **Ukrainian**, complete.
- **Translations are maintained in this repository now**; upstream's Transifex project is
  not used.

### Build

- **CMake 4 can configure the build**, which it refused while the build asked for CMake 3.2.
- **Improved portability.**

### Tests

- **The test suite compiles and passes again**, against TDLib 1.8 — 128 tests, all green.
  It had not built since 1.7.9, and the two fixes above are bugs it found as soon as it ran.

## 0.9.0 (2026-09-27)

This is the first release from this fork. The upstream project,
[ars3niy/tdlib-purple](https://github.com/ars3niy/tdlib-purple), published 0.8.1 on
2021-12-17 and has not had a commit since; several good pull requests have been sitting
open there since 2022.

An enormous thank-you is owed to **Ben Wiederhake**, whose
[fork](https://github.com/BenWiederhake/tdlib-purple) kept this plugin alive through the
entire dormancy. Ben did not merely patch it — he acted as the project's de facto
maintainer, gathering, reviewing and carrying other people's work long after upstream
stopped answering. A large fraction of what is listed below reached us through his branch,
and where an item originated with someone else, Ben is the reason it survived at all.
This release would not exist without him.

### TDLib compatibility

Upstream 0.8.1 required *exactly* TDLib 1.7.9. This release tracks the modern API and
builds against 1.8.x, tested against **1.8.48**.

- Initial port to the TDLib 1.8 API. Written by **ludo** (upstream PR #154, which is still
  open and unanswered), and picked up and preserved by **Ben Wiederhake** on his branch,
  which is how it reached us.
- Adaptation to TDLib 1.8.48: message origin types, authorization state handling,
  `tdlibParameters`, reply-to field access, and error handling in
  `updateMessageSendFailed`.
- Outgoing file transfers migrated to the current API (`preliminaryUploadFile` and
  `cancelPreliminaryUploadFile`, replacing the removed `uploadFile` and
  `cancelUploadFile`). The implementation is **667bdrm**'s, from the TDLib 1.8.34 port —
  again carried and curated on **Ben Wiederhake**'s branch, and taken from there
  verbatim so that our two trees stay easy to reconcile. Hats off to both.

### Features

- **Self-destructing messages can now be displayed anyway**, controlled by the
  `show-self-destruct` account option (off by default). Authored by the ever-generous
  **Ben Wiederhake** — this is his own work, offered upstream as PR #159 in May 2022 and
  never merged there. Our thanks for both the feature and the patience.
- **Videos are shown with their thumbnail**, the preview image the sender's client attaches
  to a video, animation or round video message. The picture leads the message, with the
  link to the video directly beneath it and the caption below that; where there is no link
  yet, the notice about the download follows instead. That includes videos over the
  auto-download limit, which can now be recognised before deciding whether to download
  them. Each message waits briefly for its thumbnail, in the same queue that keeps messages
  in order, so a preview never lands below messages that arrived after it; a thumbnail that
  takes too long is skipped. Thumbnails in MPEG4 format, which some animations carry, are
  not shown. The picture is marked up as part of the link, but Pidgin 2.14 activates a
  link only from its text — an inline image is a widget of its own there — so for now the
  link beneath the picture is the thing to click.
- **Group chats you are not a member of are no longer added** to the buddy list, with a
  corresponding correction to the member-status check. Authored by **Björn Bidar**
  (upstream PR #163, likewise still open), and delivered to us — with our gratitude —
  by way of **Ben Wiederhake**'s branch.
- **Messages of unsupported types now show what they contain.** Below the usual
  "Unsupported message type" notice comes TDLib's own description of the message — a
  poll's question and options, a venue's address, a service action's text — shown
  verbatim in a monospace font, with empty fields and file-cache details left out and
  long descriptions cut short. It covers every type TDLib can deliver, including ones
  added after this release, without the plugin having to know them.

### Behaviour

- **History backlog fetching is capped at 80 messages** (was 10000). This stops an
  endless history-fetch when the plugin loses its position in a chat because the stored
  last-message id refers to a deleted message, and makes start-up markedly less spammy.
  Authored by **Mike Kazantsev**, and — once more, and with feeling — brought to us
  through **Ben Wiederhake**'s branch. Note this affects only the catch-up path; a first
  fetch of an unread chat remains capped at 100 messages, as it always was.

### Fixes

- **Files whose names contain spaces, non-ASCII letters or other characters a URI cannot
  carry literally can now be opened from the conversation window.** Such a file was saved
  correctly, but clicking its link produced *Failed to open URI* and a path full of
  `%25D0%25A1…%2520…`. The cause is on Pidgin's side: its `file://` link handler strips
  the scheme and passes the remainder to `purple_notify_uri()` as though it were a plain
  filesystem path, which is then percent-encoded a second time and handed to `xdg-open`,
  which — finding no scheme — encodes it once more. Neither encoding the path nor leaving
  it bare survives that. The link target is now a complete, correctly encoded `file:` URI
  behind a second `file://` prefix, so that Pidgin strips one and is left holding the URI
  intact; every opener it may invoke (`xdg-open`, `gnome-open`, `kfmclient`) accepts a URI
  as readily as a path. Encoding also makes the old refusal to show a file whose path
  contains a double quote unnecessary, so that has been dropped.
- **Captions of files received as standard file transfers are no longer lost.** With
  file downloads set to "Standard file transfers" (the default outside Pidgin), a file in a
  private chat arrives as a transfer request, which puts nothing in the conversation, and
  its caption was dropped, along with the reply or "forwarded from" header that goes with
  it. A caption now appears in the conversation, headers included, as it does for a file
  shown inline. This bug predates the fork; it is present in 0.8.1 unchanged.
- **Names containing markup characters are shown correctly.** Values chosen by a remote
  party — a file name, a contact's display name, a group title, the caption of a photo or
  file, the text of a quoted message — reach the conversation window, which libpurple
  renders as HTML, but were inserted without escaping. A name containing `&`, `<` or `>`
  was mangled, and one containing a tag was swallowed outright. Such values are now
  escaped wherever they enter conversation markup: the file hyperlink, the four download
  notices and the two for self-destructing files, the sender prefix shared by every in-chat
  notice, captions (which are now treated exactly like the text of an ordinary message),
  the quoted-reply header and its quoted text, the "forwarded from" header, and the
  group-title-change notice. Text bound for
  `purple_request_*` dialogs is deliberately left alone, as Pidgin escapes that itself.
  This bug predates the fork; it is present in 0.8.1 unchanged.

### Build

- **The bundled third-party libraries have been dropped.** The vendored copies of `fmt`
  and `rlottie` — some 74,000 lines of foreign source carried in-tree — are gone, and both
  are now located as system packages via `pkg-config`. Distribution packagers no longer
  need to unbundle them, and security fixes in either library arrive through the system
  rather than waiting on this repository to re-vendor them.
- **Builds cleanly when the Telegram VoIP library (`tgvoip`) is absent**, disabling voice
  call support automatically rather than failing. Can also be forced with the `NoVoip`
  CMake option.
- `build_and_install.sh`, a single script that performs the whole build, courtesy of
  **Ben Wiederhake**.

### Known issues

- **Voice calls are untested, and are absent from any build made without `tgvoip`.** When
  that library is not found, call support is compiled out entirely (`NoVoip`), which is
  the case on the machine where this release was prepared and exercised. The call code has
  therefore not been run at all against the 1.8 API, and should be treated as unverified
  rather than as working.

---

Earlier history is that of the upstream project; see
<https://github.com/ars3niy/tdlib-purple> for releases up to and including 0.8.1.
