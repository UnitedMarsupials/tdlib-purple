#include "pidgin-ui.h"
#include "client-utils.h"
#include "config.h"
#include <string>

#ifndef NoPidgin
#include <gtkconv.h>

// Pidgin's and GTK's functions, looked up by name: their types are those the headers declare
#define FRONT_END_FUNCTION(name) static decltype(&name) p_##name
#define LOOK_UP(name) p_##name = reinterpret_cast<decltype(&name)>(findFrontEndSymbol(#name))

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

static bool g_pidginUi = false;

// The message being written, and where its text starts in the first conversation it goes to
struct MessageBeingWritten {
    MessageId           messageId;
    PurpleConversation *conv        = nullptr;
    gint                startOffset = 0;
};
static MessageBeingWritten *g_messageBeingWritten = nullptr;

// The text of a conversation's tab, if Pidgin shows it
static GtkTextBuffer *getTextBuffer(PurpleConversation *conv)
{
    if (!g_pidginUi || !conv || (purple_conversation_get_ui_ops(conv) != p_pidgin_conversations_get_conv_ui_ops()))
        return nullptr;
    PidginConversation *gtkconv = PIDGIN_CONVERSATION(conv);
    if (!gtkconv || !gtkconv->imhtml)
        return nullptr;
    // GTK_IMHTML() would call gtk_imhtml_get_type(), which the plugin does not link against
    return reinterpret_cast<GtkIMHtml *>(gtkconv->imhtml)->text_buffer;
}

static std::string getMarkName(MessageId messageId, bool start)
{
    return std::string(start ? "tdlib-message-start-" : "tdlib-message-end-") +
           std::to_string(messageId.value());
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
    LOOK_UP(pidgin_conversations_get_handle);
    LOOK_UP(pidgin_conversations_get_conv_ui_ops);
    LOOK_UP(gtk_text_buffer_get_end_iter);
    LOOK_UP(gtk_text_buffer_get_iter_at_offset);
    LOOK_UP(gtk_text_iter_get_offset);
    LOOK_UP(gtk_text_buffer_get_mark);
    LOOK_UP(gtk_text_buffer_create_mark);
    LOOK_UP(gtk_text_buffer_move_mark);
    LOOK_UP(gtk_text_buffer_delete_mark);
    LOOK_UP(gtk_text_buffer_get_iter_at_mark);
    LOOK_UP(gtk_text_buffer_select_range);
    LOOK_UP(gtk_text_view_scroll_to_mark);
    g_pidginUi = p_pidgin_conversations_get_handle && p_pidgin_conversations_get_conv_ui_ops &&
                 p_gtk_text_buffer_get_end_iter && p_gtk_text_buffer_get_iter_at_offset &&
                 p_gtk_text_iter_get_offset && p_gtk_text_buffer_get_mark &&
                 p_gtk_text_buffer_create_mark && p_gtk_text_buffer_move_mark &&
                 p_gtk_text_buffer_delete_mark &&
                 p_gtk_text_buffer_get_iter_at_mark && p_gtk_text_buffer_select_range &&
                 p_gtk_text_view_scroll_to_mark;
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

MessageMarkScope::MessageMarkScope(MessageId messageId)
{
    if (g_pidginUi && messageId.valid() && !g_messageBeingWritten) {
        g_messageBeingWritten = new MessageBeingWritten;
        g_messageBeingWritten->messageId = messageId;
        m_active = true;
    }
}

MessageMarkScope::~MessageMarkScope()
{
    if (m_active) {
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

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId)
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

MessageMarkScope::MessageMarkScope(MessageId messageId)
{
}

MessageMarkScope::~MessageMarkScope()
{
}

MessageMarks findMessageMarks(PurpleConversation *conv, MessageId messageId)
{
    return MessageMarks();
}

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId)
{
}

bool scrollToMessage(PidginIMHtml *imhtml, MessageId messageId)
{
    return false;
}

#endif
