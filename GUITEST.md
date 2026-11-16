# Testing in the GUI

The automated tests drive the plugin through a mock of libpurple and TDLib. They cannot see
what GTK, Pidgin and a real Telegram server do: whether a hook is accepted, a menu closes, a tab
turns red, or TDLib polls the right messages. This list is what a person checks in a real Pidgin,
against a real account, before a release and after changing the parts each test names.

Each test says what to do, what should be seen, and where the debug log can confirm it. The
"Since" line names the release or the bug that the test was written for: a test is added
whenever a failure is found that only the GUI showed.

## Setting up

* Pidgin with the plugin installed, started with its debug output kept:
  `pidgin -d > pidgin-debug.log 2>&1`. TDLib's own log goes there too, at the verbosity of
  Pidgin's debug window; its verbose mode gives TDLib's requests and updates.
* A group of your own to test in, named however you like, with at least one other member who
  knows it is for testing: messages can then be sent, reacted to and deleted freely, and
  someone there can confirm what reached them. Do not test in others' groups.
* Telegram's phone or desktop app on the same account, to see what the server has.
* A busy channel, with posts that keep getting reactions, for the reaction counts.

In the log, the plugin's own lines start with `telegram-tdlib:`, TDLib's with `tdlib:`. A
request the plugin makes appears as TDLib's `Receive request N: <name> {`.

## Starting and quitting

### S1. Start with conversations open

Quit Pidgin with a tab or two of Telegram chats open, and start it again.

* No warning `Cannot tell which tabs are shown` in the log, and no GLib warning about emission
  hooks.
* Each tab is filled with its messages: `Redrawing chat <id> ... N messages`. Note how long it
  took from `Opening chat <id>` to that line.
* A tab of a chat with nothing unread is not marked as having new messages.

Since: 0.9.6, whose focus hook GLib refused, so that no tab was known to be shown.

### S2. No disconnect notice after a restart

Quit Pidgin with a group's tab open, start it, and open the tab.

* The tab does not show "The account has disconnected and you are no longer in this chat",
  which Pidgin writes into its chats as it quits.

Since: the notice was logged at every quit and shown again when the tab was filled.

### S3. Messages missed while offline

Quit Pidgin, have someone send several messages to a private chat and a group, start Pidgin.

* All of them are shown, in order, not only the latest.

## Reading and the tabs shown

### T1. Read as seen

With a chat that has unread messages, open its tab in a focused window, and scroll slowly.

* The phone app shows the messages as read as they come into view, not all at once.
* Log: `viewMessages` requests listing the messages on screen, oldest first.

### T2. Not read unseen

Leave the window unfocused, minimized, or the screen blanked, while messages arrive.

* They stay unread in the phone app until the window is focused and they are on screen.

Since: 0.9.6.

### T3. Read elsewhere

Read a chat in the phone app while its tab is open in Pidgin, not focused.

* The tab, the tray and the buddy list's menu stop counting it as unread.

## Reactions

### R1. Reaction to a text message

Right-click a text message, pick a reaction from the menu.

* The reaction appears under the message, in Pidgin and the phone app.
* Log: `Reaction <emoji> to message <id> picked`, then `addMessageReaction`.

### R2. Reaction to a picture

Right-click the picture of a photo sent without text, and pick a reaction from the message's
items after "Save Image...". Do the same with a video's preview picture.

* The reaction is given, as in R1; the menu does not just close with nothing done.

Since: the menu of a picture opened on the button's release dropped picks made in its submenus.

### R3. Taking a reaction back

Pick the reaction already given, from the same menu.

* It is removed in Pidgin and in the phone app.

### R4. Counts kept up to date

Open a busy channel's tab in a focused window, with several posts on screen, and watch it for a
few minutes beside the phone app.

* The counts under every post on screen follow the phone app's, not only those of the latest.
* Log: TDLib's `messages.getMessagesReactions` queries, every 15 seconds or so, list the posts on
  screen.

Since: 0.9.6, whose read receipts went newest first, so that TDLib polled the oldest of them.

### R5. Who reacted

Point at a reaction under a group's message.

* The tooltip lists who gave it.

### R6. Reactions to our messages

Have someone react to a message of ours, with its chat's tab closed.

* The chat is marked with ❤️ before its title and its buddy's name, until the tab is opened.
* For a message of ours the tab does not show, the tab is filled anew from that message.

## A message's menu

### M1. Edit and delete

Edit a message of ours from its menu, then delete it.

* The edit is written over the message in Pidgin and the other members see it; the deletion takes
  it out, and the notice names us.

### M2. Forward

Forward a message from its menu, to a buddy of this account and to a chat of another account,
picking each in the dialog by typing part of its name.

* It arrives at both, as a forward where the destination is Telegram.

### M3. Files of a message

On a photo: "Copy the picture", paste into another program; "Save Image...", check the name it
suggests. Drag a file's link out of the conversation into a file manager.

* Each gives the message's own file, under its own name.

## History

### H1. Opening a chat

Open a chat with unread messages.

* It is scrolled to the first unread, with read ones before it.

### H2. Going back

Scroll to the top of a conversation, or turn the mouse wheel up there.

* Fifty more messages are added before the earliest, which stays where it was.

### H3. Links to messages

Click the time in a reply's quote, and in the notice of an edit or a reaction.

* The conversation scrolls to that message.

## Files and pictures

### F1. Pasting

Copy a picture in an image viewer, and a few files in a file manager; paste each into a message,
add text after the placeholder, and send.

* The input field shows thumbnails; the message arrives with the picture as a photo, the files as
  files, the text as their caption.

### F2. Stickers and custom emoji

Send and receive an animated sticker, a video sticker and a message with custom emoji.

* They are shown animated and transparent around the picture, not on a white square.

### F3. Videos

Receive a video with a cover, and one without.

* The cover, where there is one, is shown rather than the first frame; the links list the
  video's other sizes, the original marked.

## Groups

### G1. A group's news

Add someone to the test group, have them leave, pin a message.

* Each is told in words: "X added Y", "X left", "X pinned a message".
