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
FRONT_END_FUNCTION(gtk_text_iter_get_offset);
// For pasting a picture, without which pasting is as Pidgin has it
FRONT_END_FUNCTION(gtk_imhtml_get_type);
FRONT_END_FUNCTION(g_signal_lookup);
FRONT_END_FUNCTION(g_signal_add_emission_hook);
FRONT_END_FUNCTION(g_signal_remove_emission_hook);
FRONT_END_FUNCTION(g_value_get_object);
FRONT_END_FUNCTION(gtk_widget_get_clipboard);
FRONT_END_FUNCTION(gtk_clipboard_wait_is_image_available);
FRONT_END_FUNCTION(gtk_clipboard_wait_for_image);
FRONT_END_FUNCTION(gdk_pixbuf_save_to_buffer);
FRONT_END_FUNCTION(g_object_unref);
FRONT_END_FUNCTION(gtk_imhtml_insert_image_at_iter);
FRONT_END_FUNCTION(gtk_text_buffer_get_insert);
FRONT_END_FUNCTION(g_object_get_data);
FRONT_END_FUNCTION(g_object_set_data_full);
FRONT_END_FUNCTION(gtk_widget_get_pointer);
FRONT_END_FUNCTION(gtk_text_buffer_get_iter_at_offset);
FRONT_END_FUNCTION(gtk_text_view_window_to_buffer_coords);
FRONT_END_FUNCTION(gtk_text_view_get_iter_at_location);
FRONT_END_FUNCTION(gtk_text_iter_get_marks);
FRONT_END_FUNCTION(gtk_text_mark_get_name);
FRONT_END_FUNCTION(gtk_separator_menu_item_new);
FRONT_END_FUNCTION(gtk_widget_show);

static bool g_pidginUi = false;

