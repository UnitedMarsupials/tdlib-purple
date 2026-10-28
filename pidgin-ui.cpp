#include "pidgin-ui.h"
#include "client-utils.h"
#include "config.h"
#include <string>

#ifndef NoPidgin
#include <gtkconv.h>
#include <gdk/gdkkeysyms.h>

// Pidgin's and GTK's functions, looked up by name: their types are those the headers declare
#define FRONT_END_FUNCTION(name) static decltype(&name) p_##name
#define LOOK_UP(name) (p_##name = reinterpret_cast<decltype(&name)>(findFrontEndSymbol(#name)))

FRONT_END_FUNCTION(pidgin_conversations_get_handle);
FRONT_END_FUNCTION(pidgin_conversations_get_conv_ui_ops);
FRONT_END_FUNCTION(gtk_text_buffer_get_end_iter);
FRONT_END_FUNCTION(gtk_text_iter_get_char);
FRONT_END_FUNCTION(gtk_text_iter_forward_char);
FRONT_END_FUNCTION(gtk_text_iter_backward_char);
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
FRONT_END_FUNCTION(gtk_widget_set_tooltip_text);
FRONT_END_FUNCTION(gtk_bin_get_child);
FRONT_END_FUNCTION(gtk_label_set_markup);
FRONT_END_FUNCTION(gtk_widget_show_all);
FRONT_END_FUNCTION(gtk_widget_destroy);
FRONT_END_FUNCTION(g_signal_connect_data);
FRONT_END_FUNCTION(gtk_imhtml_get_markup);
FRONT_END_FUNCTION(gtk_imhtml_delete);
FRONT_END_FUNCTION(gtk_imhtml_append_text_with_images);
FRONT_END_FUNCTION(gtk_widget_modify_base);
FRONT_END_FUNCTION(gtk_widget_grab_focus);
FRONT_END_FUNCTION(g_signal_handler_disconnect);
FRONT_END_FUNCTION(gtk_text_buffer_apply_tag_by_name);
FRONT_END_FUNCTION(gtk_text_iter_forward_search);
FRONT_END_FUNCTION(gtk_text_iter_compare);
FRONT_END_FUNCTION(gtk_text_buffer_get_text);
FRONT_END_FUNCTION(g_object_get_data);
FRONT_END_FUNCTION(g_object_set_data_full);

static bool g_pidginUi = false;

// The message being written, and the first conversation it goes to. Written in place of its
// earlier version, if replacing, with the note after its first part.
struct MessageBeingWritten {
    MessageId           messageId;
    std::string         prefix;
    PurpleConversation *conv = nullptr;
    bool                replacing = false;
    bool                anew      = false; // the whole message, not a part coming later
    std::string         editNote;
    bool                markChanges = false; // its text, words added and removed marked
    bool                bodyMarked = false;
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

// Where a message starts, before its header; where its own text starts, after the header; where it
// ends; and where its prefix went, if it has one
enum class MarkKind {
    Start,
    Body,
    End,
    Prefix,
    PrefixEnd,
    Reactions // the end of the line of reactions after the message, if it has one
};

static std::string getMarkName(MessageId messageId, MarkKind kind)
{
    const char *prefix = (kind == MarkKind::Start) ? "tdlib-message-start-" :
                         (kind == MarkKind::Body)  ? "tdlib-message-body-" :
                         (kind == MarkKind::End)   ? "tdlib-message-end-" :
                         (kind == MarkKind::Prefix) ? "tdlib-message-prefix-" :
                         (kind == MarkKind::PrefixEnd) ? "tdlib-message-prefix-end-" : "tdlib-message-reactions-";
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

// Put by us before a message's own text, after Pidgin's header, so as to find where it starts; it
// is gone once found. A word joiner, which shows as nothing, should it stay.
static const char BODY_MARKER[] = "\xe2\x81\xa0";

// The message's text and its end, if it is in the tab still, and not trimmed away
static bool getBody(GtkTextBuffer *buffer, MessageId messageId, GtkTextIter &body, GtkTextIter &end)
{
    GtkTextMark *bodyMark = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Body).c_str());
    GtkTextMark *endMark  = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::End).c_str());
    if (!bodyMark || !endMark)
        return false;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &body, bodyMark);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &end, endMark);
    return p_gtk_text_iter_compare(&body, &end) < 0;
}

