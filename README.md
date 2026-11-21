# New libpurple plugin for Telegram

This is a future successor to [telegram-purple](https://github.com/majn/telegram-purple).

Of the known forks of [ars3niy's original](https://github.com/ars3niy/tdlib-purple),
this one is the most fully featured. See [CHANGELOG.md](CHANGELOG.md).

There is a [telegram group](https://t.me/joinchat/BuRiSBO0mMw7Lxy0ufVO5g) (what else?) for discussions and support.

## Functionality

### Animated stickers

Converting animated stickers to GIFs is CPU-intensive. If this is a problem,
the conversion can be disabled in account settings, or even at compile time (see below).

### Pasting files and pictures

What is pasted into a message is sent with it, once the message is: the files
copied to the clipboard, as a file manager copies them, or else its picture.
Each pasted thing is numbered, and stands in the message's text as its
placeholder, the text after it being its caption:

* `[Image#1]` or `[Video#2]` sends a picture or video as such, nameless, as
  Telegram shows it in the chat (and makes it smaller);
* `[name#N]` sends thing N as a file, as it is, under that name: its own, as in
  `[report.pdf#3]`, or any other, as in `[meow.jpg#1]`. A picture of the
  clipboard's, having no name, is `image-1.png` and so on, the extension the
  one TDLib gives the type of its content. A name without an extension takes
  the thing's own, as `[meow#1]` does, since Telegram tells a file's type by
  its extension.

Several in a row go as an album, pictures and videos together, files apart, up
to ten in one. Letter case does not matter. Files are not copied: one gone by
the time the message is sent keeps the message from being sent.

In Pidgin, the input field shows a thumbnail of the file in the placeholder's
place, as tall as five lines of its font, which the message is sent with, the placeholder in its tooltip: a
picture's own, or the one file managers keep, or one a thumbnailer registered
for the file's type makes, or else an icon of the type. Right-clicking it, or
a placeholder written out, offers to send it the other way, or to take it out
of the message.

A message's picture, video or file, edited, goes by a placeholder too, before
its caption: kept, only the caption changes; another placeholder put in its
place, or the same one written as a file, or under another name, replaces it,
as far as Telegram allows (in an album, a photo or video only by a photo or
video, a file by a file). Telegram takes nothing out of a message, nor puts
more into it, and such an edit is refused.

In Pidgin, pasting into a conversation's input field puts the placeholders of
the files copied there, or of the picture; so does dropping files onto the
conversation, its input field or the messages above it, and a picture dropped
from a browser is fetched to go there, anything else dropped as a link going
in as its address. `/paste` does the same everywhere: other front-ends, such
as Finch, cannot take anything into their input line, so it says the
placeholders, to be written into the message where they go. The clipboard is read there with
[xclip](https://github.com/astrand/xclip), found in `PATH`, which is not
otherwise required. Placeholders last as long as the account is connected.

A file sent by file transfer ("Send File...") goes as a file. An account
option, "Send a file as a picture, video or voice message when it is one",
makes it go as what its content is instead, as a pasted one does, for
front-ends whose only way of sending a file is a file transfer.

### Telegram links

A Telegram link, such as `tg://resolve?domain=channel&post=12`, which a web
page's "View in Telegram" leads to, opens the post's chat, at that post, once
handed to libpurple: TDLib, which knows every kind of Telegram link, tells
what it leads to. Links to chats and to their messages are followed, those of
channels and groups not joined too, which are then shown though not joined;
any other kind, or one leading nowhere, is told in a dialog. The first
Telegram account connected follows it, or the one an `account` parameter
names, as in `tg://resolve?domain=channel&post=12&account=%2B15551234567`.

Libpurple takes such links over D-Bus, in Pidgin, Finch or another program
built with it, which `tdlib-purple-tg-link`, installed with the plugin, hands
them over by:

    tdlib-purple-tg-link 'tg://resolve?domain=channel&post=12'

It needs only `gdbus`, which comes with GLib. What goes wrong, such as no
libpurple program running, it tells as a desktop notification, or else with
`xmessage`, or on stderr. A web browser starts it for `tg:` links once
`tdlib-purple-tg-link.desktop`, installed with it, is made their handler:

    xdg-mime default tdlib-purple-tg-link.desktop x-scheme-handler/tg

Firefox asks the first time whether to open such a link with it.

## Debugging vs. privacy

It's good to have debug log at hand whenever a glitch is observed, or to be able to reproduce the glitch with logging turned on. With pidgin, debug log can be turned on like this:
```
rm -f ~/pidgin.log; (umask 077; pidgin -d > ~/pidgin.log 2>&1)
```

The debug log contains a lot of private information such as names and phone numbers of all contacts, list of all channels you've participated in or text of all sent and received messages. Be mindful of that before posting debug log on the internets. On a system with other users, keep the file readable only by you, as the command above does. Such is the nature of debugging instant messaging software.

## Building

Compatible version of TDLib should be prebuilt and installed somewhere (requires C++14). Version requirement can be found in cmake/tdlib-version.cmake:
```
grep -o "tdlib version.*" cmake/tdlib-version.cmake
```

TDLib can be built like this:
```
cd <path to TDLib sources>
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
make install DESTDIR=/path/to/tdlib
```

See also [building](https://github.com/tdlib/td#building).

libtgvoip is required for voice calls.

Building this plugin:
```
mkdir build
cd build
cmake -DTd_DIR=/path/to/tdlib/usr/local/lib/cmake/Td ..
make
```

This will build with test API id, which is supposed to be rate limited and may
occasionally not work. Maybe I will man up and include my API id in the source
code in the future. For now, it's possible to build with `-DAPI_ID=<api id> -DAPI_HASH=<api hash>`
to use another API id.

To install, copy the .so to libpurple plugins directory, or run `make install`.

Building without animated sticker decoding: `-DNoLottie=True`

Building without localization: `-DNoTranslations=True`

Building without voice call support: `-DNoVoip=True`

Building without Pidgin's headers: `-DNoPidgin=True`. The plugin is then built against libpurple alone, and features that need Pidgin's conversation window are left out.

Building with voice call support: `-Dtgvoip_LIBRARIES="tgvoip;opus;<any other tgvoip dependencies>"`

If libtgvoip is not installed in include/library path then build with
```
-Dtgvoip_INCLUDE_DIRS=/path/to/tgvoip/include -Dtgvoip_LIBRARIES="/path/to/libtgvoip.a;<dependencies>"
```

## Proper user names in bitlbee

```
account telegram-tdlib set nick_format %full_name
```

## Regression test

Build google test library and `make install` it somewhere

Run cmake with `-DGTEST_PATH=/path/to/gtest`

`make run-tests` or `make tests`, `test/tests` or `valgrind test/tests`

What these cannot catch is tested by hand: see [GUITEST.md](GUITEST.md).