// The message being written, and the first conversation it goes to. Written in place of its
// earlier version, if replacing, with the note after its first part.
struct MessageBeingWritten {
    MessageId           messageId;
    PurpleConversation *conv = nullptr;
    bool                replacing = false;
    bool                anew      = false; // the whole message, not a part coming later
    std::string         editNote;
    bool                markChanges = false; // its text, words added and removed marked
    bool                bodyMarked = false;
    bool                extending  = false; // a part coming after the message was shown
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

// Where a message starts, before its header; where its own text starts, after the header; and where it
// ends
enum class MarkKind {
    Start,
    Body,
    End,
    Reactions // the end of the line of reactions after the message, if it has one
};

static const char START_MARK_PREFIX[] = "tdlib-message-start-";

static std::string getMarkName(MessageId messageId, MarkKind kind)
{
    const char *prefix = (kind == MarkKind::Start) ? START_MARK_PREFIX :
                         (kind == MarkKind::Body)  ? "tdlib-message-body-" :
                         (kind == MarkKind::End)   ? "tdlib-message-end-" : "tdlib-message-reactions-";
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
static void insertAtMark(GtkIMHtml *imhtml, GtkTextMark *mark, const char *html,
                         GtkIMHtmlOptions options = GTK_IMHTML_NO_SCROLL)
{
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextIter    where;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &where, mark);
    // Right gravity: it stays after what is inserted
    GtkTextMark *after = p_gtk_text_buffer_create_mark(buffer, NULL, &where, FALSE);
    p_gtk_imhtml_insert_html_at_iter(imhtml, html, options, &where);
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

// A third of the way to the color from the theme's own background, light or dark, so that the
// text on it stays as legible
static std::string tintBackground(GtkIMHtml *imhtml, guint16 red, guint16 green, guint16 blue)
{
    GdkColor base = reinterpret_cast<GtkWidget *>(imhtml)->style->base[GTK_STATE_NORMAL];
    auto     mix  = [](guint16 from, guint16 to) { return (from / 3 * 2 + to / 3) >> 8; };
    char     color[8];
    snprintf(color, sizeof(color), "#%02x%02x%02x", mix(base.red, red), mix(base.green, green),
             mix(base.blue, blue));
    return color;
}

static std::string formatEditedText(GtkIMHtml *imhtml, const EditedText &edited)
{
    if (!edited.marked)
        return edited.html;
    // Colors, not strikes or underlines, which may be the sender's own formatting
    return markChanges(edited.previous, edited.html, tintBackground(imhtml, 0xFFFF, 0, 0),
                       tintBackground(imhtml, 0, 0xC0C0, 0));
}

// The new version of an edited message's text, the change from the previous one marked, unless
// the earlier version was shown without. The previous one is what the tab shows, unless kept.
static std::string markEditedText(GtkIMHtml *imhtml, const GtkTextIter &body, const GtkTextIter &end,
                                  const char *html)
{
    GtkTextBuffer *buffer = imhtml->text_buffer;
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
    return formatEditedText(imhtml, *edited);
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
        text = g_messageBeingWritten->markChanges ? markEditedText(imhtml, body, end, html) : std::string(html);
        p_gtk_imhtml_delete(imhtml, &body, &end);
        g_messageBeingWritten->bodyMarked = true;
        text += g_messageBeingWritten->editNote;
    } else
        text = std::string("<br>") + html;
    insertAtMark(imhtml, endMark, text.c_str());
}

static const char EDIT_OUTCOME[] = "tdlib-edit-outcome";
static void removeEditNotice(PurpleConversation *conv, const char *name);

// Emitted by Pidgin just before it writes into a tab, for IM and chat messages alike; returning
// TRUE stops it from writing
static gboolean displayingMessage(PurpleAccount *account, const char *who, char **message,
                                  PurpleConversation *conv, PurpleMessageFlags flags)
{
    // For the log only, which has it by now; nothing is shown if that was all
    std::string html = *message;
    bool        logOnly = takeLogOnly(html);
    if (takeLogMarker(html) || logOnly) {
        g_free(*message);
        *message = g_strdup(html.c_str());
    }
    if (logOnly && html.empty())
        return TRUE;
    // How an edit ended is of no interest once the conversation goes on
    if (flags & (PURPLE_MESSAGE_SEND | PURPLE_MESSAGE_RECV))
        removeEditNotice(conv, EDIT_OUTCOME);
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
            for (MarkKind kind: {MarkKind::Body, MarkKind::Reactions}) {
                GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, getMarkName(id, kind).c_str());
                if (mark)
                    p_gtk_text_buffer_delete_mark(buffer, mark);
            }
            // Shown as it is now, the change unmarked
            p_g_object_set_data_full(reinterpret_cast<GObject *>(buffer), getEditedTextKey(id).c_str(),
                                     nullptr, nullptr);
        } else if (start && !g_messageBeingWritten->replacing) {
            // A part coming later, such as a downloaded picture, extends the message
            g_messageBeingWritten->bodyMarked = true;
            g_messageBeingWritten->extending  = true;
        }
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
    // Where the message ends, before its reactions and what came after it, on a line of its own,
    // without a header again; the tab scrolled to it, if it is the last
    GtkTextBuffer *buffer  = g_messageBeingWritten->extending ? getTextBuffer(conv) : nullptr;
    GtkTextMark   *endMark = buffer ? p_gtk_text_buffer_get_mark(buffer,
                                          getMarkName(g_messageBeingWritten->messageId, MarkKind::End).c_str())
                                    : nullptr;
    if (endMark) {
        GtkTextIter end, bufferEnd;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &end, endMark);
        p_gtk_text_buffer_get_end_iter(buffer, &bufferEnd);
        bool last = !p_gtk_text_iter_compare(&end, &bufferEnd);
        insertAtMark(getIMHtml(conv), endMark, (std::string("<br>") + *message).c_str(),
                     last ? GtkIMHtmlOptions(0) : GTK_IMHTML_NO_SCROLL);
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

static guint g_connectSource = 0;