// Inserts HTML at a mark, moving it past what is inserted
static void insertAtMark(GtkIMHtml *imhtml, GtkTextMark *mark, const char *html)
{
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextIter    where;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &where, mark);
    // Right gravity: it stays after what is inserted
    GtkTextMark *after = p_gtk_text_buffer_create_mark(buffer, NULL, &where, FALSE);
    p_gtk_imhtml_insert_html_at_iter(imhtml, html, GTK_IMHTML_NO_SCROLL, &where);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &where, after);
    p_gtk_text_buffer_move_mark(buffer, mark, &where);
    p_gtk_text_buffer_delete_mark(buffer, after);
}

static char *getPlainText(const char *html)
{
    char *stripped = purple_markup_strip_html(html);
    char *text     = purple_unescape_html(stripped);
    g_free(stripped);
    return text;
}

// An edited message's latest version, and the one before, kept with the tab's buffer, so that the
// change can be marked or not, and the next one marked against it
struct EditedText {
    std::string previous; // plain text
    std::string html;
    std::string editNote;
    bool        marked = true;
};

static std::string getEditedTextKey(MessageId messageId)
{
    return "tdlib-message-text-" + std::to_string(messageId.value());
}

static EditedText *getEditedText(GtkTextBuffer *buffer, MessageId messageId)
{
    return static_cast<EditedText *>(p_g_object_get_data(reinterpret_cast<GObject *>(buffer),
                                                          getEditedTextKey(messageId).c_str()));
}

static std::string formatEditedText(const EditedText &edited)
{
    return edited.marked ? markChanges(edited.previous, edited.html) : edited.html;
}

// The new version of an edited message's text, the change from the previous one marked, unless
// the earlier version was shown without. The previous one is what the tab shows, unless kept.
static std::string markEditedText(GtkTextBuffer *buffer, const GtkTextIter &body, const GtkTextIter &end,
                                  const char *html)
{
    MessageId   messageId = g_messageBeingWritten->messageId;
    EditedText *edited    = new EditedText;
    edited->html     = html;
    edited->editNote = g_messageBeingWritten->editNote;
    if (const EditedText *kept = getEditedText(buffer, messageId)) {
        char *previous = getPlainText(kept->html.c_str());
        edited->previous = previous;
        edited->marked   = kept->marked;
        g_free(previous);
    } else {
        char *shown = p_gtk_text_buffer_get_text(buffer, &body, &end, FALSE);
        char *note  = getPlainText(edited->editNote.c_str());
        edited->previous = shown;
        // Written after an earlier change, by a version without these marks
        if ((edited->previous.size() >= strlen(note)) &&
            (edited->previous.compare(edited->previous.size() - strlen(note), std::string::npos, note) == 0))
            edited->previous.resize(edited->previous.size() - strlen(note));
        g_free(note);
        g_free(shown);
    }
    p_g_object_set_data_full(reinterpret_cast<GObject *>(buffer), getEditedTextKey(messageId).c_str(), edited,
                             [](gpointer data) { delete static_cast<EditedText *>(data); });
    return formatEditedText(*edited);
}

// A part of the message written over its earlier version: the first replaces all of its text,
// header aside; the next ones follow it
static void replaceBody(PurpleConversation *conv, const char *html)
{
    GtkIMHtml     *imhtml  = getIMHtml(conv);
    GtkTextBuffer *buffer  = imhtml->text_buffer;
    MessageId      id      = g_messageBeingWritten->messageId;
    GtkTextMark   *endMark = p_gtk_text_buffer_get_mark(buffer, getMarkName(id, MarkKind::End).c_str());
    std::string    text;
    if (!g_messageBeingWritten->bodyMarked) {
        GtkTextIter body, end;
        if (!getBody(buffer, id, body, end))
            return;
        text = g_messageBeingWritten->markChanges ? markEditedText(buffer, body, end, html) : std::string(html);
        p_gtk_imhtml_delete(imhtml, &body, &end);
        g_messageBeingWritten->bodyMarked = true;
        text += g_messageBeingWritten->editNote;
    } else
        text = std::string("<br>") + html;
    insertAtMark(imhtml, endMark, text.c_str());
}

