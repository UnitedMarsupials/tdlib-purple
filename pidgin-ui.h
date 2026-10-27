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
// message once it is written, unless an earlier showing of it already has one.
class MessageMarkScope {
public:
    explicit MessageMarkScope(MessageId messageId, const std::string &prefix = std::string());
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