// Disconnected by libpurple when the plugin is unloaded
static void connectPaste();
static void connectPopup();

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
    connectPaste();
    connectPopup();
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
                 LOOK_UP(gtk_widget_set_sensitive) && LOOK_UP(gtk_widget_set_tooltip_text) &&
                 LOOK_UP(gtk_bin_get_child) && LOOK_UP(gtk_label_set_markup) &&
                 LOOK_UP(gtk_widget_show_all) &&
                 LOOK_UP(gtk_widget_destroy) && LOOK_UP(g_signal_connect_data) &&
                 LOOK_UP(gtk_text_buffer_apply_tag_by_name) &&
                 LOOK_UP(gtk_text_iter_forward_search) &&
                 LOOK_UP(gtk_text_iter_compare) && LOOK_UP(gtk_imhtml_get_markup) &&
                 LOOK_UP(gtk_imhtml_delete) && LOOK_UP(gtk_imhtml_append_text_with_images) &&
                 LOOK_UP(gtk_widget_modify_base) && LOOK_UP(gtk_widget_grab_focus) &&
                 LOOK_UP(g_signal_handler_disconnect) && LOOK_UP(gtk_text_buffer_get_text) && LOOK_UP(gtk_text_iter_get_offset) &&
                 LOOK_UP(g_object_get_data) && LOOK_UP(g_object_set_data_full) &&
                 LOOK_UP(gtk_text_buffer_get_iter_at_offset);
    purple_debug_info(config::pluginId, "Pidgin's conversation window: %s\n",
                      g_pidginUi ? "found" : "not found");
    if (!g_pidginUi)
        return;

    // A protocol plugin is loaded before Pidgin's window registers its signals: they are
    // connected to once the main loop is idle
    g_connectSource = g_idle_add(connectSignals, plugin);
}

static void disconnectPaste();
static void disconnectPopup();

void pidginUiUnload()
{
    disconnectPaste();
    disconnectPopup();
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

MessageMarkScope::MessageMarkScope(MessageId messageId, const char *editNote, bool anew, bool markChanges)
{
    if (g_pidginUi && messageId.valid() && !g_messageBeingWritten) {
        g_messageBeingWritten = new MessageBeingWritten;
        g_messageBeingWritten->messageId = messageId;
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
    std::string  name = getMarkName(messageId, MarkKind::Reactions);
    GtkTextIter  end, areaEnd;
    GtkTextMark *areaEndMark = p_gtk_text_buffer_get_mark(buffer, name.c_str());
    p_gtk_text_buffer_get_iter_at_mark(buffer, &end, endMark);
    if (areaEndMark) {
        p_gtk_text_buffer_get_iter_at_mark(buffer, &areaEnd, areaEndMark);
        if (p_gtk_text_iter_compare(&end, &areaEnd) < 0) {
            p_gtk_imhtml_delete(imhtml, &end, &areaEnd);
            p_gtk_text_buffer_get_iter_at_mark(buffer, &end, endMark);
        }
        p_gtk_text_buffer_delete_mark(buffer, areaEndMark);
    }
    // Right gravity only while the line goes in: kept so, at the end of the tab, the mark would go
    // past the next message too, which a change of reactions would then take away
    GtkTextMark *after = p_gtk_text_buffer_create_mark(buffer, NULL, &end, FALSE);
    if (!html.empty()) {
        // Into the tab only, not the log
        std::string line = "<br>" + html;
        p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GTK_IMHTML_NO_SCROLL, &end);
    }
    p_gtk_text_buffer_get_iter_at_mark(buffer, &areaEnd, after);
    p_gtk_text_buffer_create_mark(buffer, name.c_str(), &areaEnd, TRUE);
    p_gtk_text_buffer_delete_mark(buffer, after);
}

bool canRedrawConversation(PurpleConversation *conv)
{
    return getIMHtml(conv) != nullptr;
}

// A line at the end of the tab, its last character turning, and the marks around it and that
// character
static const char PROGRESS_KEY[]        = "tdlib-progress";
static const char PROGRESS_START[]      = "tdlib-progress-start";
static const char PROGRESS_SPINNER[]    = "tdlib-progress-spinner";
static const char PROGRESS_END[]        = "tdlib-progress-end";
static const char *const SPINNER_FRAMES[] = {"\xc2\xb7", "\xe2\x9c\xa2", "\xe2\x9c\xb3", "\xe2\x9c\xb6",
                                             "\xe2\x9c\xbb", "\xe2\x9c\xbd"};
enum { SPINNER_FRAME_COUNT = sizeof(SPINNER_FRAMES) / sizeof(SPINNER_FRAMES[0]), SPINNER_MS = 120 };

struct Progress {
    PurpleConversation *conv;
    guint               timer = 0;
    unsigned            step  = 0;
};

static GtkTextMark *getProgressMark(GtkTextBuffer *buffer, const char *name)
{
    return p_gtk_text_buffer_get_mark(buffer, name);
}

// Back and forth through the frames, as Claude Code's
static const char *getSpinnerFrame(unsigned step)
{
    unsigned position = step % (2 * SPINNER_FRAME_COUNT - 2);
    return SPINNER_FRAMES[(position < SPINNER_FRAME_COUNT) ? position : 2 * SPINNER_FRAME_COUNT - 2 - position];
}

static gboolean turnSpinner(gpointer data)
{
    Progress *progress = static_cast<Progress *>(data);
    GtkIMHtml *imhtml  = g_list_find(purple_get_conversations(), progress->conv) ? getIMHtml(progress->conv)
                                                                                 : nullptr;
    GtkTextMark *spinner = imhtml ? getProgressMark(imhtml->text_buffer, PROGRESS_SPINNER) : nullptr;
    GtkTextMark *end     = imhtml ? getProgressMark(imhtml->text_buffer, PROGRESS_END) : nullptr;
    if (!spinner || !end) {
        // The tab is gone
        progress->timer = 0;
        if (imhtml)
            purple_conversation_set_data(progress->conv, PROGRESS_KEY, nullptr);
        delete progress;
        return G_SOURCE_REMOVE;
    }
    GtkTextIter from, to;
    p_gtk_text_buffer_get_iter_at_mark(imhtml->text_buffer, &from, spinner);
    p_gtk_text_buffer_get_iter_at_mark(imhtml->text_buffer, &to, end);
    p_gtk_imhtml_delete(imhtml, &from, &to);
    // The end, which nothing written after the line moves, past the new frame
    insertAtMark(imhtml, end, getSpinnerFrame(++progress->step));
    return G_SOURCE_CONTINUE;
}

void appendTabLine(PurpleConversation *conv, const std::string &html)
{
    GtkIMHtml *imhtml = getIMHtml(conv);
    if (!imhtml)
        return;
    GtkTextIter where;
    p_gtk_text_buffer_get_end_iter(imhtml->text_buffer, &where);
    std::string line = (p_gtk_text_iter_get_offset(&where) ? "<br>" : "") + html;
    p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GtkIMHtmlOptions(0), &where);
}