// Emitted by Pidgin just before it writes into a tab, for IM and chat messages alike; returning
// TRUE stops it from writing
static gboolean displayingMessage(PurpleAccount *account, const char *who, char **message,
                                  PurpleConversation *conv, PurpleMessageFlags flags)
{
    if (!g_messageBeingWritten)
        return FALSE;
    if (!g_messageBeingWritten->conv) {
        GtkTextBuffer *buffer = getTextBuffer(conv);
        if (!buffer)
            return FALSE;
        g_messageBeingWritten->conv = conv;
        GtkTextIter body, end;
        if (g_messageBeingWritten->replacing && !getBody(buffer, g_messageBeingWritten->messageId, body, end))
            // Gone from the tab since: shown anew instead
            g_messageBeingWritten->replacing = false;
        MessageId    id    = g_messageBeingWritten->messageId;
        GtkTextMark *start = p_gtk_text_buffer_get_mark(buffer, getMarkName(id, MarkKind::Start).c_str());
        if (g_messageBeingWritten->anew) {
            // What went with an earlier showing goes with this one instead
            for (MarkKind kind: {MarkKind::Body, MarkKind::Prefix, MarkKind::PrefixEnd, MarkKind::Reactions}) {
                GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, getMarkName(id, kind).c_str());
                if (mark)
                    p_gtk_text_buffer_delete_mark(buffer, mark);
            }
            // Shown as it is now, the change unmarked
            p_g_object_set_data_full(reinterpret_cast<GObject *>(buffer), getEditedTextKey(id).c_str(),
                                     nullptr, nullptr);
        } else if (start && !g_messageBeingWritten->replacing)
            // A part coming later, such as a downloaded picture, extends the message
            g_messageBeingWritten->bodyMarked = true;
        if (!g_messageBeingWritten->replacing && (g_messageBeingWritten->anew || !start)) {
            // A mark, not an offset: Pidgin may yet trim the tab's oldest lines
            p_gtk_text_buffer_get_end_iter(buffer, &end);
            setMark(buffer, getMarkName(id, MarkKind::Start), end);
        }
    }
    if (g_messageBeingWritten->conv != conv)
        return FALSE;
    if (g_messageBeingWritten->replacing) {
        replaceBody(conv, *message);
        return TRUE;
    }
    if (!g_messageBeingWritten->bodyMarked) {
        gchar *marked = g_strconcat(BODY_MARKER, *message, NULL);
        g_free(*message);
        *message = marked;
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
        GtkTextMark *startMark = p_gtk_text_buffer_get_mark(buffer, getMarkName(g_messageBeingWritten->messageId, true).c_str());
        GtkTextIter  start, end;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &start, startMark);
        // Pidgin ends the previous line only after saying a message is coming
        if (p_gtk_text_iter_get_char(&start) == '\n')
            p_gtk_text_iter_forward_char(&start);
        p_gtk_text_buffer_get_end_iter(buffer, &end);
        setMark(buffer, getMarkName(g_messageBeingWritten->messageId, true), start);
        setMark(buffer, getMarkName(g_messageBeingWritten->messageId, false), end);

        // Where the first part's text starts, past the header
        GtkTextIter marker, pastMarker;
        if (!g_messageBeingWritten->bodyMarked &&
            p_gtk_text_iter_forward_search(&start, BODY_MARKER, GTK_TEXT_SEARCH_TEXT_ONLY, &marker, &pastMarker, &end))
        {
            p_gtk_imhtml_delete(getIMHtml(conv), &marker, &pastMarker);
            setMark(buffer, getMarkName(g_messageBeingWritten->messageId, MarkKind::Body), marker);
        }
        g_messageBeingWritten->bodyMarked = true;
    }
}

// Longer than "(Mon, 26 Oct 2026 23:41:54)", as dates go in old messages
static constexpr unsigned MAX_TIMESTAMP_LENGTH = 48;

