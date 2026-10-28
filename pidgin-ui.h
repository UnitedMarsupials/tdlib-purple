#ifndef _PIDGIN_UI_H
#define _PIDGIN_UI_H

// Pidgin's conversation window, through its own headers, for their types only. The plugin links
// against neither Pidgin nor GTK, so that it still loads into other front-ends: their functions
// are looked up by name at run time (findFrontEndSymbol), and found only when the host is Pidgin.
// Built with NoPidgin, the types are opaque and what needs them does nothing.

#include "buildopt.h"
#include "identifiers.h"
#include <purple.h>
#include <functional>
#include <string>
#include <vector>

#ifndef NoPidgin
#include <gtkimhtml.h>

using PidginIMHtml     = GtkIMHtml;
using PidginIMHtmlLink = GtkIMHtmlLink;
using PidginWidget     = GtkWidget;
using PidginTextMark   = GtkTextMark;
#else
using PidginIMHtml     = void;
using PidginIMHtmlLink = void;
using PidginWidget     = void;
using PidginTextMark   = void;
#endif

// Looks up what the rest needs, and follows what Pidgin writes into its conversations. Without
// Pidgin, everything below does nothing, and finds nothing.
void pidginUiLoad(PurplePlugin *plugin);
void pidginUiUnload();
bool pidginUiFound();

// Each message shown is bracketed in its tab by two marks, found again by its id; those of a
// message shown again are moved. While an object of this class lives, what is written into a
// conversation belongs to the given message. The prefix, HTML for the tab only, goes before the
// message once it is written, unless an earlier showing of it already has one. With an edit note,
// the message is written over its earlier version, where that is shown, its header kept, and the
// note after its text; elsewhere it is written anew. A message written anew, all of it, takes its
// marks and prefix from an earlier showing; otherwise what is written extends what was, if any.
// Marking changes, the first part of an edited message shows how its text differs from before.
class MessageMarkScope {
public:
    explicit MessageMarkScope(MessageId messageId, const std::string &prefix = std::string(),
                              const char *editNote = nullptr, bool anew = false,
                              bool markChanges = false);
    ~MessageMarkScope();
    MessageMarkScope(const MessageMarkScope &) = delete;
    MessageMarkScope &operator=(const MessageMarkScope &) = delete;
#ifndef NoPidgin
private:
    bool m_active = false; // not if another scope is already in effect, or there is no Pidgin
#endif
};

struct MessageMarks {
    PidginTextMark *start = nullptr;
    PidginTextMark *end   = nullptr;
};

// A message edited in a conversation's input field, in Pidgin: its text takes the place of what the
// field held, which comes back once the edit is sent or given up, and the field is tinted meanwhile.
// False where that cannot be done.
struct EntryEdit {
    ChatId      chatId;
    MessageId   messageId;
    bool        isCaption = false;
    std::string original; // sent unchanged, it is no edit
};
// Esc in the field gives the edit up as well, calling back after.
bool beginEntryEdit(PurpleConversation *conv, const EntryEdit &edit, const std::string &text,
                    std::function<void()> cancelled);
// Ends the edit in progress, if any, telling which it was
bool endEntryEdit(PurpleConversation *conv, EntryEdit *edit);
// Whether that message is being edited in the conversation's input field
bool isEntryEditing(PurpleConversation *conv, MessageId messageId);

// Whether Pidgin shows the conversation, so that it can be emptied and filled anew
bool canRedrawConversation(PurpleConversation *conv);
// A line at the end of the tab saying what is under way, with a turning character after it, until
// stopped; in Pidgin only
void startProgress(PurpleConversation *conv, const std::string &html);
void stopProgress(PurpleConversation *conv);

// Whether the message's text is in the conversation's tab, to be written over
bool canReplaceMessage(PurpleConversation *conv, MessageId messageId);

// The line under a message that lists its reactions, as HTML, replacing what it said before; none
// if empty. In Pidgin's tab only, where the message is.
void showMessageReactions(PurpleConversation *conv, MessageId messageId, const std::string &html);

// Whether the tab shows an edited message whose change can be marked, and whether it is
bool canMarkChanges(PurpleConversation *conv, MessageId messageId, bool &marked);
// Writes the message's latest version anew, with or without the change from the one before marked
void setChangesMarked(PurpleConversation *conv, MessageId messageId, bool marked);

// Strikes out a deleted message's text, where the tab shows it, and takes its menu link away
void strikeOutMessage(PurpleConversation *conv, MessageId messageId);
// Takes a deleted message out of the tab, its header and reactions with it
void removeMessage(PurpleConversation *conv, MessageId messageId);

// Where the message is in the conversation's tab, if it is there
MessageMarks findMessageMarks(PurpleConversation *conv, MessageId messageId);

// A message being sent gets a new id once it is: its marks are renamed with it, and the prefix,
// which names the new id, is added only then
void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId,
                        const std::string &prefix);

// Scrolls the tab's text to the message and selects it; false if it is not there
bool scrollToMessage(PidginIMHtml *imhtml, MessageId messageId);

// An item of a menu: a submenu, if it has items, otherwise disabled if it has no action. The
// submenu's items are laid out in rows of the given number, if any, rather than in one column.
struct MenuItem {
    std::string            label;
    bool                   checkable = false;
    bool                   checked   = false;
    bool                   dimmed    = false;  // Looks disabled, but can be picked
    std::string            tooltip;
    unsigned               columns   = 0;
    std::function<void()>  action;
    std::vector<MenuItem>  submenu;
};

// Pops up a menu where the pointer is
void popUpMenu(const std::vector<MenuItem> &items);

#endif