void startProgress(PurpleConversation *conv, const std::string &html)
{
    GtkIMHtml *imhtml = getIMHtml(conv);
    if (!imhtml || purple_conversation_get_data(conv, PROGRESS_KEY))
        return;
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextIter    where;
    p_gtk_text_buffer_get_end_iter(buffer, &where);
    gint        offset = p_gtk_text_iter_get_offset(&where);
    std::string line   = (offset ? "<br>" : "") + std::string("<font color=\"#777777\">") + html + " </font>";
    p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GtkIMHtmlOptions(0), &where);
    // What is written at either end meanwhile stays outside: the start, set once the line is in, as
    // its right gravity would have it go past; the end, with the spinner's frame left before it
    p_gtk_text_buffer_get_iter_at_offset(buffer, &where, offset);
    p_gtk_text_buffer_create_mark(buffer, PROGRESS_START, &where, FALSE);
    p_gtk_text_buffer_get_end_iter(buffer, &where);
    p_gtk_text_buffer_create_mark(buffer, PROGRESS_SPINNER, &where, TRUE);
    GtkTextMark *end = p_gtk_text_buffer_create_mark(buffer, PROGRESS_END, &where, TRUE);
    insertAtMark(imhtml, end, getSpinnerFrame(0));

    Progress *progress = new Progress;
    progress->conv  = conv;
    progress->timer = g_timeout_add(SPINNER_MS, turnSpinner, progress);
    purple_conversation_set_data(conv, PROGRESS_KEY, progress);
}