// Inserts what precedes a message after its timestamp, once. The start mark stays before it, so the
// message takes it in.
static void insertPrefix(PurpleConversation *conv, MessageId messageId, const std::string &prefix)
{
    GtkIMHtml     *imhtml = getIMHtml(conv);
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *start  = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, true).c_str()) : nullptr;
    if (prefix.empty() || !start || p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Prefix).c_str()))
        return;

    GtkTextIter where;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &where, start);
    // Between the timestamp, if Pidgin shows one, and the name, where other plugins put theirs
    if (p_gtk_text_iter_get_char(&where) == '(') {
        GtkTextIter after = where;
        for (unsigned i = 0; (i < MAX_TIMESTAMP_LENGTH) && p_gtk_text_iter_forward_char(&after); i++) {
            gunichar c = p_gtk_text_iter_get_char(&after);
            if (c == '\n')
                break;
            if (c == ')') {
                p_gtk_text_iter_forward_char(&after);
                if (p_gtk_text_iter_get_char(&after) == ' ')
                    p_gtk_text_iter_forward_char(&after);
                where = after;
                break;
            }
        }
    }
    p_gtk_text_buffer_create_mark(buffer, getMarkName(messageId, MarkKind::Prefix).c_str(), &where, TRUE);
    // Right gravity: past the prefix once it is in, so that it can be taken out again
    p_gtk_text_buffer_create_mark(buffer, getMarkName(messageId, MarkKind::PrefixEnd).c_str(), &where, FALSE);
    // Into the tab only, not the log
    p_gtk_imhtml_insert_html_at_iter(imhtml, prefix.c_str(), GTK_IMHTML_NO_SCROLL, &where);
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
                 LOOK_UP(gtk_text_buffer_get_end_iter) && LOOK_UP(gtk_text_iter_get_char) &&
                 LOOK_UP(gtk_text_iter_forward_char) && LOOK_UP(gtk_text_iter_backward_char) &&
                 LOOK_UP(gtk_text_buffer_get_mark) &&
                 LOOK_UP(gtk_text_buffer_create_mark) && LOOK_UP(gtk_text_buffer_move_mark) &&
                 LOOK_UP(gtk_text_buffer_delete_mark) && LOOK_UP(gtk_text_buffer_get_iter_at_mark) &&
                 LOOK_UP(gtk_text_buffer_select_range) && LOOK_UP(gtk_text_view_scroll_to_mark) &&
                 LOOK_UP(gtk_imhtml_insert_html_at_iter) && LOOK_UP(gtk_menu_new) &&
                 LOOK_UP(gtk_menu_item_new_with_label) && LOOK_UP(gtk_check_menu_item_new_with_label) &&
                 LOOK_UP(gtk_check_menu_item_set_active) && LOOK_UP(gtk_menu_item_set_submenu) &&
                 LOOK_UP(gtk_menu_shell_append) && LOOK_UP(gtk_menu_attach) &&
                 LOOK_UP(gtk_menu_popup) &&
                 LOOK_UP(gtk_widget_set_sensitive) && LOOK_UP(gtk_widget_set_tooltip_text) &&
                 LOOK_UP(gtk_bin_get_child) && LOOK_UP(gtk_label_set_markup) &&
                 LOOK_UP(gtk_widget_show_all) &&
                 LOOK_UP(gtk_widget_destroy) && LOOK_UP(g_signal_connect_data) &&
                 LOOK_UP(gtk_text_buffer_apply_tag_by_name) &&
                 LOOK_UP(gtk_text_iter_forward_search) &&
                 LOOK_UP(gtk_text_iter_compare) && LOOK_UP(gtk_imhtml_get_markup) &&
                 LOOK_UP(gtk_imhtml_delete) && LOOK_UP(gtk_imhtml_append_text_with_images) &&
                 LOOK_UP(gtk_widget_modify_base) && LOOK_UP(gtk_widget_grab_focus) &&
                 LOOK_UP(g_signal_handler_disconnect) && LOOK_UP(gtk_text_buffer_get_text) &&
                 LOOK_UP(g_object_get_data) && LOOK_UP(g_object_set_data_full);
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

MessageMarkScope::MessageMarkScope(MessageId messageId, const std::string &prefix, const char *editNote, bool anew,
                                   bool markChanges)
{
    if (g_pidginUi && messageId.valid() && !g_messageBeingWritten) {
        g_messageBeingWritten = new MessageBeingWritten;
        g_messageBeingWritten->messageId = messageId;
        g_messageBeingWritten->prefix    = prefix;
        g_messageBeingWritten->anew      = anew && !editNote;
        if (editNote) {
            g_messageBeingWritten->replacing = true;
            g_messageBeingWritten->editNote  = editNote;
            g_messageBeingWritten->markChanges = markChanges;
        }
        m_active = true;
    }
}

