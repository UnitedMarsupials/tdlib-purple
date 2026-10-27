#include "pidgin-ui.h"
#include "client-utils.h"
#include "config.h"
#include <string>

#ifndef NoPidgin
#include <gtkconv.h>

// Pidgin's and GTK's functions, looked up by name: their types are those the headers declare
#define FRONT_END_FUNCTION(name) static decltype(&name) p_##name
#define LOOK_UP(name) (p_##name = reinterpret_cast<decltype(&name)>(findFrontEndSymbol(#name)))

FRONT_END_FUNCTION(pidgin_conversations_get_handle);
FRONT_END_FUNCTION(pidgin_conversations_get_conv_ui_ops);
FRONT_END_FUNCTION(gtk_text_buffer_get_end_iter);
FRONT_END_FUNCTION(gtk_text_buffer_get_iter_at_offset);
FRONT_END_FUNCTION(gtk_text_iter_get_offset);
FRONT_END_FUNCTION(gtk_text_buffer_get_mark);
FRONT_END_FUNCTION(gtk_text_buffer_create_mark);
FRONT_END_FUNCTION(gtk_text_buffer_move_mark);
FRONT_END_FUNCTION(gtk_text_buffer_delete_mark);
FRONT_END_FUNCTION(gtk_text_buffer_get_iter_at_mark);
FRONT_END_FUNCTION(gtk_text_buffer_select_range);
FRONT_END_FUNCTION(gtk_text_view_scroll_to_mark);
FRONT_END_FUNCTION(gtk_imhtml_insert_html_at_iter);
FRONT_END_FUNCTION(gtk_menu_new);
FRONT_END_FUNCTION(gtk_menu_item_new_with_label);
FRONT_END_FUNCTION(gtk_check_menu_item_new_with_label);
FRONT_END_FUNCTION(gtk_check_menu_item_set_active);
FRONT_END_FUNCTION(gtk_menu_item_set_submenu);
FRONT_END_FUNCTION(gtk_menu_shell_append);
FRONT_END_FUNCTION(gtk_menu_attach);
FRONT_END_FUNCTION(gtk_menu_popup);
FRONT_END_FUNCTION(gtk_widget_set_sensitive);
FRONT_END_FUNCTION(gtk_widget_show_all);
FRONT_END_FUNCTION(gtk_widget_destroy);
FRONT_END_FUNCTION(g_signal_connect_data);

static bool g_pidginUi = false;

// The message being written, and where its text starts in the first conversation it goes to
struct MessageBeingWritten {
    MessageId           messageId;
    std::string         trailer;
    PurpleConversation *conv        = nullptr;
    gint                startOffset = 0;
};
static MessageBeingWritten *g_messageBeingWritten = nullptr;

// A conversation's tab, if Pidgin shows it
static GtkIMHtml *getIMHtml(PurpleConversation *conv)
{
    if (!g_pidginUi || !conv || (purple_conversation_get_ui_ops(conv) != p_pidgin_conversations_get_conv_ui_ops()))
        return nullptr;
    PidginConversation *gtkconv = PIDGIN_CONVERSATION(conv);
    // GTK_IMHTML() would call gtk_imhtml_get_type(), which the plugin does not link against
    return gtkconv ? reinterpret_cast<GtkIMHtml *>(gtkconv->imhtml) : nullptr;
}

static GtkTextBuffer *getTextBuffer(PurpleConversation *conv)
{
    GtkIMHtml *imhtml = getIMHtml(conv);
    return imhtml ? imhtml->text_buffer : nullptr;
}

enum class MarkKind {
    Start,
    End,
    Trailer
};

static std::string getMarkName(MessageId messageId, MarkKind kind)
{
    const char *prefix = (kind == MarkKind::Start) ? "tdlib-message-start-" :
                         (kind == MarkKind::End)   ? "tdlib-message-end-" : "tdlib-message-trailer-";
    return prefix + std::to_string(messageId.value());
}

static std::string getMarkName(MessageId messageId, bool start)
{
    return getMarkName(messageId, start ? MarkKind::Start : MarkKind::End);
}