void stopProgress(PurpleConversation *conv)
{
    Progress *progress = conv ? static_cast<Progress *>(purple_conversation_get_data(conv, PROGRESS_KEY)) : nullptr;
    if (!progress)
        return;
    purple_conversation_set_data(conv, PROGRESS_KEY, nullptr);
    g_source_remove(progress->timer);
    delete progress;
    GtkIMHtml *imhtml = getIMHtml(conv);
    if (!imhtml)
        return;
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextMark   *start  = getProgressMark(buffer, PROGRESS_START);
    GtkTextMark   *end    = getProgressMark(buffer, PROGRESS_END);
    if (start && end) {
        GtkTextIter from, to;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &from, start);
        p_gtk_text_buffer_get_iter_at_mark(buffer, &to, end);
        p_gtk_imhtml_delete(imhtml, &from, &to);
    }
    for (const char *name: {PROGRESS_START, PROGRESS_SPINNER, PROGRESS_END})
        if (GtkTextMark *mark = getProgressMark(buffer, name))
            p_gtk_text_buffer_delete_mark(buffer, mark);
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

// Pidgin pastes HTML or text only, a picture on the clipboard into a Telegram conversation's input
// field going nowhere. Its handler of "paste-clipboard" stops the signal, so the picture is put in
// by an emission hook, which comes before: at the cursor, as Pidgin's Insert menu has it, and sent
// as a photo the same way. Pidgin pastes the text, if there is any, after.
static guint  g_pasteSignal = 0;
static gulong g_pasteHook   = 0;

static PurpleConversation *findEntryConversation(gpointer entry)
{
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv = static_cast<PurpleConversation *>(item->data);
        if (!strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)), config::pluginId) &&
            (getEntry(conv) == entry))
            return conv;
    }
    return nullptr;
}

static gboolean pastingClipboard(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    gpointer   instance = paramCount ? p_g_value_get_object(&params[0]) : nullptr;
    GtkWidget *entry    = static_cast<GtkWidget *>(instance);
    if (!entry || !findEntryConversation(entry))
        return TRUE;
    GtkClipboard *clipboard = p_gtk_widget_get_clipboard(entry, GDK_SELECTION_CLIPBOARD);
    if (!clipboard || !p_gtk_clipboard_wait_is_image_available(clipboard))
        return TRUE;
    GdkPixbuf *pixbuf = p_gtk_clipboard_wait_for_image(clipboard);
    gchar     *data   = nullptr;
    gsize      size   = 0;
    GError    *error  = nullptr;
    if (pixbuf && p_gdk_pixbuf_save_to_buffer(pixbuf, &data, &size, "png", &error, NULL)) {
        // The store takes the data
        int id = purple_imgstore_add_with_id(data, size, "clipboard.png");
        GtkTextBuffer *buffer = reinterpret_cast<GtkIMHtml *>(entry)->text_buffer;
        GtkTextIter    cursor;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &cursor, p_gtk_text_buffer_get_insert(buffer));
        p_gtk_imhtml_insert_image_at_iter(reinterpret_cast<GtkIMHtml *>(entry), id, &cursor);
        purple_imgstore_unref_by_id(id);
        purple_debug_misc(config::pluginId, "Pasted a picture of %zu bytes\n", size_t(size));
    } else
        purple_debug_warning(config::pluginId, "Cannot paste the picture: %s\n",
                             error ? error->message : "none on the clipboard");
    if (error)
        g_error_free(error);
    if (pixbuf)
        p_g_object_unref(pixbuf);
    // Kept for the next paste
    return TRUE;
}

static void connectPaste()
{
    if (!LOOK_UP(gtk_imhtml_get_type) || !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook) || !LOOK_UP(g_value_get_object) ||
        !LOOK_UP(gtk_widget_get_clipboard) || !LOOK_UP(gtk_clipboard_wait_is_image_available) ||
        !LOOK_UP(gtk_clipboard_wait_for_image) || !LOOK_UP(gdk_pixbuf_save_to_buffer) ||
        !LOOK_UP(g_object_unref) || !LOOK_UP(gtk_imhtml_insert_image_at_iter) ||
        !LOOK_UP(gtk_text_buffer_get_insert))
    {
        purple_debug_warning(config::pluginId, "Cannot paste pictures: some of GTK is not found\n");
        return;
    }
    g_pasteSignal = p_g_signal_lookup("paste-clipboard", p_gtk_imhtml_get_type());
    if (g_pasteSignal)
        g_pasteHook = p_g_signal_add_emission_hook(g_pasteSignal, 0, pastingClipboard, nullptr, nullptr);
}