MessageMarkScope::~MessageMarkScope()
{
    if (m_active) {
        if (g_messageBeingWritten->conv)
            insertPrefix(g_messageBeingWritten->conv, g_messageBeingWritten->messageId,
                         g_messageBeingWritten->prefix);
        delete g_messageBeingWritten;
        g_messageBeingWritten = nullptr;
    }
}

void showMessageReactions(PurpleConversation *conv, MessageId messageId, const std::string &html)
{
    GtkIMHtml     *imhtml  = getIMHtml(conv);
    GtkTextBuffer *buffer  = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *endMark = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::End).c_str()) : nullptr;
    if (!endMark)
        return;
    GtkTextIter  end, areaEnd;
    GtkTextMark *areaEndMark = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Reactions).c_str());
    p_gtk_text_buffer_get_iter_at_mark(buffer, &end, endMark);
    if (areaEndMark) {
        p_gtk_text_buffer_get_iter_at_mark(buffer, &areaEnd, areaEndMark);
        if (p_gtk_text_iter_compare(&end, &areaEnd) < 0) {
            p_gtk_imhtml_delete(imhtml, &end, &areaEnd);
            p_gtk_text_buffer_get_iter_at_mark(buffer, &end, endMark);
        }
        p_gtk_text_buffer_move_mark(buffer, areaEndMark, &end);
    } else
        // Right gravity: it goes past what is inserted where it is
        areaEndMark = p_gtk_text_buffer_create_mark(buffer, getMarkName(messageId, MarkKind::Reactions).c_str(),
                                                    &end, FALSE);
    if (!html.empty()) {
        // Into the tab only, not the log
        std::string line = "<br>" + html;
        p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GTK_IMHTML_NO_SCROLL, &end);
    }
}

bool canRedrawConversation(PurpleConversation *conv)
{
    return getIMHtml(conv) != nullptr;
}

// The edit in progress in a conversation's input field, and what the field held before
struct EntryEditState {
    EntryEdit             edit;
    std::string           draft;
    gulong                keyHandler = 0;
    std::function<void()> cancelled;
};
static const char ENTRY_EDIT_KEY[] = "tdlib-purple-entry-edit";

static GtkWidget *getEntry(PurpleConversation *conv)
{
    return getIMHtml(conv) ? PIDGIN_CONVERSATION(conv)->entry : nullptr;
}

static void setEntryText(GtkWidget *entry, const char *html)
{
    p_gtk_imhtml_delete(reinterpret_cast<GtkIMHtml *>(entry), NULL, NULL);
    if (html && *html)
        p_gtk_imhtml_append_text_with_images(reinterpret_cast<GtkIMHtml *>(entry), html, GtkIMHtmlOptions(0), NULL);
}

// A quarter of the way to amber from the theme's own background, light or dark
static void tintEntry(GtkWidget *entry, bool tint)
{
    if (!tint) {
        p_gtk_widget_modify_base(entry, GTK_STATE_NORMAL, NULL);
        return;
    }
    GdkColor base   = entry->style->base[GTK_STATE_NORMAL];
    GdkColor tinted = {0, guint16(base.red / 4 * 3 + 0xFFFF / 4), guint16(base.green / 4 * 3 + 0xC0C0 / 4),
                       guint16(base.blue / 4 * 3)};
    p_gtk_widget_modify_base(entry, GTK_STATE_NORMAL, &tinted);
}

static gboolean entryKeyPressed(GtkWidget *entry, GdkEventKey *event, gpointer conv)
{
    auto *state = static_cast<EntryEditState *>(purple_conversation_get_data(static_cast<PurpleConversation *>(conv),
                                                                            ENTRY_EDIT_KEY));
    if (!state || (event->keyval != GDK_Escape))
        return FALSE;
    std::function<void()> cancelled = std::move(state->cancelled);
    endEntryEdit(static_cast<PurpleConversation *>(conv), nullptr);
    if (cancelled)
        cancelled();
    return TRUE;
}

