# Testing in the GUI

The automated tests drive the plugin through a mock of libpurple and TDLib. They cannot see
what GTK, Pidgin and a real Telegram server do: whether a hook is accepted, a menu closes, a tab
turns red, or TDLib polls the right messages. This list is what a person checks in a real Pidgin,
against a real account, before a release and after changing the parts each test names.

Each test says what to do, what should be seen, and where the debug log can confirm it. The
"Since" line names the release or the bug that the test was written for: a test is added
whenever a failure is found that only the GUI showed.

## Setting up

* Pidgin with the plugin installed, started with its debug output kept, in a file only you can
  read, in a directory only you can write to, such as your home:
  `cd; rm -f pidgin-debug.log; (umask 077; pidgin -d > pidgin-debug.log 2>&1)`, the file removed
  first, as one already there keeps its permissions. TDLib's own log goes there too, at the
  verbosity of Pidgin's debug window; its verbose mode gives TDLib's requests and updates. The
  log holds messages, names, phone numbers and, while signing in, the login code: take out what
  is private before showing any of it to others.
* A group of your own to test in, named however you like, with at least one other member who
  knows it is for testing: messages can then be sent, reacted to and deleted freely, and
  someone there can confirm what reached them. Do not test in others' groups.
* Telegram's phone or desktop app on the same account, to see what the server has.
* A busy channel, with posts that keep getting reactions, for the reaction counts.

In the log, the plugin's own lines start with `telegram-tdlib:`, TDLib's with `tdlib:`. A
request the plugin makes appears as TDLib's `Receive request N: <name> {`.

Without Telegram Premium, an account gives one reaction to a message: a test's reaction takes
the place of the one given before.

## Driving Pidgin with synthetic input

The tests can be done by a script or an agent, with input made up by a tool, on the tester's
own display. Such input is not a hand on the mouse, and whatever tool is used, look out for:

* A click that lets the button go at once, before the menu it opens is up. GTK's menu then
  still takes the right button for the one that picks, from the menu and its submenus, and a
  left click in a submenu closes it with nothing picked: pick with the right button too.
* A screenshot that grabs the pointer or the display, which closes any open menu.
* Input going to whatever is under the pointer and has the focus: the tester should not be
  using the display meanwhile.

With X11's `xdotool`, for example, `click 3` is such a click; `xwd -root` takes screenshots
without a grab, where ImageMagick's `import` grabs; and `xdotool search --onlyvisible --pid`
finds a menu, as one of Pidgin's windows.

## Starting and quitting

### S1. Start with conversations open

Quit Pidgin with a tab or two of Telegram chats open, and start it again.

* No warning `Cannot tell which tabs are shown` in the log, and no GLib warning about emission
  hooks.
* Each tab is filled with its messages once it is shown: `Redrawing chat <id> ... N messages`.
  Note how long it took from `Opening chat <id>`, which is logged as the tab is shown: at once
  for the chosen tab of each window, and for one behind it when it is switched to.
* A tab of a chat with nothing unread is not marked as having new messages.

Since: 0.9.6, whose focus hook GLib refused, so that no tab was known to be shown.

### S2. No disconnect notice after a restart

Quit Pidgin with a group's tab open, start it, and open the tab.

* The tab does not show "The account has disconnected and you are no longer in this chat",
  which Pidgin writes into its chats as it quits.

Since: the notice was logged at every quit and shown again when the tab was filled.

### S3. Signing in

With a fresh Pidgin profile (`pidgin -c <directory>`) or an account not yet signed in, add the
account and enter the login code Telegram sends, and the two-step verification password if the
account has one.

* Pidgin's dialogs ask for each, take what is entered, and the account connects.

### S4. Messages missed while offline

Quit Pidgin, have someone send several messages to a private chat and a group, start Pidgin.

* All of them are shown, in order, not only the latest.

### S5. Encrypting the local database

On a copy of the profile, or with an account whose database can be lost, turn on "Encrypt the
local database with the password" in the account's settings, with a password not remembered, and
connect.

* Pidgin asks for the password; then the plugin asks for it again, warning how long rewriting the
  database takes, and a notice not to quit stays up until it says the database is encrypted.
* The debug log names the database directory as encrypted ("using database directory ..., encrypted").
* Connecting again asks for the password, and a wrong one is refused, the account staying
  disconnected; the right one connects.
* "Change the database password..." with two different passwords asks again; with the same one,
  the database is encrypted anew and the new password opens it next time.
* Turning the option off asks for the password at the next connection, and goes back to TDLib's
  built-in key.

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

Click a reaction someone else gave, on the line under a message; then click it again.

* The first click gives it as ours too, in bold, its count one more; the second takes it back.
* Log: `Reaction e<emoji> to message <id> clicked: giving it`, then `taking it back`.
* Pressing on a reaction and dragging off it before letting go does nothing; clicking the paid
  one (⭐) asks how many stars, as the menu does.

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

### R7. A reaction to an older message of ours

With the chat's tab open, have someone react to a message of ours older than the earliest the tab
shows.

* The tab is filled anew from that message to the latest, the reaction under it, if they fit in
  the lines Pidgin is set to keep of a conversation (its "scrollback_lines" preference).
* For one further back than that, the tab stays as it is, and the debug log says the message is
  further back than the tab holds.

## A message's menu

### M1. Edit and delete

Edit a message of ours from its menu, then delete it.

* The edit is written over the message in Pidgin and the other members see it; the deletion takes
  it out, and the notice names us, as having deleted our own message: in English, "deleted their
  message".

Since: an edit forgot that the message was ours, so that deleting it named us as its sender like
anyone else's.

### M2. Forward