static void disconnectPaste()
{
    if (g_pasteHook)
        p_g_signal_remove_emission_hook(g_pasteSignal, g_pasteHook);
    g_pasteHook = 0;
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

// Lines about an edit, each between its own marks wherever other lines go meanwhile: what is
// inserted at either of them, such as the line of reactions of the message before it or a message
// coming after it, stays outside. One tells how to go about the edit while it lasts; the other,
// that it was given up, until the next one.
static const char EDIT_NOTICE[]  = "tdlib-edit-notice";

static void removeEditNotice(PurpleConversation *conv, const char *name)
{
    GtkIMHtml     *imhtml = getIMHtml(conv);
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *start  = buffer ? p_gtk_text_buffer_get_mark(buffer, (name + std::string("-start")).c_str()) : nullptr;
    GtkTextMark   *end    = buffer ? p_gtk_text_buffer_get_mark(buffer, (name + std::string("-end")).c_str()) : nullptr;
    if (start && end) {
        GtkTextIter from, to;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &from, start);
        p_gtk_text_buffer_get_iter_at_mark(buffer, &to, end);
        if (p_gtk_text_iter_compare(&from, &to) < 0)
            p_gtk_imhtml_delete(imhtml, &from, &to);
    }
    for (GtkTextMark *mark: {start, end})
        if (mark)
            p_gtk_text_buffer_delete_mark(buffer, mark);
}

// At the end of the tab, in grey, with the line break before it; not logged
static void showEditNotice(PurpleConversation *conv, const std::string &html, const char *name = EDIT_NOTICE)
{
    GtkIMHtml     *imhtml = getIMHtml(conv);
    if (!imhtml)
        return;
    removeEditNotice(conv, name);
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextIter    where;
    p_gtk_text_buffer_get_end_iter(buffer, &where);
    gint        offset = p_gtk_text_iter_get_offset(&where);
    std::string line   = (offset ? "<br>" : "") + std::string("<font color=\"#777777\">") + html + "</font>";
    p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GtkIMHtmlOptions(0), &where);
    // Set once it is in, as the start's right gravity would have it go past the line
    p_gtk_text_buffer_get_iter_at_offset(buffer, &where, offset);
    p_gtk_text_buffer_create_mark(buffer, (name + std::string("-start")).c_str(), &where, FALSE);
    p_gtk_text_buffer_get_end_iter(buffer, &where);
    p_gtk_text_buffer_create_mark(buffer, (name + std::string("-end")).c_str(), &where, TRUE);
}