bool beginEntryEdit(PurpleConversation *conv, const EntryEdit &edit, const std::string &text,
                    std::function<void()> cancelled)
{
    GtkWidget *entry = getEntry(conv);
    if (!entry)
        return false;
    auto *state = static_cast<EntryEditState *>(purple_conversation_get_data(conv, ENTRY_EDIT_KEY));
    // Another edit begun meanwhile keeps the draft from before the first
    if (!state) {
        state = new EntryEditState;
        gchar *draft = p_gtk_imhtml_get_markup(reinterpret_cast<GtkIMHtml *>(entry));
        state->draft = draft ? draft : "";
        g_free(draft);
        state->keyHandler = p_g_signal_connect_data(entry, "key-press-event", G_CALLBACK(entryKeyPressed), conv,
                                                    NULL, GConnectFlags(0));
        purple_conversation_set_data(conv, ENTRY_EDIT_KEY, state);
    }
    state->edit      = edit;
    state->cancelled = std::move(cancelled);

    gchar      *escaped = g_markup_escape_text(text.c_str(), -1);
    std::string html;
    for (const char *c = escaped; *c; c++)
        if (*c == '\n')
            html += "<br>";
        else
            html += *c;
    g_free(escaped);
    setEntryText(entry, html.c_str());
    tintEntry(entry, true);
    p_gtk_widget_grab_focus(entry);
    return true;
}

namespace {
struct DraftToRestore {
    PurpleConversation *conv;
    std::string         draft;
};
}

// Once Pidgin has emptied the field after sending, or after a command
static gboolean restoreDraft(gpointer data)
{
    std::unique_ptr<DraftToRestore> restore(static_cast<DraftToRestore *>(data));
    GtkWidget *entry = g_list_find(purple_get_conversations(), restore->conv) ? getEntry(restore->conv) : nullptr;
    // Emptied too, if there was none, after Esc
    if (entry)
        setEntryText(entry, restore->draft.c_str());
    return FALSE;
}

bool isEntryEditing(PurpleConversation *conv, MessageId messageId)
{
    auto *state = conv ? static_cast<EntryEditState *>(purple_conversation_get_data(conv, ENTRY_EDIT_KEY)) : nullptr;
    return state && (state->edit.messageId == messageId);
}

bool endEntryEdit(PurpleConversation *conv, EntryEdit *edit)
{
    auto *state = conv ? static_cast<EntryEditState *>(purple_conversation_get_data(conv, ENTRY_EDIT_KEY)) : nullptr;
    if (!state)
        return false;
    if (edit)
        *edit = state->edit;
    purple_conversation_set_data(conv, ENTRY_EDIT_KEY, NULL);
    GtkWidget *entry = getEntry(conv);
    if (entry) {
        p_g_signal_handler_disconnect(entry, state->keyHandler);
        tintEntry(entry, false);
        g_idle_add(restoreDraft, new DraftToRestore{conv, std::move(state->draft)});
    }
    delete state;
    return true;
}

bool canReplaceMessage(PurpleConversation *conv, MessageId messageId)
{
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    body, end;
    return buffer && getBody(buffer, messageId, body, end);
}

bool canMarkChanges(PurpleConversation *conv, MessageId messageId, bool &marked)
{
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    body, end;
    EditedText    *edited = (buffer && getBody(buffer, messageId, body, end)) ? getEditedText(buffer, messageId)
                                                                            : nullptr;
    if (edited)
        marked = edited->marked;
    return edited;
}

void setChangesMarked(PurpleConversation *conv, MessageId messageId, bool marked)
{
    GtkIMHtml     *imhtml = getIMHtml(conv);
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextIter    body, end;
    EditedText    *edited = (buffer && getBody(buffer, messageId, body, end)) ? getEditedText(buffer, messageId)
                                                                            : nullptr;
    if (!edited)
        return;
    edited->marked = marked;
    p_gtk_imhtml_delete(imhtml, &body, &end);
    GtkTextMark *endMark = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::End).c_str());
    insertAtMark(imhtml, endMark, (formatEditedText(*edited) + edited->editNote).c_str());
}