Forward a message from its menu, to a buddy of this account and to a chat of another account,
picking each in the dialog by typing part of its name.

* It arrives at both, as a forward where the destination is Telegram.

Forward one again, ticking two destinations: type part of one's name, tick it, type part of the
other's, tick that too, and press OK.

* The first one stays ticked while the list shows only the second.
* It arrives at both; with none ticked, Enter forwards it to the one picked, as before.

### M3. Files of a message

On a photo: "Copy the picture", paste into another program; "Save Image...", check the name it
suggests. Drag a file's link out of the conversation into a file manager.

* Each gives the message's own file, under its own name.

### M4. Giving up an edit

Type a message without sending it, then start editing one of ours from its menu, and press
Escape. Do it again, and send the edit this time.

* Escape leaves the message as it was; either way, the text typed before comes back into the
  input field.

## History

### H1. Opening a chat

Open a chat with unread messages.

* It is scrolled to the first unread, with read ones before it.

Leave a chat's tab open, but not the one shown in its window, while more
than a screenful of messages comes to it. Then switch to it.

* It is filled anew and scrolled to the first unread, as it was when opened,
  not left at its end.
* A tab with nothing unread is left as it was, scrolled where it was.

### H2. Going back

Scroll to the top of a conversation, or turn the mouse wheel up there.

* Fifty more messages are added before the earliest, which stays where it was.

In a chat short enough, go back to its first message. Then have the tab filled
anew from its first unread, as in H1, and go back again.

* The earlier messages come back, up to the first one again.

### H3. Links to messages

Click the time in a reply's quote, and in the notice of an edit or a reaction.

* The conversation scrolls to that message.

### H4. Our deleted messages, again

Send a message, delete it, then close the chat's tab and open it again, or quit Pidgin and start it.

* The message is struck out, as anyone else's deleted message is, or left out if the chat is set
  not to keep deleted messages.

Since: ours were logged before Telegram gave them their lasting ids, so the log could not tell
which they were, and they came back as if never deleted.

### H5. Telegram links

With Pidgin running, hand it a link to a post of a channel, joined or not,
with `tdlib-purple-tg-link`, as README.md shows; then one to a message of an open chat that
its tab still shows, and one that it no longer does.

* The chat's conversation comes up, its tab chosen in its window even while another was, at
  the post, filled from it.
* The tab still showing the message is scrolled to it; the other one is filled
  anew from the message.
* A link to something else, such as `tg://settings`, or to a channel that does
  not exist, gets a dialog saying it cannot be followed.
* With Pidgin not running, `tdlib-purple-tg-link` says so, as a desktop
  notification or with `xmessage`.
* Made the handler of `tg:` links (`xdg-mime default
  tdlib-purple-tg-link.desktop x-scheme-handler/tg`), a "View in Telegram"
  link on a channel's page in Firefox opens the post in Pidgin.
* A link to a post further back than Pidgin's "scrollback_lines" preference
  holds gets a dialog saying so; a tab opened for it shows the latest messages,
  from the first unread, and an open one is left as it was.
* Two links followed one right after the other, the first to a chat whose tab
  is just being opened, end up at the second.
* So do two to an open tab, the first to a message it does not show, which
  fills it anew, the second to one it shows: it ends at the second, not
  scrolled back to the first once filled.

## Files and pictures

### F1. Pasting

Copy a picture in an image viewer, and a few files in a file manager; paste each into a message,
add text after the placeholder, and send.

* The input field shows thumbnails; the message arrives with the picture as a photo, the files as
  files, the text as their caption.

### F2. Dropping

Drop a file from a file manager onto the input field, and another onto the conversation's text.
Drag a picture from a web browser, and a link to a page that is not a picture.

* Each file and the picture get their placeholders and thumbnails, and arrive when the message is
  sent; the link to the page stays text.
* Log: `Fetching <url>, dropped` for what is dragged from the browser, and
  `Dropped <url>, not a picture` for the page.

### F3. A pasted thing's menu

Paste a picture, right-click its thumbnail in the input field and pick "Send as a file"; paste
another, and remove it from the same menu.

* The first arrives as a file rather than a photo; the one removed is not sent.

### F4. Sending and receiving by file transfer

Send a large file with "Send File...", and have someone send you one; cancel another while it is
being received.

* The transfer dialog's progress advances and the file arrives whole; the one cancelled stops.
* Log: `Started uploading`, `Finishing uploading`; `Cancelling download` for the one cancelled.

### F5. Stickers and custom emoji

Have someone send an animated sticker, a video sticker and a message with custom emoji: the
plugin cannot send stickers.

* They are shown animated and transparent around the picture, not on a white square.

### F6. Videos

Receive a video with a cover, and one without.

* The cover, where there is one, is shown rather than the first frame; the links list the
  video's other sizes, the original marked.

## Groups

### G1. A group's news

Add someone to the test group, have them leave, pin a message.

* Each is told in words: "X added Y", "X left", "X pinned a message".

### G2. A member who is not a contact

In the test group, open a private conversation with a member who is not in the buddy list, from
the menu of their name in the member list, and send them a message (or have them write first).

* The conversation's tab shows their name; it goes by `id<their id>`, as the debug log shows.
* They are in the buddy list, in "Telegram: not in contacts", until Pidgin quits; `blist.xml`
  does not keep them.
* Their messages are marked read as they are seen, as a contact's are.
* Removing them from "Telegram: not in contacts" only takes them out of the list: the chat with
  them is still there, on the phone too.

With another member who is not a contact, written to in the same way:

* "Add Buddy..." from the menu of their name adds them as a contact, under their name, and the
  same tab stays open.