void showEditOutcome(PurpleConversation *conv, const std::string &html)
{
    showEditNotice(conv, html, EDIT_OUTCOME);
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
                    const std::string &notice, std::function<void()> cancelled)
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
    removeEditNotice(conv, EDIT_OUTCOME);
    showEditNotice(conv, notice);

    // Markup already, but for its line breaks
    std::string html;
    for (char c: text)
        if (c == '\n')
            html += "<br>";
        else
            html += c;
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
    removeEditNotice(conv, EDIT_NOTICE);
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
    insertAtMark(imhtml, endMark, (formatEditedText(imhtml, *edited) + edited->editNote).c_str());
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
    for (MarkKind kind: {MarkKind::Start, MarkKind::Body, MarkKind::End, MarkKind::Reactions}) {
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
        for (MarkKind kind: {MarkKind::Start, MarkKind::Body, MarkKind::End}) {
            GtkTextMark *mark = p_gtk_text_buffer_get_mark(buffer, getMarkName(oldId, kind).c_str());
            if (mark) {
                GtkTextIter where;
                p_gtk_text_buffer_get_iter_at_mark(buffer, &where, mark);
                setMark(buffer, getMarkName(newId, kind), where);
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

static void itemActivated(GtkWidget *item, gpointer data)
{
    (*static_cast<std::function<void()> *>(data))();
}

static void deleteAction(gpointer data, GClosure *closure)
{
    delete static_cast<std::function<void()> *>(data);
}

// In a grid of the given number of columns, if any, where an item with a submenu takes a row
static GtkWidget *makeMenu(const std::vector<MenuItem> &items, unsigned columns);

static void addMenuItems(GtkWidget *menu, const std::vector<MenuItem> &items, unsigned columns)
{
    unsigned column = 0, row = 0;
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
}

static GtkWidget *makeMenu(const std::vector<MenuItem> &items, unsigned columns)
{
    GtkWidget *menu = p_gtk_menu_new();
    addMenuItems(menu, items, columns);
    return menu;
}

struct OpenMenu {
    GtkWidget *menu        = nullptr; // none once GTK destroys it
    GtkWidget *separator   = nullptr;
    GtkWidget *placeholder = nullptr;
    gulong     destroyHandler = 0;
    ~OpenMenu()
    {
        if (menu)
            p_g_signal_handler_disconnect(menu, destroyHandler);
    }
};

static MessagePopupHandler g_messagePopupHandler = nullptr;
static guint               g_popupSignal = 0;
static gulong              g_popupHook   = 0;

void setMessagePopupHandler(MessagePopupHandler handler)
{
    g_messagePopupHandler = handler;
}

static void openMenuDestroyed(GtkWidget *, gpointer data)
{
    std::shared_ptr<OpenMenu> menu = static_cast<std::weak_ptr<OpenMenu> *>(data)->lock();
    if (menu)
        menu->menu = nullptr;
}

static void deleteOpenMenuRef(gpointer data, GClosure *)
{
    delete static_cast<std::weak_ptr<OpenMenu> *>(data);
}

void fillOpenMenu(const OpenMenuRef &menu, const std::vector<MenuItem> &items)
{
    if (!menu || !menu->menu || !menu->placeholder)
        return;
    p_gtk_widget_destroy(menu->placeholder);
    menu->placeholder = nullptr;
    if (items.empty())
        p_gtk_widget_destroy(menu->separator);
    else {
        addMenuItems(menu->menu, items, 0);
        p_gtk_widget_show_all(menu->menu);
    }
    menu->separator = nullptr;
}

// The message whose text, header or reactions are there, if any: the nearest start of a message
// before it, if that message has not ended before it
static MessageId findMessageAt(GtkTextBuffer *buffer, const GtkTextIter &where)
{
    GtkTextIter iter = where;
    do {
        MessageId messageId;
        GSList   *marks = p_gtk_text_iter_get_marks(&iter);
        for (GSList *item = marks; item && !messageId.valid(); item = item->next) {
            const char *name = p_gtk_text_mark_get_name(static_cast<GtkTextMark *>(item->data));
            if (name && g_str_has_prefix(name, START_MARK_PREFIX))
                messageId = MessageId::fromString(name + strlen(START_MARK_PREFIX));
        }
        g_slist_free(marks);
        if (!messageId.valid())
            continue;
        for (MarkKind kind: {MarkKind::Reactions, MarkKind::End}) {
            GtkTextMark *end = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, kind).c_str());
            GtkTextIter  endIter;
            if (end) {
                p_gtk_text_buffer_get_iter_at_mark(buffer, &endIter, end);
                return (p_gtk_text_iter_compare(&where, &endIter) < 0) ? messageId : MessageId();
            }
        }
        return MessageId();
    } while (p_gtk_text_iter_backward_char(&iter));
    return MessageId();
}

static PurpleConversation *findTabConversation(gpointer imhtml)
{
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv = static_cast<PurpleConversation *>(item->data);
        if (!strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)), config::pluginId) &&
            (getIMHtml(conv) == imhtml))
            return conv;
    }
    return nullptr;
}