static void setMark(GtkTextBuffer *buffer, const std::string &name, const GtkTextIter &where)
{
    GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, name.c_str());
    if (mark)
        p_gtk_text_buffer_move_mark(buffer, mark, &where);
    else
        // Left gravity: text later written at the same place goes after the mark
        p_gtk_text_buffer_create_mark(buffer, name.c_str(), &where, TRUE);
}

// Emitted by Pidgin just before it writes into a tab, for IM and chat messages alike
static gboolean displayingMessage(PurpleAccount *account, const char *who, char **message,
                                  PurpleConversation *conv, PurpleMessageFlags flags)
{
    if (g_messageBeingWritten && !g_messageBeingWritten->conv) {
        GtkTextBuffer *buffer = getTextBuffer(conv);
        if (buffer) {
            GtkTextIter end;
            p_gtk_text_buffer_get_end_iter(buffer, &end);
            g_messageBeingWritten->conv        = conv;
            g_messageBeingWritten->startOffset = p_gtk_text_iter_get_offset(&end);
        }
    }
    return FALSE;
}

// Emitted by Pidgin once it has written into a tab; not if another plugin stopped the writing.
// The marks are set (again) after each part of the message, while the conversation surely exists.
static void displayedMessage(PurpleAccount *account, const char *who, char *message,
                             PurpleConversation *conv, PurpleMessageFlags flags)
{
    GtkTextBuffer *buffer = (g_messageBeingWritten && (g_messageBeingWritten->conv == conv))
                            ? getTextBuffer(conv) : nullptr;
    if (buffer) {
        GtkTextIter start, end;
        p_gtk_text_buffer_get_iter_at_offset(buffer, &start, g_messageBeingWritten->startOffset);
        p_gtk_text_buffer_get_end_iter(buffer, &end);
        setMark(buffer, getMarkName(g_messageBeingWritten->messageId, true), start);
        setMark(buffer, getMarkName(g_messageBeingWritten->messageId, false), end);
    }
}

// Inserts what follows a message after its end, once: the message is then up to its end
static void appendTrailer(PurpleConversation *conv, MessageId messageId, const std::string &trailer)
{
    GtkIMHtml     *imhtml = getIMHtml(conv);
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *end    = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, false).c_str()) : nullptr;
    if (trailer.empty() || !end || p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Trailer).c_str()))
        return;

    GtkTextIter where;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &where, end);
    p_gtk_text_buffer_create_mark(buffer, getMarkName(messageId, MarkKind::Trailer).c_str(), &where, TRUE);
    // Into the tab only, not the log
    p_gtk_imhtml_insert_html_at_iter(imhtml, trailer.c_str(), GTK_IMHTML_NO_SCROLL, &where);
    p_gtk_text_buffer_move_mark(buffer, end, &where);
}

static guint g_connectSource = 0;

// Disconnected by libpurple when the plugin is unloaded
static gboolean connectSignals(gpointer plugin)
{
    g_connectSource = 0;
    void *handle    = p_pidgin_conversations_get_handle();
    bool  connected = true;
    for (const char *signal: {"displaying-im-msg", "displaying-chat-msg"})
        connected = purple_signal_connect(handle, signal, plugin, PURPLE_CALLBACK(displayingMessage), NULL) &&
                    connected;
    for (const char *signal: {"displayed-im-msg", "displayed-chat-msg"})
        connected = purple_signal_connect(handle, signal, plugin, PURPLE_CALLBACK(displayedMessage), NULL) &&
                    connected;
    if (!connected)
        purple_debug_warning(config::pluginId, "Cannot follow Pidgin's conversation window\n");
    return FALSE;
}