void strikeOutMessage(PurpleConversation *conv, MessageId messageId)
{
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    body, end;
    if (!buffer || !getBody(buffer, messageId, body, end))
        return;
    // GtkIMHtml's own, as for <s>: a tag of ours would need a name, as GtkIMHtml compares
    // the names of all tags with strncmp() when it draws, and crashes on one without
    p_gtk_text_buffer_apply_tag_by_name(buffer, "STRIKE", &body, &end);

    // Nothing is left to do to it, so its menu goes
    GtkTextMark *prefix    = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Prefix).c_str());
    GtkTextMark *prefixEnd = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::PrefixEnd).c_str());
    if (prefix && prefixEnd) {
        GtkTextIter from, to;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &from, prefix);
        p_gtk_text_buffer_get_iter_at_mark(buffer, &to, prefixEnd);
        p_gtk_imhtml_delete(getIMHtml(conv), &from, &to);
    }
}

void removeMessage(PurpleConversation *conv, MessageId messageId)
{
    GtkIMHtml     *imhtml = getIMHtml(conv);
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextIter    body, end;
    GtkTextMark   *start  = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Start).c_str()) : nullptr;
    if (!start || !getBody(buffer, messageId, body, end))
        return;
    GtkTextIter  from, to;
    GtkTextMark *reactionsEnd = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Reactions).c_str());
    p_gtk_text_buffer_get_iter_at_mark(buffer, &from, start);
    to = end;
    if (reactionsEnd) {
        GtkTextIter areaEnd;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &areaEnd, reactionsEnd);
        if (p_gtk_text_iter_compare(&to, &areaEnd) < 0)
            to = areaEnd;
    }
    // With the line break before it, which Pidgin put there for it
    if (p_gtk_text_iter_backward_char(&from) && (p_gtk_text_iter_get_char(&from) != '\n'))
        p_gtk_text_iter_forward_char(&from);
    p_gtk_imhtml_delete(imhtml, &from, &to);
    for (MarkKind kind: {MarkKind::Start, MarkKind::Body, MarkKind::End, MarkKind::Prefix, MarkKind::PrefixEnd,
                         MarkKind::Reactions})
    {
        GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, kind).c_str());
        if (mark)
            p_gtk_text_buffer_delete_mark(buffer, mark);
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

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId, const std::string &prefix)
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
        for (MarkKind kind: {MarkKind::Start, MarkKind::Body, MarkKind::End}) {
            GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, getMarkName(oldId, kind).c_str());
            if (mark) {
                GtkTextIter where;
                p_gtk_text_buffer_get_iter_at_mark(buffer, &where, mark);
                setMark(buffer, getMarkName(newId, kind), where);
                p_gtk_text_buffer_delete_mark(buffer, mark);
            }
        }
        insertPrefix(conv, newId, prefix);
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
        if (item.dimmed) {
            gchar *markup = g_markup_printf_escaped("<span alpha=\"40%%\">%s</span>", item.label.c_str());
            p_gtk_label_set_markup(reinterpret_cast<GtkLabel *>(p_gtk_bin_get_child(reinterpret_cast<GtkBin *>(widget))),
                                   markup);
            g_free(markup);
        }
        if (!item.tooltip.empty())
            p_gtk_widget_set_tooltip_text(widget, item.tooltip.c_str());
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

MessageMarkScope::MessageMarkScope(MessageId messageId, const std::string &prefix, const char *editNote, bool anew,
                                   bool markChanges)
{
}

MessageMarkScope::~MessageMarkScope()
{
}

void showMessageReactions(PurpleConversation *conv, MessageId messageId, const std::string &html)
{
}

bool beginEntryEdit(PurpleConversation *conv, const EntryEdit &edit, const std::string &text,
                    std::function<void()> cancelled)
{
    return false;
}

bool endEntryEdit(PurpleConversation *conv, EntryEdit *edit)
{
    return false;
}

bool isEntryEditing(PurpleConversation *conv, MessageId messageId)
{
    return false;
}

bool canRedrawConversation(PurpleConversation *conv)
{
    return false;
}

bool canReplaceMessage(PurpleConversation *conv, MessageId messageId)
{
    return false;
}

bool canMarkChanges(PurpleConversation *conv, MessageId messageId, bool &marked)
{
    return false;
}

void setChangesMarked(PurpleConversation *conv, MessageId messageId, bool marked)
{
}

void strikeOutMessage(PurpleConversation *conv, MessageId messageId)
{
}

void removeMessage(PurpleConversation *conv, MessageId messageId)
{
}

MessageMarks findMessageMarks(PurpleConversation *conv, MessageId messageId)
{
    return MessageMarks();
}

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId, const std::string &prefix)
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