// GTK builds the menu of a tab's text anew each time, before emitting "populate-popup" for others to
// add to it. The message is the one under the pointer, also when the menu is opened from the
// keyboard; none if the pointer is elsewhere.
static gboolean populatingPopup(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkWidget          *view = (paramCount > 1) ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    GtkWidget          *menu = view ? static_cast<GtkWidget *>(p_g_value_get_object(&params[1])) : nullptr;
    PurpleConversation *conv = menu ? findTabConversation(view) : nullptr;
    if (!conv || !g_messagePopupHandler)
        return TRUE;
    gint x, y;
    p_gtk_widget_get_pointer(view, &x, &y);
    if ((x < 0) || (y < 0) || (x >= view->allocation.width) || (y >= view->allocation.height))
        return TRUE;
    GtkTextView *textView = reinterpret_cast<GtkTextView *>(view);
    GtkTextIter  where;
    p_gtk_text_view_window_to_buffer_coords(textView, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
    p_gtk_text_view_get_iter_at_location(textView, &where, x, y);
    MessageId messageId = findMessageAt(getTextBuffer(conv), where);
    if (!messageId.valid())
        return TRUE;

    OpenMenuRef openMenu = std::make_shared<OpenMenu>();
    openMenu->menu        = menu;
    openMenu->separator   = p_gtk_separator_menu_item_new();
    openMenu->placeholder = p_gtk_menu_item_new_with_label("\xe2\x80\xa6");
    p_gtk_widget_set_sensitive(openMenu->placeholder, FALSE);
    for (GtkWidget *item: {openMenu->separator, openMenu->placeholder}) {
        p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu), item);
        p_gtk_widget_show(item);
    }
    openMenu->destroyHandler = p_g_signal_connect_data(menu, "destroy", G_CALLBACK(openMenuDestroyed),
                                                       new std::weak_ptr<OpenMenu>(openMenu), deleteOpenMenuRef,
                                                       GConnectFlags(0));
    purple_debug_misc(config::pluginId, "Menu opened on message %" G_GINT64_FORMAT "\n", messageId.value());
    if (!g_messagePopupHandler(conv, messageId, openMenu))
        fillOpenMenu(openMenu, {});
    return TRUE;
}

static void connectPopup()
{
    if (!LOOK_UP(gtk_imhtml_get_type) || !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook) || !LOOK_UP(g_value_get_object) ||
        !LOOK_UP(gtk_widget_get_pointer) || !LOOK_UP(gtk_text_view_window_to_buffer_coords) ||
        !LOOK_UP(gtk_text_view_get_iter_at_location) || !LOOK_UP(gtk_text_iter_get_marks) ||
        !LOOK_UP(gtk_text_mark_get_name) || !LOOK_UP(gtk_separator_menu_item_new) || !LOOK_UP(gtk_widget_show))
    {
        purple_debug_warning(config::pluginId, "Cannot add to the menu of a tab's text: some of GTK is not found\n");
        return;
    }
    g_popupSignal = p_g_signal_lookup("populate-popup", p_gtk_imhtml_get_type());
    if (g_popupSignal)
        g_popupHook = p_g_signal_add_emission_hook(g_popupSignal, 0, populatingPopup, nullptr, nullptr);
}

static void disconnectPopup()
{
    if (g_popupHook)
        p_g_signal_remove_emission_hook(g_popupSignal, g_popupHook);
    g_popupHook = 0;
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

MessageMarkScope::MessageMarkScope(MessageId messageId, const char *editNote, bool anew, bool markChanges)
{
}

MessageMarkScope::~MessageMarkScope()
{
}

void showMessageReactions(PurpleConversation *conv, MessageId messageId, const std::string &html)
{
}

bool beginEntryEdit(PurpleConversation *conv, const EntryEdit &edit, const std::string &text,
                    const std::string &notice, std::function<void()> cancelled)
{
    return false;
}

bool endEntryEdit(PurpleConversation *conv, EntryEdit *edit)
{
    return false;
}

void showEditOutcome(PurpleConversation *conv, const std::string &html)
{
}

bool isEntryEditing(PurpleConversation *conv, MessageId messageId)
{
    return false;
}

bool canRedrawConversation(PurpleConversation *conv)
{
    return false;
}

void appendTabLine(PurpleConversation *conv, const std::string &html)
{
}

void startProgress(PurpleConversation *conv, const std::string &html)
{
}

void stopProgress(PurpleConversation *conv)
{
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

void renameMessageMarks(PurpleAccount *account, MessageId oldId, MessageId newId)
{
}

bool scrollToMessage(PidginIMHtml *imhtml, MessageId messageId)
{
    return false;
}

void setMessagePopupHandler(MessagePopupHandler handler)
{
}

void fillOpenMenu(const OpenMenuRef &menu, const std::vector<MenuItem> &items)
{
}

#endif