void pidginUiLoad(PurplePlugin *plugin)
{
    g_pidginUi = LOOK_UP(pidgin_conversations_get_handle) && LOOK_UP(pidgin_conversations_get_conv_ui_ops) &&
                 LOOK_UP(gtk_text_buffer_get_end_iter) && LOOK_UP(gtk_text_buffer_get_iter_at_offset) &&
                 LOOK_UP(gtk_text_iter_get_offset) && LOOK_UP(gtk_text_buffer_get_mark) &&
                 LOOK_UP(gtk_text_buffer_create_mark) && LOOK_UP(gtk_text_buffer_move_mark) &&
                 LOOK_UP(gtk_text_buffer_delete_mark) && LOOK_UP(gtk_text_buffer_get_iter_at_mark) &&
                 LOOK_UP(gtk_text_buffer_select_range) && LOOK_UP(gtk_text_view_scroll_to_mark) &&
                 LOOK_UP(gtk_imhtml_insert_html_at_iter) && LOOK_UP(gtk_menu_new) &&
                 LOOK_UP(gtk_menu_item_new_with_label) && LOOK_UP(gtk_check_menu_item_new_with_label) &&
                 LOOK_UP(gtk_check_menu_item_set_active) && LOOK_UP(gtk_menu_item_set_submenu) &&
                 LOOK_UP(gtk_menu_shell_append) && LOOK_UP(gtk_menu_attach) &&
                 LOOK_UP(gtk_menu_popup) &&
                 LOOK_UP(gtk_widget_set_sensitive) && LOOK_UP(gtk_widget_show_all) &&
                 LOOK_UP(gtk_widget_destroy) && LOOK_UP(g_signal_connect_data);
    purple_debug_info(config::pluginId, "Pidgin's conversation window: %s\n",
                      g_pidginUi ? "found" : "not found");
    if (!g_pidginUi)
        return;

    // A protocol plugin is loaded before Pidgin's window registers its signals: they are
    // connected to once the main loop is idle
    g_connectSource = g_idle_add(connectSignals, plugin);
}

void pidginUiUnload()
{
    if (g_connectSource) {
        g_source_remove(g_connectSource);
        g_connectSource = 0;
    }
    g_pidginUi = false;
}

bool pidginUiFound()
{
    return g_pidginUi;
}

MessageMarkScope::MessageMarkScope(MessageId messageId, const std::string &trailer)
{
    if (g_pidginUi && messageId.valid() && !g_messageBeingWritten) {
        g_messageBeingWritten = new MessageBeingWritten;
        g_messageBeingWritten->messageId = messageId;
        g_messageBeingWritten->trailer   = trailer;
        m_active = true;
    }
}

MessageMarkScope::~MessageMarkScope()
{
    if (m_active) {
        if (g_messageBeingWritten->conv)
            appendTrailer(g_messageBeingWritten->conv, g_messageBeingWritten->messageId,
                          g_messageBeingWritten->trailer);
        delete g_messageBeingWritten;
        g_messageBeingWritten = nullptr;
    }
}

MessageMarks findMessageMarks(PurpleConversation *conv, MessageId messageId)
{
    MessageMarks   marks;
    GtkTextBuffer *buffer = getTextBuffer(conv);
    if (buffer) {
        marks.start = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, true).c_str());
        marks.end   = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, false).c_str());
        if (!marks.start || !marks.end)
            marks = MessageMarks();
    }
    return marks;
}

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId, const std::string &trailer)
{
    if (!g_pidginUi)
        return;
    // The message's conversation is not looked for, lest it be opened again: a temporary id is
    // not that of a message in another chat
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv   = static_cast<PurpleConversation *>(item->data);
        GtkTextBuffer      *buffer = (purple_conversation_get_account(conv) == account) ? getTextBuffer(conv) : nullptr;
        if (!buffer)
            continue;
        for (bool start: {true, false}) {
            GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, getMarkName(oldId, start).c_str());
            if (mark) {
                GtkTextIter where;
                p_gtk_text_buffer_get_iter_at_mark(buffer, &where, mark);
                setMark(buffer, getMarkName(newId, start), where);
                p_gtk_text_buffer_delete_mark(buffer, mark);
            }
        }
        appendTrailer(conv, newId, trailer);
    }
}

bool scrollToMessage(PidginIMHtml *imhtml, MessageId messageId)
{
    GtkTextBuffer *buffer = (g_pidginUi && imhtml) ? imhtml->text_buffer : nullptr;
    GtkTextMark   *start  = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, true).c_str()) : nullptr;
    GtkTextMark   *end    = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, false).c_str()) : nullptr;
    if (!start || !end)
        return false;

    GtkTextIter startIter, endIter;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &startIter, start);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &endIter, end);
    p_gtk_text_buffer_select_range(buffer, &startIter, &endIter);
    // The message's beginning a little below the top, so what came just before shows too
    p_gtk_text_view_scroll_to_mark(reinterpret_cast<GtkTextView *>(imhtml), start, 0.0, TRUE, 0.0, 0.1);
    return true;
}

static void itemActivated(GtkWidget *item, gpointer data)
{
    (*static_cast<std::function<void()> *>(data))();
}

static void deleteAction(gpointer data, GClosure *closure)
{
    delete static_cast<std::function<void()> *>(data);
}

// In a grid of the given number of columns, if any, where an item with a submenu takes a row
static GtkWidget *makeMenu(const std::vector<MenuItem> &items, unsigned columns)
{
    GtkWidget *menu   = p_gtk_menu_new();
    unsigned   column = 0, row = 0;
    for (const MenuItem &item: items) {
        GtkWidget *widget = item.checkable ? p_gtk_check_menu_item_new_with_label(item.label.c_str())
                                           : p_gtk_menu_item_new_with_label(item.label.c_str());
        if (item.checkable)
            p_gtk_check_menu_item_set_active(reinterpret_cast<GtkCheckMenuItem *>(widget), item.checked);
        if (!item.submenu.empty())
            p_gtk_menu_item_set_submenu(reinterpret_cast<GtkMenuItem *>(widget),
                                        makeMenu(item.submenu, item.columns));
        else if (item.action)
            p_g_signal_connect_data(widget, "activate", G_CALLBACK(itemActivated),
                                    new std::function<void()>(item.action), deleteAction, GConnectFlags(0));
        else
            p_gtk_widget_set_sensitive(widget, FALSE);

        if (!columns)
            p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu), widget);
        else if (!item.submenu.empty()) {
            if (column) {
                column = 0;
                row++;
            }
            p_gtk_menu_attach(reinterpret_cast<GtkMenu *>(menu), widget, 0, columns, row, row + 1);
            row++;
        } else {
            p_gtk_menu_attach(reinterpret_cast<GtkMenu *>(menu), widget, column, column + 1, row, row + 1);
            if (++column == columns) {
                column = 0;
                row++;
            }
        }
    }
    return menu;
}

static gboolean destroyMenu(gpointer menu)
{
    p_gtk_widget_destroy(static_cast<GtkWidget *>(menu));
    return FALSE;
}

// The chosen item is activated after the menu goes away
static void menuDeactivated(GtkWidget *menu, gpointer data)
{
    g_idle_add(destroyMenu, menu);
}

void popUpMenu(const std::vector<MenuItem> &items)
{
    if (!g_pidginUi || items.empty())
        return;
    GtkWidget *menu = makeMenu(items, 0);
    p_g_signal_connect_data(menu, "deactivate", G_CALLBACK(menuDeactivated), NULL, NULL, GConnectFlags(0));
    p_gtk_widget_show_all(menu);
    // Where the pointer is, though no button is pressed by now
    p_gtk_menu_popup(reinterpret_cast<GtkMenu *>(menu), NULL, NULL, NULL, NULL, 0, GDK_CURRENT_TIME);
}

#else

void pidginUiLoad(PurplePlugin *plugin)
{
}

void pidginUiUnload()
{
}

bool pidginUiFound()
{
    return false;
}

MessageMarkScope::MessageMarkScope(MessageId messageId, const std::string &trailer)
{
}

MessageMarkScope::~MessageMarkScope()
{
}

MessageMarks findMessageMarks(PurpleConversation *conv, MessageId messageId)
{
    return MessageMarks();
}

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId, const std::string &trailer)
{
}

bool scrollToMessage(PidginIMHtml *imhtml, MessageId messageId)
{
    return false;
}

void popUpMenu(const std::vector<MenuItem> &items)
{
}

#endif
