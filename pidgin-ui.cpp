#include "pidgin-ui.h"
#include "thumbnail.h"
#include <gio/gio.h>
#include "client-utils.h"
#include "config.h"
#include "translate.h"
#include <algorithm>
#include <set>
#include <string>

#ifndef NoPidgin
#include <gtkconv.h>
// Its header, gtkblist.h, is no C++, naming a parameter "new"
extern "C" void *pidgin_blist_get_handle(void);
#include <gdk/gdkkeysyms.h>

// Pidgin's and GTK's functions, looked up by name: their types are those the headers declare
#define FRONT_END_FUNCTION(name) static decltype(&name) p_##name
#define LOOK_UP(name) (p_##name = reinterpret_cast<decltype(&name)>(findFrontEndSymbol(#name)))

FRONT_END_FUNCTION(pidgin_conversations_get_handle);
FRONT_END_FUNCTION(pidgin_conversations_get_conv_ui_ops);
FRONT_END_FUNCTION(pidgin_blist_get_handle);
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
// For /paste of files, which then sends a picture only
FRONT_END_FUNCTION(gtk_clipboard_wait_for_uris);
FRONT_END_FUNCTION(g_signal_handler_find);
FRONT_END_FUNCTION(g_signal_handler_block);
FRONT_END_FUNCTION(g_signal_handler_unblock);
FRONT_END_FUNCTION(g_signal_stop_emission_by_name);
FRONT_END_FUNCTION(gtk_text_buffer_insert_at_cursor);
// For the menu of a placeholder in the input field
FRONT_END_FUNCTION(gtk_text_buffer_get_slice);
FRONT_END_FUNCTION(gtk_text_buffer_insert);
// For the thumbnails before placeholders
FRONT_END_FUNCTION(gtk_text_buffer_create_child_anchor);
FRONT_END_FUNCTION(gtk_text_view_add_child_at_anchor);
FRONT_END_FUNCTION(gtk_text_iter_get_child_anchor);
FRONT_END_FUNCTION(gtk_image_new_from_icon_name);
FRONT_END_FUNCTION(gtk_image_set_from_pixbuf);
FRONT_END_FUNCTION(gdk_pixbuf_new_from_file_at_scale);
FRONT_END_FUNCTION(g_object_ref);
FRONT_END_FUNCTION(gtk_text_buffer_place_cursor);
FRONT_END_FUNCTION(gtk_text_buffer_delete);
FRONT_END_FUNCTION(gtk_widget_create_pango_layout);
FRONT_END_FUNCTION(pango_layout_get_pixel_size);
FRONT_END_FUNCTION(gtk_widget_get_default_style);
FRONT_END_FUNCTION(gdk_pango_context_get);
FRONT_END_FUNCTION(pango_context_get_metrics);
FRONT_END_FUNCTION(pango_font_metrics_get_ascent);
FRONT_END_FUNCTION(pango_font_metrics_get_descent);
FRONT_END_FUNCTION(pango_font_metrics_unref);
FRONT_END_FUNCTION(gtk_text_buffer_get_iter_at_child_anchor);
FRONT_END_FUNCTION(gtk_text_child_anchor_get_deleted);
FRONT_END_FUNCTION(gtk_text_child_anchor_get_widgets);
FRONT_END_FUNCTION(gtk_text_view_get_buffer);
FRONT_END_FUNCTION(gtk_widget_get_parent);
FRONT_END_FUNCTION(gtk_event_box_new);
FRONT_END_FUNCTION(gtk_event_box_set_visible_window);
FRONT_END_FUNCTION(gtk_container_add);
FRONT_END_FUNCTION(gtk_menu_popup);
FRONT_END_FUNCTION(g_object_set_data);
// For drops
FRONT_END_FUNCTION(g_value_get_uint);
FRONT_END_FUNCTION(gdk_atom_intern);
FRONT_END_FUNCTION(gtk_drag_finish);
FRONT_END_FUNCTION(gtk_drag_dest_find_target);
FRONT_END_FUNCTION(gdk_drag_status);
FRONT_END_FUNCTION(gtk_drag_get_data);
FRONT_END_FUNCTION(gdk_pixbuf_loader_new);
FRONT_END_FUNCTION(gdk_pixbuf_loader_write);
FRONT_END_FUNCTION(gdk_pixbuf_loader_close);
FRONT_END_FUNCTION(gdk_pixbuf_loader_get_pixbuf);
FRONT_END_FUNCTION(gdk_pixbuf_save_to_buffer);
FRONT_END_FUNCTION(g_object_unref);
FRONT_END_FUNCTION(gtk_text_buffer_get_insert);
FRONT_END_FUNCTION(g_object_get_data);
FRONT_END_FUNCTION(g_object_set_data_full);
FRONT_END_FUNCTION(gtk_widget_get_pointer);
FRONT_END_FUNCTION(gtk_adjustment_get_type);
FRONT_END_FUNCTION(gtk_widget_get_type);
FRONT_END_FUNCTION(g_value_get_boxed);
FRONT_END_FUNCTION(gtk_text_buffer_get_iter_at_offset);
FRONT_END_FUNCTION(gtk_text_view_window_to_buffer_coords);
FRONT_END_FUNCTION(gtk_text_view_get_iter_at_location);
FRONT_END_FUNCTION(gtk_text_iter_get_marks);
FRONT_END_FUNCTION(gtk_text_mark_get_name);
FRONT_END_FUNCTION(gtk_text_buffer_get_line_count);
FRONT_END_FUNCTION(gtk_text_buffer_get_iter_at_line);
FRONT_END_FUNCTION(gtk_text_mark_get_deleted);
FRONT_END_FUNCTION(gtk_text_iter_get_line);
FRONT_END_FUNCTION(gtk_text_iter_is_end);
FRONT_END_FUNCTION(gtk_widget_hide);
FRONT_END_FUNCTION(pidgin_conv_windows_get_list);
FRONT_END_FUNCTION(pidgin_conv_get_tab_at_xy);
FRONT_END_FUNCTION(pidgin_conv_window_get_gtkconv_at_index);
FRONT_END_FUNCTION(gtk_separator_menu_item_new);
FRONT_END_FUNCTION(gtk_menu_get_type);
FRONT_END_FUNCTION(g_type_check_instance_is_a);
FRONT_END_FUNCTION(gtk_widget_show);
// For the dialog choosing out of a list, searched by what is typed
FRONT_END_FUNCTION(gtk_dialog_new_with_buttons);
FRONT_END_FUNCTION(gtk_dialog_get_content_area);
FRONT_END_FUNCTION(gtk_dialog_response);
FRONT_END_FUNCTION(gtk_dialog_set_default_response);
FRONT_END_FUNCTION(gtk_window_set_default_size);
FRONT_END_FUNCTION(gtk_label_new);
FRONT_END_FUNCTION(gtk_entry_new);
FRONT_END_FUNCTION(gtk_entry_get_text);
FRONT_END_FUNCTION(gtk_entry_set_activates_default);
FRONT_END_FUNCTION(gtk_list_store_new);
FRONT_END_FUNCTION(gtk_list_store_append);
FRONT_END_FUNCTION(gtk_list_store_set);
FRONT_END_FUNCTION(gtk_tree_model_filter_new);
FRONT_END_FUNCTION(gtk_tree_model_filter_set_visible_func);
FRONT_END_FUNCTION(gtk_tree_model_filter_refilter);
FRONT_END_FUNCTION(gtk_tree_model_get);
FRONT_END_FUNCTION(gtk_tree_model_get_iter_first);
FRONT_END_FUNCTION(gtk_tree_view_new_with_model);
FRONT_END_FUNCTION(gtk_tree_view_set_headers_visible);
FRONT_END_FUNCTION(gtk_tree_view_append_column);
FRONT_END_FUNCTION(gtk_tree_view_get_selection);
FRONT_END_FUNCTION(gtk_tree_view_column_new_with_attributes);
FRONT_END_FUNCTION(gtk_cell_renderer_text_new);
FRONT_END_FUNCTION(gtk_cell_renderer_toggle_new);
FRONT_END_FUNCTION(gtk_tree_model_get_iter_from_string);
FRONT_END_FUNCTION(gtk_tree_model_filter_convert_iter_to_child_iter);
FRONT_END_FUNCTION(gtk_tree_model_iter_next);
FRONT_END_FUNCTION(gtk_tree_selection_get_selected);
FRONT_END_FUNCTION(gtk_tree_selection_select_iter);
FRONT_END_FUNCTION(gtk_scrolled_window_new);
FRONT_END_FUNCTION(gtk_scrolled_window_set_policy);
FRONT_END_FUNCTION(gtk_box_pack_start);
static bool g_chooser = false;
FRONT_END_FUNCTION(gtk_text_buffer_get_tag_table);
FRONT_END_FUNCTION(gtk_text_tag_table_lookup);
FRONT_END_FUNCTION(gtk_text_buffer_create_tag);
FRONT_END_FUNCTION(gtk_text_buffer_apply_tag);
FRONT_END_FUNCTION(gtk_text_iter_get_tags);
FRONT_END_FUNCTION(gtk_text_view_get_iter_location);
FRONT_END_FUNCTION(gtk_widget_set_has_tooltip);
FRONT_END_FUNCTION(gtk_widget_trigger_tooltip_query);
FRONT_END_FUNCTION(gtk_tooltip_set_text);
FRONT_END_FUNCTION(gtk_clipboard_get);
FRONT_END_FUNCTION(gtk_clipboard_set_with_data);
FRONT_END_FUNCTION(gtk_clipboard_set_can_store);
FRONT_END_FUNCTION(gtk_clipboard_set_text);
FRONT_END_FUNCTION(gtk_target_list_new);
FRONT_END_FUNCTION(gtk_target_list_add);
FRONT_END_FUNCTION(gtk_target_list_add_uri_targets);
FRONT_END_FUNCTION(gtk_target_list_add_image_targets);
FRONT_END_FUNCTION(gtk_target_list_add_text_targets);
FRONT_END_FUNCTION(gtk_target_list_unref);
FRONT_END_FUNCTION(gtk_target_table_new_from_list);
FRONT_END_FUNCTION(gtk_target_table_free);
FRONT_END_FUNCTION(gtk_selection_data_set);
FRONT_END_FUNCTION(gtk_selection_data_set_uris);
FRONT_END_FUNCTION(gtk_selection_data_set_text);
FRONT_END_FUNCTION(gtk_selection_data_set_pixbuf);
FRONT_END_FUNCTION(gdk_pixbuf_new_from_file);
FRONT_END_FUNCTION(gtk_text_view_get_iter_at_position);
FRONT_END_FUNCTION(gtk_drag_begin);
FRONT_END_FUNCTION(gtk_drag_check_threshold);
FRONT_END_FUNCTION(gtk_drag_set_icon_pixbuf);
FRONT_END_FUNCTION(gdk_window_get_user_data);
FRONT_END_FUNCTION(gtk_image_get_storage_type);
FRONT_END_FUNCTION(gtk_image_get_pixbuf);
FRONT_END_FUNCTION(gdk_pixbuf_scale_simple);
FRONT_END_FUNCTION(gdk_pixbuf_get_width);
FRONT_END_FUNCTION(gdk_pixbuf_get_height);
FRONT_END_FUNCTION(gdk_pixbuf_new);
FRONT_END_FUNCTION(gdk_pixbuf_fill);
FRONT_END_FUNCTION(pidgin_conv_window_get_active_conversation);
FRONT_END_FUNCTION(pidgin_conv_window_switch_gtkconv);
FRONT_END_FUNCTION(gdk_window_get_state);
FRONT_END_FUNCTION(gtk_text_view_get_visible_rect);
FRONT_END_FUNCTION(gtk_window_is_active);

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
    bool                hidden     = false; // past the end of what the tab shows, taken out once written
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
    Buttons,  // the end of the lines of its buttons after the message, if it has them
    Reactions // the end of the line of reactions after them, if it has one
};

static const char START_MARK_PREFIX[]   = "tdlib-message-start-";

static std::string getMarkName(MessageId messageId, MarkKind kind)
{
    const char *prefix = (kind == MarkKind::Start) ? START_MARK_PREFIX :
                         (kind == MarkKind::Body)  ? "tdlib-message-body-" :
                         (kind == MarkKind::End)   ? "tdlib-message-end-" :
                         (kind == MarkKind::Buttons) ? "tdlib-message-buttons-" : "tdlib-message-reactions-";
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
    GtkTextMark *after = p_gtk_text_buffer_create_mark(buffer, nullptr, &where, FALSE);
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
static gboolean displayingMessage(PurpleAccount *account, const char *, char **message,
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
    // Pidgin's "Last Said" finds a chat member's latest message by a mark named after the sender
    // written, which for our own is not the account's name the member list has us by: that mark is
    // set too, where Pidgin sets its own, unless an earlier message is being written over
    bool           ownInChat  = (flags & PURPLE_MESSAGE_SEND) &&
                                (purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT) &&
                                !strcmp(purple_account_get_protocol_id(account), config::pluginId);
    GtkTextBuffer *chatBuffer = ownInChat ? getTextBuffer(conv) : nullptr;
    if (chatBuffer && !(g_messageBeingWritten && g_messageBeingWritten->replacing)) {
        std::string name = std::string("user:") + purple_account_get_username(account);
        GtkTextIter end;
        p_gtk_text_buffer_get_end_iter(chatBuffer, &end);
        GtkTextMark *mark = p_gtk_text_buffer_get_mark(chatBuffer, name.c_str());
        if (mark)
            p_gtk_text_buffer_move_mark(chatBuffer, mark, &end);
        else
            p_gtk_text_buffer_create_mark(chatBuffer, name.c_str(), &end, TRUE);
    }
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
            for (MarkKind kind: {MarkKind::Body, MarkKind::Buttons, MarkKind::Reactions}) {
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
        gchar *marked = g_strconcat(BODY_MARKER, *message, nullptr);
        g_free(*message);
        *message = marked;
    }
    return FALSE;
}

static void scheduleViewCheck(PurpleConversation *conv);
static bool isMessageHidden(PurpleConversation *conv, MessageId messageId);

// Emitted by Pidgin once it has written into a tab; not if another plugin stopped the writing.
// The marks are set (again) after each part of the message, while the conversation surely exists.
static void displayedMessage(PurpleAccount *, const char *, char *,
                             PurpleConversation *conv, PurpleMessageFlags)
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
        // Written for the log, and for Pidgin to tell of it, but past the end of what the tab shows:
        // taken out once all its parts are, which go by its marks
        if (isMessageHidden(conv, g_messageBeingWritten->messageId))
            g_messageBeingWritten->hidden = true;
    }
    if (conv)
        scheduleViewCheck(conv);
}

static BuddyMarkHandler g_buddyMarkHandler = nullptr;
static std::string      g_buddyMark;

void setBuddyMarkHandler(BuddyMarkHandler handler, const char *mark)
{
    g_buddyMarkHandler = handler;
    g_buddyMark        = mark ? mark : "";
}

// The name as the buddy list shows it, escaped, Pidgin's own unless it is marked
static char *drawingBuddy(PurpleBuddy *buddy)
{
    if (!g_buddyMarkHandler || !g_buddyMarkHandler(buddy))
        return nullptr;
    const char *name    = purple_buddy_get_contact_alias(buddy);
    gchar      *escaped = g_markup_escape_text(name ? name : purple_buddy_get_name(buddy), -1);
    gchar      *marked  = g_strconcat(g_buddyMark.c_str(), escaped, nullptr);
    g_free(escaped);
    return marked;
}

static guint g_connectSource = 0;

static const char MESSAGE_MARK_PREFIX[] = "tdlib-message-";

// Emptied, as by /clear or to be filled anew: what comes is shown again. The marks of the messages
// that were in it go too, which GTK would keep, all at its start, for reaction lines to be written
// at and messages to be taken to be there still.
static void historyCleared(PurpleConversation *conv)
{
    setTabEnd(conv, MessageId());
    GtkTextBuffer *buffer = getTextBuffer(conv);
    if (!buffer || !p_gtk_text_iter_get_marks || !p_gtk_text_mark_get_name)
        return;
    std::vector<GtkTextMark *> gone;
    GtkTextIter                iter;
    p_gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
    do {
        GSList *marks = p_gtk_text_iter_get_marks(&iter);
        for (GSList *item = marks; item; item = item->next) {
            const char *name = p_gtk_text_mark_get_name(static_cast<GtkTextMark *>(item->data));
            if (name && g_str_has_prefix(name, MESSAGE_MARK_PREFIX))
                gone.push_back(static_cast<GtkTextMark *>(item->data));
        }
        g_slist_free(marks);
    } while (p_gtk_text_iter_forward_char(&iter));
    for (GtkTextMark *mark: gone)
        p_gtk_text_buffer_delete_mark(buffer, mark);
}

// Disconnected by libpurple when the plugin is unloaded
static void connectPaste(gpointer plugin);
static void connectPopup();
static void connectScroll();
static void connectShown(gpointer plugin);
static void findTabWindowFunctions();
static void connectTabMenu();
static void disconnectTabMenu();

static gboolean connectSignals(gpointer plugin)
{
    g_connectSource = 0;
    void *handle    = p_pidgin_conversations_get_handle();
    bool  connected = true;
    for (const char *signal: {"displaying-im-msg", "displaying-chat-msg"})
        connected = purple_signal_connect(handle, signal, plugin, PURPLE_CALLBACK(displayingMessage), nullptr) &&
                    connected;
    for (const char *signal: {"displayed-im-msg", "displayed-chat-msg"})
        connected = purple_signal_connect(handle, signal, plugin, PURPLE_CALLBACK(displayedMessage), nullptr) &&
                    connected;
    connected = purple_signal_connect(purple_conversations_get_handle(), "cleared-message-history", plugin,
                                      PURPLE_CALLBACK(historyCleared), nullptr) && connected;
    if (!connected)
        purple_debug_warning(config::pluginId, "Cannot follow Pidgin's conversation window\n");
    if (!LOOK_UP(pidgin_blist_get_handle) ||
        !purple_signal_connect(p_pidgin_blist_get_handle(), "drawing-buddy", plugin, PURPLE_CALLBACK(drawingBuddy), nullptr))
        purple_debug_warning(config::pluginId, "Cannot mark contacts in Pidgin's buddy list\n");
    connectPaste(plugin);
    connectPopup();
    connectScroll();
    connectTabMenu();
    connectShown(plugin);
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
                 LOOK_UP(gtk_text_buffer_get_iter_at_offset) && LOOK_UP(gtk_text_iter_is_end);
    // Reactions are listed in their line's tooltip only with these
    if (g_pidginUi && (!LOOK_UP(gtk_text_buffer_get_tag_table) || !LOOK_UP(gtk_text_tag_table_lookup) ||
                       !LOOK_UP(gtk_text_buffer_create_tag) || !LOOK_UP(gtk_text_buffer_apply_tag)))
        p_gtk_text_buffer_apply_tag = nullptr;
    purple_debug_info(config::pluginId, "Pidgin's conversation window: %s\n",
                      g_pidginUi ? "found" : "not found");
    if (!g_pidginUi)
        return;
    // Pictures sized by the font if they can be
    if (!LOOK_UP(pango_layout_get_pixel_size) || !LOOK_UP(gtk_widget_create_pango_layout))
        p_gtk_widget_create_pango_layout = nullptr;
    if (!LOOK_UP(gtk_widget_get_default_style) || !LOOK_UP(gdk_pango_context_get) ||
        !LOOK_UP(pango_context_get_metrics) || !LOOK_UP(pango_font_metrics_get_ascent) ||
        !LOOK_UP(pango_font_metrics_get_descent) || !LOOK_UP(pango_font_metrics_unref))
        p_gtk_widget_get_default_style = nullptr;

    g_chooser = LOOK_UP(gtk_dialog_new_with_buttons) && LOOK_UP(gtk_dialog_get_content_area) && LOOK_UP(gtk_dialog_response) &&
                LOOK_UP(gtk_dialog_set_default_response) && LOOK_UP(gtk_window_set_default_size) &&
                LOOK_UP(gtk_label_new) && LOOK_UP(gtk_entry_new) && LOOK_UP(gtk_entry_get_text) &&
                LOOK_UP(gtk_entry_set_activates_default) && LOOK_UP(gtk_list_store_new) &&
                LOOK_UP(gtk_list_store_append) && LOOK_UP(gtk_list_store_set) &&
                LOOK_UP(gtk_tree_model_filter_new) && LOOK_UP(gtk_tree_model_filter_set_visible_func) &&
                LOOK_UP(gtk_tree_model_filter_refilter) && LOOK_UP(gtk_tree_model_get) &&
                LOOK_UP(gtk_tree_model_get_iter_first) && LOOK_UP(gtk_tree_view_new_with_model) &&
                LOOK_UP(gtk_tree_view_set_headers_visible) && LOOK_UP(gtk_tree_view_append_column) &&
                LOOK_UP(gtk_tree_view_get_selection) && LOOK_UP(gtk_tree_view_column_new_with_attributes) &&
                LOOK_UP(gtk_cell_renderer_text_new) && LOOK_UP(gtk_cell_renderer_toggle_new) &&
                LOOK_UP(gtk_tree_model_get_iter_from_string) &&
                LOOK_UP(gtk_tree_model_filter_convert_iter_to_child_iter) && LOOK_UP(gtk_tree_model_iter_next) &&
                LOOK_UP(gtk_tree_selection_get_selected) &&
                LOOK_UP(gtk_tree_selection_select_iter) && LOOK_UP(gtk_scrolled_window_new) &&
                LOOK_UP(gtk_scrolled_window_set_policy) && LOOK_UP(gtk_box_pack_start) &&
                LOOK_UP(gtk_widget_show_all) && LOOK_UP(gtk_widget_destroy) &&
                LOOK_UP(g_signal_connect_data) && LOOK_UP(gtk_container_add);

    findTabWindowFunctions();

    // A protocol plugin is loaded before Pidgin's window registers its signals: they are
    // connected to once the main loop is idle
    g_connectSource = g_idle_add(connectSignals, plugin);
}

static void disconnectPaste();
static void disconnectPopup();
static void disconnectScroll();
static void disconnectReactionTips();
static void disconnectShown();

void pidginUiUnload()
{
    disconnectShown();
    disconnectReactionTips();
    disconnectPaste();
    disconnectPopup();
    disconnectScroll();
    disconnectTabMenu();
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
        PurpleConversation *conv = g_messageBeingWritten->conv;
        if (g_messageBeingWritten->hidden && g_list_find(purple_get_conversations(), conv))
            removeMessage(conv, g_messageBeingWritten->messageId);
        delete g_messageBeingWritten;
        g_messageBeingWritten = nullptr;
    }
}

// Each reaction on a message's line is tagged as that reaction, for its tooltip
static const char REACTION_TAG_PREFIX[] = "tdlib-reaction ";
static void connectReactionTips(GtkIMHtml *imhtml);

static GtkTextTag *getReactionTag(GtkTextBuffer *buffer, const std::string &key)
{
    std::string name = REACTION_TAG_PREFIX + key;
    GtkTextTag *tag  = p_gtk_text_tag_table_lookup(p_gtk_text_buffer_get_tag_table(buffer), name.c_str());
    return tag ? tag : p_gtk_text_buffer_create_tag(buffer, name.c_str(), nullptr);
}

// The buttons go right after the message, the reactions after them; each from the end of what is
// before it, to its own mark
static void showMessageArea(PurpleConversation *conv, MessageId messageId, MarkKind kind,
                            const std::vector<ShownReaction> &reactions)
{
    GtkIMHtml     *imhtml  = getIMHtml(conv);
    GtkTextBuffer *buffer  = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *endMark = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::End).c_str()) : nullptr;
    if (!endMark)
        return;
    GtkTextMark *buttonsMark = (kind == MarkKind::Reactions)
                               ? p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Buttons).c_str())
                               : nullptr;
    if (buttonsMark) {
        GtkTextIter textEnd, buttonsEnd;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &textEnd, endMark);
        p_gtk_text_buffer_get_iter_at_mark(buffer, &buttonsEnd, buttonsMark);
        if (p_gtk_text_iter_compare(&textEnd, &buttonsEnd) < 0)
            endMark = buttonsMark;
    }
    std::string  name = getMarkName(messageId, kind);
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
    GtkTextMark *after = p_gtk_text_buffer_create_mark(buffer, nullptr, &end, FALSE);
    // Into the tab only, not the log; each reaction in turn, to be tagged as such
    bool tagged = p_gtk_text_buffer_get_tag_table && p_gtk_text_tag_table_lookup && p_gtk_text_buffer_create_tag &&
                  p_gtk_text_buffer_apply_tag;
    for (size_t i = 0; i < reactions.size(); i++) {
        std::string html = (i && !reactions[i].newLine) ? "&nbsp;&nbsp;&nbsp;" : "<br>";
        p_gtk_imhtml_insert_html_at_iter(imhtml, ("<font size=\"2\">" + html + "</font>").c_str(),
                                         GTK_IMHTML_NO_SCROLL, &end);
        p_gtk_text_buffer_get_iter_at_mark(buffer, &end, after);
        gint start = p_gtk_text_iter_get_offset(&end);
        p_gtk_imhtml_insert_html_at_iter(imhtml, ("<font size=\"2\">" + reactions[i].html + "</font>").c_str(),
                                         GTK_IMHTML_NO_SCROLL, &end);
        p_gtk_text_buffer_get_iter_at_mark(buffer, &end, after);
        if (tagged && !reactions[i].key.empty()) {
            GtkTextIter from;
            p_gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
            p_gtk_text_buffer_apply_tag(buffer, getReactionTag(buffer, reactions[i].key), &from, &end);
        }
    }
    if (tagged && !reactions.empty())
        connectReactionTips(imhtml);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &areaEnd, after);
    p_gtk_text_buffer_create_mark(buffer, name.c_str(), &areaEnd, TRUE);
    p_gtk_text_buffer_delete_mark(buffer, after);
}

void showMessageReactions(PurpleConversation *conv, MessageId messageId, const std::vector<ShownReaction> &reactions)
{
    showMessageArea(conv, messageId, MarkKind::Reactions, reactions);
}

void showMessageButtons(PurpleConversation *conv, MessageId messageId, const std::vector<ShownReaction> &buttons)
{
    showMessageArea(conv, messageId, MarkKind::Buttons, buttons);
}

bool canRedrawConversation(PurpleConversation *conv)
{
    return getIMHtml(conv) != nullptr;
}

// Pidgin switches to the tab only if a key or a button caused it, which a link handed over D-Bus
// did not: switched to here, then
void presentConversation(PurpleConversation *conv)
{
    purple_conversation_present(conv);
    PidginConversation *gtkconv = getIMHtml(conv) ? PIDGIN_CONVERSATION(conv) : nullptr;
    if (gtkconv && gtkconv->win && (p_pidgin_conv_window_switch_gtkconv ||
                                    LOOK_UP(pidgin_conv_window_switch_gtkconv)))
        p_pidgin_conv_window_switch_gtkconv(gtkconv->win, gtkconv);
}

// A line at the end of the tab, or at its top, its last character turning, and the marks around it
// and that character
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

// Around a line of the log that is a message, such as one deleted since: not taken for one shown,
// with a menu and reactions, but found by a link to it
static const char LOGGED_MARK_PREFIX[] = "tdlib-message-logged-";

static std::string getLoggedMarkName(MessageId messageId, bool start)
{
    return LOGGED_MARK_PREFIX + std::string(start ? "start-" : "end-") + std::to_string(messageId.value());
}

void appendTabLine(PurpleConversation *conv, const std::string &html, MessageId messageId)
{
    GtkIMHtml *imhtml = getIMHtml(conv);
    if (!imhtml)
        return;
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextIter    where;
    p_gtk_text_buffer_get_end_iter(buffer, &where);
    gint offset = p_gtk_text_iter_get_offset(&where);
    std::string line = (offset ? "<br>" : "") + html;
    p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GtkIMHtmlOptions(0), &where);
    if (messageId.valid()) {
        GtkTextIter start;
        p_gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
        // Past the line break, at the line's own start
        if (offset)
            p_gtk_text_iter_forward_char(&start);
        p_gtk_text_buffer_get_end_iter(buffer, &where);
        setMark(buffer, getLoggedMarkName(messageId, true), start);
        setMark(buffer, getLoggedMarkName(messageId, false), where);
    }
}

static bool isAtBottom(GtkIMHtml *imhtml);

void startProgress(PurpleConversation *conv, const std::string &html, bool atTop)
{
    GtkIMHtml *imhtml = getIMHtml(conv);
    if (!imhtml || purple_conversation_get_data(conv, PROGRESS_KEY))
        return;
    GtkTextBuffer *buffer = imhtml->text_buffer;
    GtkTextIter    where;
    bool           atEnd  = !atTop && isAtBottom(imhtml);
    if (atTop)
        p_gtk_text_buffer_get_iter_at_offset(buffer, &where, 0);
    else
        p_gtk_text_buffer_get_end_iter(buffer, &where);
    gint        offset = p_gtk_text_iter_get_offset(&where);
    std::string line   = std::string((offset && !atTop) ? "<br>" : "") + "<font color=\"#777777\">" + html + " </font>";
    p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GTK_IMHTML_NO_SCROLL, &where);
    // What is written at either end meanwhile stays outside: the start, set once the line is in, as
    // its right gravity would have it go past; the end, with the spinner's frame left before it, and
    // at the top, the line break after it
    gint lineEnd = p_gtk_text_iter_get_offset(&where);
    p_gtk_text_buffer_get_iter_at_offset(buffer, &where, offset);
    p_gtk_text_buffer_create_mark(buffer, PROGRESS_START, &where, FALSE);
    p_gtk_text_buffer_get_iter_at_offset(buffer, &where, lineEnd);
    p_gtk_text_buffer_create_mark(buffer, PROGRESS_SPINNER, &where, TRUE);
    GtkTextMark *end = p_gtk_text_buffer_create_mark(buffer, PROGRESS_END, &where, TRUE);
    insertAtMark(imhtml, end, getSpinnerFrame(0));
    p_gtk_text_buffer_get_iter_at_mark(buffer, &where, end);
    if (atTop && !p_gtk_text_iter_is_end(&where))
        p_gtk_imhtml_insert_html_at_iter(imhtml, "<br>", GTK_IMHTML_NO_SCROLL, &where);
    // Where it is looked for: at the end, if the tab was scrolled there
    if (atEnd)
        p_gtk_text_view_scroll_to_mark(reinterpret_cast<GtkTextView *>(imhtml), end, 0.0, FALSE, 0.0, 0.0);

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
        // At the top, with the line break after it
        if (!p_gtk_text_iter_get_offset(&from) && (p_gtk_text_iter_get_char(&to) == '\n'))
            p_gtk_text_iter_forward_char(&to);
        p_gtk_imhtml_delete(imhtml, &from, &to);
    }
    for (const char *name: {PROGRESS_START, PROGRESS_SPINNER, PROGRESS_END})
        if (GtkTextMark *mark = getProgressMark(buffer, name))
            p_gtk_text_buffer_delete_mark(buffer, mark);
}

namespace {
struct AfterDrawing {
    std::function<void()> then;
};
}

static gboolean drawn(gpointer data)
{
    AfterDrawing *after = static_cast<AfterDrawing *>(data);
    after->then();
    delete after;
    return G_SOURCE_REMOVE;
}

void runAfterDrawing(std::function<void()> then)
{
    // Of lower priority than GTK's laying out and drawing of what changed
    if (g_pidginUi)
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, drawn, new AfterDrawing{std::move(then)}, nullptr);
    else
        then();
}

void updateProgress(PurpleConversation *conv, const std::string &html)
{
    GtkIMHtml     *imhtml  = purple_conversation_get_data(conv, PROGRESS_KEY) ? getIMHtml(conv) : nullptr;
    GtkTextBuffer *buffer  = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *start   = buffer ? getProgressMark(buffer, PROGRESS_START) : nullptr;
    GtkTextMark   *spinner = buffer ? getProgressMark(buffer, PROGRESS_SPINNER) : nullptr;
    if (!start || !spinner)
        return;
    // Its text only, between the line break before it, if any, and the spinner; the marks put
    // back around what takes its place, which their gravity would not keep them around
    GtkTextIter from, to;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &from, start);
    gint startOffset = p_gtk_text_iter_get_offset(&from);
    if (p_gtk_text_iter_get_char(&from) == '\n')
        p_gtk_text_iter_forward_char(&from);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &to, spinner);
    p_gtk_imhtml_delete(imhtml, &from, &to);
    std::string line = std::string("<font color=\"#777777\">") + html + " </font>";
    p_gtk_imhtml_insert_html_at_iter(imhtml, line.c_str(), GTK_IMHTML_NO_SCROLL, &from);
    p_gtk_text_buffer_move_mark(buffer, spinner, &from);
    p_gtk_text_buffer_get_iter_at_offset(buffer, &from, startOffset);
    p_gtk_text_buffer_move_mark(buffer, start, &from);
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
static guint  g_pasteSignal     = 0;
static gulong g_pasteHook       = 0;
static guint  g_backspaceSignal = 0;
static gulong g_backspaceHook   = 0;

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

// The picture on the widget's clipboard, as PNG, put into the image store; 0 if there is none
static int storeClipboardPicture(GtkWidget *widget)
{
    GtkClipboard *clipboard = p_gtk_widget_get_clipboard(widget, GDK_SELECTION_CLIPBOARD);
    if (!clipboard || !p_gtk_clipboard_wait_is_image_available(clipboard))
        return 0;
    GdkPixbuf *pixbuf = p_gtk_clipboard_wait_for_image(clipboard);
    gchar     *data   = nullptr;
    gsize      size   = 0;
    GError    *error  = nullptr;
    int        id     = 0;
    if (pixbuf && p_gdk_pixbuf_save_to_buffer(pixbuf, &data, &size, "png", &error, nullptr)) {
        // The store takes the data
        id = purple_imgstore_add_with_id(data, size, "clipboard.png");
        purple_debug_misc(config::pluginId, "Pasted a picture of %zu bytes\n", size_t(size));
    } else
        purple_debug_warning(config::pluginId, "Cannot paste the picture: %s\n",
                             error ? error->message : "none on the clipboard");
    if (error)
        g_error_free(error);
    if (pixbuf)
        p_g_object_unref(pixbuf);
    return id;
}

// What is pasted or dropped into a Telegram conversation is attached to the message being written,
// rather than taken as text, as Pidgin's handlers, or GtkTextView's, would take files' URIs. An
// emission hook cannot stop a signal, so the widget's handlers are blocked for the emission, and one
// of the plugin's, connected after them, unblocks them and stops the signal.
struct Takeover {
    GtkWidget          *widget;
    guint               signal;
    const char         *name;
    gulong              ownHandler = 0;
    std::vector<gulong> blocked;
};
static PastedHandler      g_pastedHandler      = nullptr;
static guint              g_dropSignal         = 0;
static gulong             g_dropHook           = 0;

void setPastedHandler(PastedHandler handler)
{
    g_pastedHandler = handler;
}

static std::string getTakeoverKey(const char *name)
{
    return std::string("tdlib-purple-takeover-") + name;
}

// Connected swapped, its other arguments, which differ between signals, ignored
static void takenOver(Takeover *takeover)
{
    if (takeover->blocked.empty())
        return;
    for (gulong handler: takeover->blocked)
        p_g_signal_handler_unblock(takeover->widget, handler);
    takeover->blocked.clear();
    p_g_signal_stop_emission_by_name(takeover->widget, takeover->name);
}

static void deleteTakeover(gpointer data)
{
    delete static_cast<Takeover *>(data);
}

static void watchSignal(GtkWidget *widget, guint signal, const char *name)
{
    std::string key = getTakeoverKey(name);
    if (p_g_object_get_data(reinterpret_cast<GObject *>(widget), key.c_str()))
        return;
    Takeover *takeover   = new Takeover{widget, signal, name};
    takeover->ownHandler = p_g_signal_connect_data(widget, name, G_CALLBACK(takenOver), takeover, nullptr,
                                                   G_CONNECT_SWAPPED);
    p_g_object_set_data_full(reinterpret_cast<GObject *>(widget), key.c_str(), takeover, deleteTakeover);
}

static void unwatchSignal(GtkWidget *widget, const char *name)
{
    std::string key      = getTakeoverKey(name);
    Takeover   *takeover = static_cast<Takeover *>(p_g_object_get_data(reinterpret_cast<GObject *>(widget),
                                                                       key.c_str()));
    if (!takeover)
        return;
    for (gulong handler: takeover->blocked)
        p_g_signal_handler_unblock(widget, handler);
    p_g_signal_handler_disconnect(widget, takeover->ownHandler);
    p_g_object_set_data_full(reinterpret_cast<GObject *>(widget), key.c_str(), nullptr, nullptr);
}

// The emission under way is the plugin's from here on; false if the widget is not watched for it
static bool takeOver(GtkWidget *widget, const char *name)
{
    std::string key      = getTakeoverKey(name);
    Takeover   *takeover = static_cast<Takeover *>(p_g_object_get_data(reinterpret_cast<GObject *>(widget),
                                                                       key.c_str()));
    if (!takeover)
        return false;
    p_g_signal_handler_block(widget, takeover->ownHandler);
    while (gulong handler = p_g_signal_handler_find(widget, GSignalMatchType(G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_UNBLOCKED),
                                                    takeover->signal, 0, nullptr, nullptr, nullptr)) {
        p_g_signal_handler_block(widget, handler);
        takeover->blocked.push_back(handler);
    }
    p_g_signal_handler_unblock(widget, takeover->ownHandler);
    return true;
}

// Shown as tall as so many lines of the input field's font, or this big without it
enum { THUMBNAIL_LINES = 5, THUMBNAIL_SIZE = 96 };
// A thumbnail in the input field stands for its placeholder, which Pidgin takes as its HTML and
// its text, as it takes those of its own pictures
static const char THUMBNAIL_KEY[]      = "tdlib-purple-thumbnail";
static const char THUMBNAIL_PATH_KEY[] = "tdlib-purple-thumbnail-path";
static const char IMHTML_HTML_KEY[]    = "gtkimhtml_htmltext";
static const char IMHTML_TEXT_KEY[]    = "gtkimhtml_plaintext";

// A placeholder of something pasted, in a conversation's input field, gets a menu of its own: to
// send it the other way, as a file or not, or to take it out
static PlaceholderHandler g_placeholderHandler = nullptr;

void setPlaceholderHandler(PlaceholderHandler handler)
{
    g_placeholderHandler = handler;
}

static void addMenuItems(GtkWidget *menu, const std::vector<MenuItem> &items, unsigned columns);

static GtkTextChildAnchor *getThumbnailAt(const GtkTextIter &iter)
{
    GtkTextChildAnchor *anchor = p_gtk_text_iter_get_child_anchor(&iter);
    return anchor && p_g_object_get_data(reinterpret_cast<GObject *>(anchor), THUMBNAIL_KEY) ? anchor : nullptr;
}

static void setThumbnailPlaceholder(GtkTextChildAnchor *anchor, GtkWidget *box, const std::string &placeholder)
{
    GObject *object = reinterpret_cast<GObject *>(anchor);
    p_g_object_set_data_full(object, THUMBNAIL_KEY, g_strdup(placeholder.c_str()), g_free);
    p_g_object_set_data_full(object, IMHTML_HTML_KEY, g_markup_escape_text(placeholder.c_str(), -1), g_free);
    // Without text, Pidgin would send no message of a thumbnail alone
    p_g_object_set_data_full(object, IMHTML_TEXT_KEY, g_strdup(placeholder.c_str()), g_free);
    std::string tooltip = placeholder + "\n" + static_cast<const char *>(p_g_object_get_data(object, THUMBNAIL_PATH_KEY));
    p_gtk_widget_set_tooltip_text(box, tooltip.c_str());
}

// Of a line of the widget's font, as Pango lays it out; 0 if it cannot be told
static int getLineHeight(GtkWidget *widget)
{
    PangoLayout *layout = p_gtk_widget_create_pango_layout ? p_gtk_widget_create_pango_layout(widget, "A") : nullptr;
    int          height = 0;
    if (layout) {
        p_pango_layout_get_pixel_size(layout, nullptr, &height);
        p_g_object_unref(layout);
    }
    return height;
}

static int getThumbnailSize(GtkWidget *entry)
{
    int height = getLineHeight(entry);
    return (height > 0) ? height * THUMBNAIL_LINES : THUMBNAIL_SIZE;
}

int makeStandInPicture(const std::string &lowGrade, int width, int height, bool clear)
{
    static bool found = LOOK_UP(gdk_pixbuf_loader_new) && LOOK_UP(gdk_pixbuf_loader_write) &&
                        LOOK_UP(gdk_pixbuf_loader_close) && LOOK_UP(gdk_pixbuf_loader_get_pixbuf) &&
                        LOOK_UP(gdk_pixbuf_scale_simple) && LOOK_UP(gdk_pixbuf_new) && LOOK_UP(gdk_pixbuf_fill) &&
                        LOOK_UP(gdk_pixbuf_save_to_buffer) && LOOK_UP(g_object_unref);
    if (!g_pidginUi || !found || (width <= 0) || (height <= 0))
        return 0;

    GdkPixbuf *pixbuf = nullptr;
    if (!lowGrade.empty()) {
        GdkPixbufLoader *loader = p_gdk_pixbuf_loader_new();
        bool read = p_gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar *>(lowGrade.data()),
                                              lowGrade.size(), nullptr) &&
                    p_gdk_pixbuf_loader_close(loader, nullptr);
        GdkPixbuf *small = read ? p_gdk_pixbuf_loader_get_pixbuf(loader) : nullptr;
        if (!read)
            p_gdk_pixbuf_loader_close(loader, nullptr);
        if (small)
            pixbuf = p_gdk_pixbuf_scale_simple(small, width, height, GDK_INTERP_BILINEAR);
        p_g_object_unref(loader);
    }

    gchar      *data   = nullptr;
    gsize       size   = 0;
    bool        saved;
    const char *name;
    if (pixbuf) {
        // Blurred as it is, JPEG keeps it small, and is quick to make
        saved = p_gdk_pixbuf_save_to_buffer(pixbuf, &data, &size, "jpeg", nullptr, "quality", "60", nullptr);
        name  = "preview.jpg";
    } else {
        pixbuf = p_gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
        if (!pixbuf)
            return 0;
        p_gdk_pixbuf_fill(pixbuf, clear ? 0 : 0xC0C0C0FF);
        // One colour, which PNG makes next to nothing of
        saved = p_gdk_pixbuf_save_to_buffer(pixbuf, &data, &size, "png", nullptr, nullptr);
        name  = "preview.png";
    }
    p_g_object_unref(pixbuf);
    // The store takes the data
    return saved ? purple_imgstore_add_with_id(data, size, name) : 0;
}

unsigned getTextLineHeight()
{
    if (!g_pidginUi)
        return 0;
    // A conversation's own, all of them having the same, or else GTK's
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        GtkIMHtml *imhtml = getIMHtml(static_cast<PurpleConversation *>(item->data));
        if (imhtml)
            return std::max(getLineHeight(reinterpret_cast<GtkWidget *>(imhtml)), 0);
    }
    if (!p_gtk_widget_get_default_style)
        return 0;
    GtkStyle     *style   = p_gtk_widget_get_default_style();
    PangoContext *context = p_gdk_pango_context_get();
    PangoFontMetrics *metrics = (style && context) ? p_pango_context_get_metrics(context, style->font_desc, nullptr)
                                                   : nullptr;
    int height = 0;
    if (metrics) {
        height = PANGO_PIXELS(p_pango_font_metrics_get_ascent(metrics) + p_pango_font_metrics_get_descent(metrics));
        p_pango_font_metrics_unref(metrics);
    }
    if (context)
        p_g_object_unref(context);
    return std::max(height, 0);
}

static void showThumbnail(GtkWidget *image, int size, const std::string &png)
{
    GdkPixbuf *pixbuf = png.empty() ? nullptr : p_gdk_pixbuf_new_from_file_at_scale(png.c_str(), size, size, TRUE,
                                                                                    nullptr);
    if (pixbuf) {
        p_gtk_image_set_from_pixbuf(reinterpret_cast<GtkImage *>(image), pixbuf);
        p_g_object_unref(pixbuf);
    }
}

static void removeThumbnail(GtkTextChildAnchor *anchor)
{
    if (p_gtk_text_child_anchor_get_deleted(anchor))
        return;
    GtkTextBuffer *buffer = nullptr;
    GList         *widgets = p_gtk_text_child_anchor_get_widgets(anchor);
    if (widgets)
        buffer = p_gtk_text_view_get_buffer(reinterpret_cast<GtkTextView *>(
            p_gtk_widget_get_parent(static_cast<GtkWidget *>(widgets->data))));
    g_list_free(widgets);
    if (!buffer)
        return;
    GtkTextIter from, to;
    p_gtk_text_buffer_get_iter_at_child_anchor(buffer, &from, anchor);
    to = from;
    p_gtk_text_iter_forward_char(&to);
    p_gtk_text_buffer_delete(buffer, &from, &to);
}

// Right-clicked, a thumbnail's menu: to send it the other way, as a file or not, or to take it out
static gboolean thumbnailClicked(GtkWidget *box, GdkEventButton *event, gpointer data)
{
    if ((event->type != GDK_BUTTON_PRESS) || (event->button != 3) || !g_placeholderHandler)
        return FALSE;
    GtkTextChildAnchor *anchor = static_cast<GtkTextChildAnchor *>(data);
    GtkWidget          *entry  = p_gtk_widget_get_parent(box);
    PurpleConversation *conv   = entry ? findEntryConversation(entry) : nullptr;
    const char         *placeholder = static_cast<const char *>(
        p_g_object_get_data(reinterpret_cast<GObject *>(anchor), THUMBNAIL_KEY));
    PastedForms forms;
    if (!conv || !placeholder || !g_placeholderHandler(conv, placeholder, forms))
        return FALSE;

    // The anchor kept, should it go meanwhile
    std::shared_ptr<GObject> kept(reinterpret_cast<GObject *>(anchor), p_g_object_unref);
    p_g_object_ref(anchor);
    std::vector<MenuItem> items;
    if (!forms.other.empty()) {
        MenuItem item;
        item.label  = forms.otherLabel;
        std::string other = forms.other;
        item.action = [kept, box, other]() {
            GtkTextChildAnchor *anchor = reinterpret_cast<GtkTextChildAnchor *>(kept.get());
            if (!p_gtk_text_child_anchor_get_deleted(anchor))
                setThumbnailPlaceholder(anchor, box, other);
        };
        items.push_back(std::move(item));
    }
    MenuItem remove;
    remove.label  = forms.removeLabel;
    remove.action = [kept]() { removeThumbnail(reinterpret_cast<GtkTextChildAnchor *>(kept.get())); };
    items.push_back(std::move(remove));

    GtkWidget *menu = p_gtk_menu_new();
    addMenuItems(menu, items, 0);
    p_gtk_widget_show_all(menu);
    p_g_signal_connect_data(menu, "selection-done", G_CALLBACK(p_gtk_widget_destroy), nullptr, nullptr,
                            GConnectFlags(0));
    p_gtk_menu_popup(reinterpret_cast<GtkMenu *>(menu), nullptr, nullptr, nullptr, nullptr, event->button, event->time);
    return TRUE;
}

// At the iterator, a thumbnail of the file, standing for the placeholder: an icon of its type until
// one is found; false if there can be none
static bool insertThumbnailAt(GtkWidget *entry, GtkTextIter *where, const PastedPlaceholder &pasted)
{
    if (!p_gtk_text_buffer_create_child_anchor || pasted.path.empty())
        return false;
    GtkTextBuffer      *buffer = reinterpret_cast<GtkIMHtml *>(entry)->text_buffer;
    GtkTextChildAnchor *anchor = p_gtk_text_buffer_create_child_anchor(buffer, where);
    p_g_object_set_data_full(reinterpret_cast<GObject *>(anchor), THUMBNAIL_PATH_KEY, g_strdup(pasted.path.c_str()),
                             g_free);

    std::string mime  = guessMimeType(pasted.path);
    gchar      *type  = mime.empty() ? nullptr : g_content_type_from_mime_type(mime.c_str());
    gchar      *icon  = type ? g_content_type_get_generic_icon_name(type) : nullptr;
    GtkWidget  *image = p_gtk_image_new_from_icon_name(icon ? icon : "text-x-generic", GTK_ICON_SIZE_DND);
    g_free(icon);
    g_free(type);
    GtkWidget *box = p_gtk_event_box_new();
    p_gtk_event_box_set_visible_window(reinterpret_cast<GtkEventBox *>(box), FALSE);
    p_gtk_container_add(reinterpret_cast<GtkContainer *>(box), image);
    setThumbnailPlaceholder(anchor, box, pasted.placeholder);
    p_g_signal_connect_data(box, "button-press-event", G_CALLBACK(thumbnailClicked), anchor, nullptr,
                            GConnectFlags(0));
    p_gtk_text_view_add_child_at_anchor(reinterpret_cast<GtkTextView *>(entry), box, anchor);
    p_gtk_widget_show_all(box);

    // A picture is its own; anything else as file managers have it, or a thumbnailer makes it
    int size = getThumbnailSize(entry);
    if (pasted.kind == AttachmentKind::Photo)
        showThumbnail(image, size, pasted.path);
    else {
        p_g_object_ref(image);
        findThumbnail(pasted.path, mime, size, [image, size](const std::string &png) {
            showThumbnail(image, size, png);
            p_g_object_unref(image);
        });
    }
    return true;
}

static std::string getEntrySlice(GtkTextBuffer *buffer);
static bool findBracketed(const std::string &text, gint offset, std::string &bracketed, gint &start);

// The placeholders written in the field, of things pasted that are here, become their thumbnails
static void thumbnailPlaceholders(GtkWidget *entry)
{
    PurpleConversation *conv = findEntryConversation(entry);
    if (!conv || !g_placeholderHandler || !p_gtk_text_buffer_create_child_anchor)
        return;
    GtkTextBuffer *buffer = reinterpret_cast<GtkIMHtml *>(entry)->text_buffer;
    std::string    text   = getEntrySlice(buffer);
    // From the end, so that the offsets before stay
    for (gint offset = g_utf8_strlen(text.c_str(), -1) - 1; offset >= 0; offset--) {
        std::string placeholder;
        gint        start;
        PastedForms forms;
        if ((*g_utf8_offset_to_pointer(text.c_str(), offset) != '[') ||
            !findBracketed(text, offset, placeholder, start) ||
            !g_placeholderHandler(conv, placeholder, forms) || forms.path.empty())
            continue;
        GtkTextIter from, to;
        p_gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
        p_gtk_text_buffer_get_iter_at_offset(buffer, &to, start + g_utf8_strlen(placeholder.c_str(), -1));
        p_gtk_text_buffer_delete(buffer, &from, &to);
        insertThumbnailAt(entry, &from, {placeholder, forms.path, forms.kind});
    }
}

// Thumbnails at the cursor, or the placeholders where there can be none
static void insertPlaceholders(GtkWidget *entry, const std::vector<PastedPlaceholder> &placeholders)
{
    GtkTextBuffer *buffer = reinterpret_cast<GtkIMHtml *>(entry)->text_buffer;
    for (const PastedPlaceholder &pasted: placeholders) {
        GtkTextIter cursor;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &cursor, p_gtk_text_buffer_get_insert(buffer));
        if (!insertThumbnailAt(entry, &cursor, pasted))
            p_gtk_text_buffer_insert_at_cursor(buffer, (pasted.placeholder + " ").c_str(), -1);
    }
}

// Pidgin's Backspace after a picture puts its text in its place, as it does a smiley's: not that
// of a thumbnail, whose text is only for sending
static gboolean backspacing(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkWidget *view = paramCount ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    if (!view || !findEntryConversation(view))
        return TRUE;
    GtkTextBuffer *buffer = reinterpret_cast<GtkIMHtml *>(view)->text_buffer;
    GtkTextIter    cursor;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &cursor, p_gtk_text_buffer_get_insert(buffer));
    GtkTextChildAnchor *anchor = p_gtk_text_iter_backward_char(&cursor) ? getThumbnailAt(cursor) : nullptr;
    if (anchor)
        p_g_object_set_data(reinterpret_cast<GObject *>(anchor), IMHTML_TEXT_KEY, nullptr);
    return TRUE;
}

// Pastes the placeholders of the files copied, if any
static bool pasteFiles(GtkWidget *entry, PurpleConversation *conv)
{
    GtkClipboard *clipboard = p_gtk_widget_get_clipboard(entry, GDK_SELECTION_CLIPBOARD);
    if (!clipboard || !g_pastedHandler)
        return false;
    gchar **uris = p_gtk_clipboard_wait_for_uris(clipboard);
    std::vector<std::string> files = getLocalPaths(uris);
    g_strfreev(uris);
    if (files.empty() || !takeOver(entry, "paste-clipboard"))
        return false;
    ClipboardContent content;
    content.files = files;
    insertPlaceholders(entry, g_pastedHandler(conv, content));
    return true;
}

struct EntryPaste {
    PurpleConversation            *conv;
    std::vector<PastedPlaceholder> placeholders;
    std::string                    text;
};

static gboolean pasteIntoEntry(gpointer data)
{
    std::unique_ptr<EntryPaste> paste(static_cast<EntryPaste *>(data));
    // Unless the conversation is gone by now
    GList *item = purple_get_conversations();
    while (item && (item->data != paste->conv))
        item = item->next;
    GtkWidget *entry = item ? getEntry(paste->conv) : nullptr;
    if (entry) {
        GtkTextBuffer *buffer = reinterpret_cast<GtkIMHtml *>(entry)->text_buffer;
        if (p_gtk_text_buffer_insert_at_cursor) {
            insertPlaceholders(entry, paste->placeholders);
            if (!paste->text.empty())
                p_gtk_text_buffer_insert_at_cursor(buffer, (paste->text + " ").c_str(), -1);
        }
    }
    return FALSE;
}

void pasteIntoEntryLater(PurpleConversation *conv, const std::vector<PastedPlaceholder> &placeholders)
{
    g_idle_add(pasteIntoEntry, new EntryPaste{conv, placeholders, ""});
}

static gboolean pastingClipboard(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    gpointer   instance = paramCount ? p_g_value_get_object(&params[0]) : nullptr;
    GtkWidget *entry    = static_cast<GtkWidget *>(instance);
    PurpleConversation *conv = entry ? findEntryConversation(entry) : nullptr;
    if (!conv || !g_pastedHandler || pasteFiles(entry, conv))
        return TRUE;
    // A picture gets a placeholder too, rather than going into the field as Pidgin's own, which
    // Backspace would turn into the text "[Image]"
    ClipboardContent content;
    content.imageId = storeClipboardPicture(entry);
    if (content.imageId && takeOver(entry, "paste-clipboard"))
        insertPlaceholders(entry, g_pastedHandler(conv, content));
    else if (content.imageId)
        purple_imgstore_unref_by_id(content.imageId);
    // Kept for the next paste
    return TRUE;
}

bool readPidginClipboard(PurpleConversation *conv, ClipboardContent &content)
{
    GtkWidget *entry = g_pasteHook ? getEntry(conv) : nullptr;
    if (!entry)
        return false;
    GtkClipboard *clipboard = p_gtk_clipboard_wait_for_uris ?
                              p_gtk_widget_get_clipboard(entry, GDK_SELECTION_CLIPBOARD) : nullptr;
    gchar **uris = clipboard ? p_gtk_clipboard_wait_for_uris(clipboard) : nullptr;
    if (uris) {
        content.files = getLocalPaths(uris);
        g_strfreev(uris);
    }
    if (content.files.empty())
        content.imageId = storeClipboardPicture(entry);
    return true;
}

// Telegram takes no bigger photo
enum { MAX_FETCHED_PICTURE = 10 * 1024 * 1024 };

struct PictureFetch {
    PurpleConversation *conv;
    std::string         url;
};

static bool isPicture(const guchar *data, gsize length)
{
    GdkPixbufLoader *loader = p_gdk_pixbuf_loader_new();
    bool picture = p_gdk_pixbuf_loader_write(loader, data, length, nullptr) &&
                   p_gdk_pixbuf_loader_close(loader, nullptr) && p_gdk_pixbuf_loader_get_pixbuf(loader);
    if (!picture)
        p_gdk_pixbuf_loader_close(loader, nullptr);
    p_g_object_unref(loader);
    return picture;
}

// A picture goes into the input field, anything else as its link
static void pictureFetched(PurpleUtilFetchUrlData *, gpointer data, const gchar *content, gsize length,
                           const gchar *error)
{
    std::unique_ptr<PictureFetch> fetch(static_cast<PictureFetch *>(data));
    if (content && length && isPicture(reinterpret_cast<const guchar *>(content), length)) {
        gchar *name = g_path_get_basename(fetch->url.c_str());
        // The store takes the copy
        gpointer copy = g_malloc(length);
        memcpy(copy, content, length);
        ClipboardContent picture;
        picture.imageId = purple_imgstore_add_with_id(copy, length, name);
        g_free(name);
        pasteIntoEntry(new EntryPaste{fetch->conv, g_pastedHandler(fetch->conv, picture), ""});
    } else {
        purple_debug_misc(config::pluginId, "Dropped %s, not a picture: %s\n", fetch->url.c_str(),
                          error ? error : "other content");
        pasteIntoEntry(new EntryPaste{fetch->conv, {}, fetch->url});
    }
}

static PurpleConversation *findWidgetConversation(GtkWidget *widget)
{
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv = static_cast<PurpleConversation *>(item->data);
        if (!strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)), config::pluginId) &&
            getIMHtml(conv) && ((getEntry(conv) == widget) || (reinterpret_cast<GtkWidget *>(getIMHtml(conv)) == widget)))
            return conv;
    }
    return nullptr;
}

// Files dropped get their placeholders in the input field, and pictures dropped from elsewhere,
// such as a browser, as links, are fetched to go there
static gboolean droppingData(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    if (paramCount < 7)
        return TRUE;
    GtkWidget          *widget = static_cast<GtkWidget *>(p_g_value_get_object(&params[0]));
    PurpleConversation *conv   = widget ? findWidgetConversation(widget) : nullptr;
    GtkSelectionData   *data   = static_cast<GtkSelectionData *>(p_g_value_get_boxed(&params[4]));
    if (!conv || !data || (data->length <= 0) || !g_pastedHandler ||
        (data->target != p_gdk_atom_intern("text/uri-list", FALSE)))
        return TRUE;

    std::string              list(reinterpret_cast<const char *>(data->data), data->length);
    gchar                  **uris = g_uri_list_extract_uris(list.c_str());
    std::vector<std::string> files = getLocalPaths(uris);
    std::vector<std::string> urls;
    for (gchar **uri = uris; uri && *uri; uri++)
        if (g_str_has_prefix(*uri, "http://") || g_str_has_prefix(*uri, "https://"))
            urls.push_back(*uri);
    g_strfreev(uris);
    if ((files.empty() && urls.empty()) || !takeOver(widget, "drag-data-received"))
        return TRUE;

    p_gtk_drag_finish(static_cast<GdkDragContext *>(p_g_value_get_object(&params[1])), TRUE, FALSE,
                      p_g_value_get_uint(&params[6]));
    GtkWidget *entry = getEntry(conv);
    if (!files.empty()) {
        ClipboardContent content;
        content.files = files;
        insertPlaceholders(entry, g_pastedHandler(conv, content));
    }
    for (const std::string &url: urls) {
        purple_debug_misc(config::pluginId, "Fetching %s, dropped\n", url.c_str());
        PictureFetch *fetch = new PictureFetch{conv, url};
        purple_util_fetch_url_len(url.c_str(), TRUE, nullptr, TRUE, MAX_FETCHED_PICTURE, pictureFetched, fetch);
    }
    p_gtk_widget_grab_focus(entry);
    return TRUE;
}

// The tab's text, not editable, takes no drop of its own accord: links and files it does take, as
// the input field does
static gboolean draggingOverText(GtkWidget *widget, GdkDragContext *context, gint, gint, guint time, gpointer)
{
    if (p_gtk_drag_dest_find_target(widget, context, nullptr) != p_gdk_atom_intern("text/uri-list", FALSE))
        return FALSE;
    p_gdk_drag_status(context, GDK_ACTION_COPY, time);
    return TRUE;
}

static gboolean droppingOnText(GtkWidget *widget, GdkDragContext *context, gint, gint, guint time, gpointer)
{
    GdkAtom target = p_gdk_atom_intern("text/uri-list", FALSE);
    if (p_gtk_drag_dest_find_target(widget, context, nullptr) != target)
        return FALSE;
    p_gtk_drag_get_data(widget, context, target, time);
    return TRUE;
}

static const char TEXT_DROP_KEY[] = "tdlib-purple-text-drop";

struct TextDrop {
    gulong motion;
    gulong drop;
};

static void deleteTextDrop(gpointer data)
{
    delete static_cast<TextDrop *>(data);
}

static void watchDrags(GtkWidget *imhtml);
static void unwatchDrags(GtkWidget *imhtml);

static void watchConversation(PidginConversation *gtkconv)
{
    PurpleConversation *conv = gtkconv ? gtkconv->active_conv : nullptr;
    if (!conv || !gtkconv->entry || !gtkconv->imhtml || !g_pasteHook || !p_gtk_clipboard_wait_for_uris ||
        strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)), config::pluginId))
        return;
    watchSignal(gtkconv->entry, g_pasteSignal, "paste-clipboard");
    watchDrags(gtkconv->imhtml);
    if (!g_dropHook)
        return;
    watchSignal(gtkconv->entry, g_dropSignal, "drag-data-received");
    watchSignal(gtkconv->imhtml, g_dropSignal, "drag-data-received");
    if (!p_g_object_get_data(reinterpret_cast<GObject *>(gtkconv->imhtml), TEXT_DROP_KEY)) {
        TextDrop *drop = new TextDrop;
        drop->motion   = p_g_signal_connect_data(gtkconv->imhtml, "drag-motion", G_CALLBACK(draggingOverText),
                                                 nullptr, nullptr, GConnectFlags(0));
        drop->drop     = p_g_signal_connect_data(gtkconv->imhtml, "drag-drop", G_CALLBACK(droppingOnText),
                                                 nullptr, nullptr, GConnectFlags(0));
        p_g_object_set_data_full(reinterpret_cast<GObject *>(gtkconv->imhtml), TEXT_DROP_KEY, drop,
                                 deleteTextDrop);
    }
}

static void unwatchConversation(PurpleConversation *conv)
{
    GtkWidget *entry  = getEntry(conv);
    GtkWidget *imhtml = entry ? reinterpret_cast<GtkWidget *>(getIMHtml(conv)) : nullptr;
    if (!entry)
        return;
    unwatchSignal(entry, "paste-clipboard");
    unwatchDrags(imhtml);
    unwatchSignal(entry, "drag-data-received");
    unwatchSignal(imhtml, "drag-data-received");
    TextDrop *drop = static_cast<TextDrop *>(p_g_object_get_data(reinterpret_cast<GObject *>(imhtml), TEXT_DROP_KEY));
    if (drop) {
        p_g_signal_handler_disconnect(imhtml, drop->motion);
        p_g_signal_handler_disconnect(imhtml, drop->drop);
        p_g_object_set_data_full(reinterpret_cast<GObject *>(imhtml), TEXT_DROP_KEY, nullptr, nullptr);
    }
}

static void connectPaste(gpointer plugin)
{
    if (!LOOK_UP(gtk_imhtml_get_type) || !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook) || !LOOK_UP(g_value_get_object) ||
        !LOOK_UP(gtk_widget_get_clipboard) || !LOOK_UP(gtk_clipboard_wait_is_image_available) ||
        !LOOK_UP(gtk_clipboard_wait_for_image) || !LOOK_UP(gdk_pixbuf_save_to_buffer) ||
        !LOOK_UP(g_object_unref) ||
        !LOOK_UP(gtk_text_buffer_get_insert))
    {
        purple_debug_warning(config::pluginId, "Cannot paste pictures: some of GTK is not found\n");
        return;
    }
    g_pasteSignal = p_g_signal_lookup("paste-clipboard", p_gtk_imhtml_get_type());
    if (g_pasteSignal)
        g_pasteHook = p_g_signal_add_emission_hook(g_pasteSignal, 0, pastingClipboard, nullptr, nullptr);
    if (!g_pasteHook || !LOOK_UP(gtk_clipboard_wait_for_uris) || !LOOK_UP(g_signal_handler_find) ||
        !LOOK_UP(g_signal_handler_block) || !LOOK_UP(g_signal_handler_unblock) ||
        !LOOK_UP(g_signal_stop_emission_by_name) || !LOOK_UP(gtk_text_buffer_insert_at_cursor))
    {
        p_gtk_clipboard_wait_for_uris = nullptr;
        purple_debug_warning(config::pluginId, "Cannot paste files: some of GTK is not found\n");
        return;
    }
    if (LOOK_UP(gtk_widget_get_type) && LOOK_UP(g_value_get_boxed) && LOOK_UP(g_value_get_uint) &&
        LOOK_UP(gdk_atom_intern) && LOOK_UP(gtk_drag_finish) && LOOK_UP(gtk_drag_dest_find_target) &&
        LOOK_UP(gdk_drag_status) && LOOK_UP(gtk_drag_get_data) && LOOK_UP(gdk_pixbuf_loader_new) &&
        LOOK_UP(gdk_pixbuf_loader_write) && LOOK_UP(gdk_pixbuf_loader_close) &&
        LOOK_UP(gdk_pixbuf_loader_get_pixbuf))
    {
        g_dropSignal = p_g_signal_lookup("drag-data-received", p_gtk_widget_get_type());
        if (g_dropSignal)
            g_dropHook = p_g_signal_add_emission_hook(g_dropSignal, 0, droppingData, nullptr, nullptr);
    }
    if (!g_dropHook)
        purple_debug_warning(config::pluginId, "Cannot take drops: some of GTK is not found\n");
    if (!LOOK_UP(gtk_text_buffer_create_child_anchor) || !LOOK_UP(gtk_text_view_add_child_at_anchor) ||
        !LOOK_UP(gtk_text_iter_get_child_anchor) || !LOOK_UP(gtk_image_new_from_icon_name) ||
        !LOOK_UP(gtk_image_set_from_pixbuf) || !LOOK_UP(gdk_pixbuf_new_from_file_at_scale) ||
        !LOOK_UP(g_object_ref) || !LOOK_UP(gtk_widget_set_tooltip_text) || !LOOK_UP(gtk_widget_show) ||
        !LOOK_UP(gtk_text_buffer_place_cursor) || !LOOK_UP(gtk_text_buffer_delete) ||
        !LOOK_UP(gtk_text_iter_forward_char) || !LOOK_UP(gtk_text_iter_backward_char) ||
        !LOOK_UP(gtk_text_buffer_get_iter_at_child_anchor) || !LOOK_UP(gtk_text_child_anchor_get_deleted) ||
        !LOOK_UP(gtk_text_child_anchor_get_widgets) || !LOOK_UP(gtk_text_view_get_buffer) ||
        !LOOK_UP(gtk_widget_get_parent) || !LOOK_UP(gtk_event_box_new) ||
        !LOOK_UP(gtk_event_box_set_visible_window) || !LOOK_UP(gtk_container_add) || !LOOK_UP(gtk_menu_new) ||
        !LOOK_UP(gtk_menu_popup) || !LOOK_UP(gtk_widget_show_all) || !LOOK_UP(gtk_widget_destroy) ||
        !LOOK_UP(g_object_set_data) || !LOOK_UP(gtk_text_buffer_get_slice) || !LOOK_UP(g_signal_connect_data) ||
        !(g_backspaceSignal = p_g_signal_lookup("backspace", p_gtk_imhtml_get_type())) ||
        !(g_backspaceHook = p_g_signal_add_emission_hook(g_backspaceSignal, 0, backspacing, nullptr, nullptr)))
    {
        p_gtk_text_buffer_create_child_anchor = nullptr;
        purple_debug_warning(config::pluginId, "Cannot show thumbnails of pasted files: some of GTK is not found\n");
    }
    purple_signal_connect(p_pidgin_conversations_get_handle(), "conversation-displayed", plugin,
                          PURPLE_CALLBACK(watchConversation), nullptr);
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv = static_cast<PurpleConversation *>(item->data);
        if (getIMHtml(conv))
            watchConversation(PIDGIN_CONVERSATION(conv));
    }
}

static void disconnectPaste()
{
    if (g_dropHook)
        p_g_signal_remove_emission_hook(g_dropSignal, g_dropHook);
    if (g_backspaceHook)
        p_g_signal_remove_emission_hook(g_backspaceSignal, g_backspaceHook);
    g_backspaceHook = 0;
    if (g_pasteHook) {
        p_g_signal_remove_emission_hook(g_pasteSignal, g_pasteHook);
        for (GList *item = purple_get_conversations(); item; item = item->next)
            unwatchConversation(static_cast<PurpleConversation *>(item->data));
    }
    g_pasteHook = 0;
    g_dropHook  = 0;
}

static void setEntryText(GtkWidget *entry, const char *html)
{
    p_gtk_imhtml_delete(reinterpret_cast<GtkIMHtml *>(entry), nullptr, nullptr);
    if (html && *html)
        p_gtk_imhtml_append_text_with_images(reinterpret_cast<GtkIMHtml *>(entry), html, GtkIMHtmlOptions(0), nullptr);
    thumbnailPlaceholders(entry);
}

// A quarter of the way to amber from the theme's own background, light or dark
static void tintEntry(GtkWidget *entry, bool tint)
{
    if (!tint) {
        p_gtk_widget_modify_base(entry, GTK_STATE_NORMAL, nullptr);
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

static gboolean entryKeyPressed(GtkWidget *, GdkEventKey *event, gpointer conv)
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
                                                    nullptr, GConnectFlags(0));
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
    purple_conversation_set_data(conv, ENTRY_EDIT_KEY, nullptr);
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
    GtkTextIter from, to;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &from, start);
    to = end;
    for (MarkKind kind: {MarkKind::Buttons, MarkKind::Reactions}) {
        GtkTextMark *areaMark = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, kind).c_str());
        GtkTextIter  areaEnd;
        if (areaMark) {
            p_gtk_text_buffer_get_iter_at_mark(buffer, &areaEnd, areaMark);
            if (p_gtk_text_iter_compare(&to, &areaEnd) < 0)
                to = areaEnd;
        }
    }
    // With the line break before it, which Pidgin put there for it
    if (p_gtk_text_iter_backward_char(&from) && (p_gtk_text_iter_get_char(&from) != '\n'))
        p_gtk_text_iter_forward_char(&from);
    p_gtk_imhtml_delete(imhtml, &from, &to);
    for (MarkKind kind: {MarkKind::Start, MarkKind::Body, MarkKind::End, MarkKind::Buttons, MarkKind::Reactions}) {
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
    // Or its line from the log, unless trimmed away since, its marks left together
    if (buffer && (!start || !end)) {
        start = p_gtk_text_buffer_get_mark(buffer, getLoggedMarkName(messageId, true).c_str());
        end   = p_gtk_text_buffer_get_mark(buffer, getLoggedMarkName(messageId, false).c_str());
    }
    if (!start || !end)
        return false;

    GtkTextIter startIter, endIter;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &startIter, start);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &endIter, end);
    if (p_gtk_text_iter_compare(&startIter, &endIter) >= 0)
        return false;
    p_gtk_text_buffer_select_range(buffer, &startIter, &endIter);
    // The message's beginning a little below the top, so what came just before shows too
    p_gtk_text_view_scroll_to_mark(reinterpret_cast<GtkTextView *>(imhtml), start, 0.0, TRUE, 0.0, 0.1);
    return true;
}

static void itemActivated(GtkWidget *, gpointer data)
{
    (*static_cast<std::function<void()> *>(data))();
}

static void deleteAction(gpointer data, GClosure *)
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
    bool       fileGiven      = false; // by the menu's own item, on top
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
    std::vector<MenuItem> shown;
    for (const MenuItem &item: items)
        if (!(menu->fileGiven && item.copiesFile))
            shown.push_back(item);
    if (shown.empty() && menu->separator)
        p_gtk_widget_destroy(menu->separator);
    // Nothing else in it either: said so, rather than left empty, as Pidgin would have
    else if (shown.empty()) {
        // TRANSLATOR: Disabled item of a menu that has nothing in it.
        GtkWidget *none = p_gtk_menu_item_new_with_label(_("No actions available"));
        p_gtk_widget_set_sensitive(none, FALSE);
        p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu->menu), none);
        p_gtk_widget_show(none);
    } else {
        addMenuItems(menu->menu, shown, 0);
        p_gtk_widget_show_all(menu->menu);
    }
    menu->separator = nullptr;
}

void addLinkMenuItems(GtkWidget *menu, const std::vector<MenuItem> &items)
{
    addMenuItems(menu, items, 0);
}

// What is on the clipboard, while it is ours, in the forms it is offered in
struct CopiedFile {
    std::string path;
    std::string uri;
};
enum CopiedForm { COPIED_URIS, COPIED_GNOME_FILES, COPIED_PICTURE, COPIED_TEXT };
static const char GNOME_COPIED_FILES[] = "x-special/gnome-copied-files";

static void gettingCopiedFile(GtkClipboard *, GtkSelectionData *data, guint form, gpointer owner)
{
    const CopiedFile *file = static_cast<const CopiedFile *>(owner);
    switch (form) {
    case COPIED_URIS: {
        gchar *uris[] = {const_cast<gchar *>(file->uri.c_str()), nullptr};
        p_gtk_selection_data_set_uris(data, uris);
        break;
    }
    case COPIED_GNOME_FILES: {
        // As Nautilus, and other file managers after it, put files copied there
        std::string list = "copy\n" + file->uri;
        p_gtk_selection_data_set(data, data->target, 8, reinterpret_cast<const guchar *>(list.c_str()), list.size());
        break;
    }
    case COPIED_PICTURE: {
        GError    *error  = nullptr;
        GdkPixbuf *pixbuf = p_gdk_pixbuf_new_from_file(file->path.c_str(), &error);
        if (pixbuf) {
            p_gtk_selection_data_set_pixbuf(data, pixbuf);
            p_g_object_unref(pixbuf);
        } else {
            purple_debug_warning(config::pluginId, "Cannot paste %s as a picture: %s\n", file->path.c_str(),
                                 error ? error->message : "unknown error");
            if (error)
                g_error_free(error);
        }
        break;
    }
    case COPIED_TEXT: {
        gchar *text = g_filename_to_utf8(file->path.c_str(), -1, nullptr, nullptr, nullptr);
        p_gtk_selection_data_set_text(data, text ? text : file->uri.c_str(), -1);
        g_free(text);
        break;
    }
    }
}

static void clearingCopiedFile(GtkClipboard *, gpointer owner)
{
    delete static_cast<CopiedFile *>(owner);
}

// Most programs take the first form they know: the file itself, then the picture, if it is one,
// then its path. File managers take files copied to the clipboard in a form of their own.
static GtkTargetList *makeCopiedForms(bool picture, bool clipboard)
{
    GtkTargetList *forms = p_gtk_target_list_new(nullptr, 0);
    p_gtk_target_list_add_uri_targets(forms, COPIED_URIS);
    if (clipboard)
        p_gtk_target_list_add(forms, p_gdk_atom_intern(GNOME_COPIED_FILES, FALSE), 0, COPIED_GNOME_FILES);
    if (picture)
        p_gtk_target_list_add_image_targets(forms, COPIED_PICTURE, TRUE);
    p_gtk_target_list_add_text_targets(forms, COPIED_TEXT);
    return forms;
}

static bool lookUpClipboard()
{
    return LOOK_UP(gtk_clipboard_get) && LOOK_UP(gtk_clipboard_set_with_data) &&
           LOOK_UP(gtk_clipboard_set_can_store) && LOOK_UP(gtk_target_list_new) && LOOK_UP(gtk_target_list_add) &&
           LOOK_UP(gtk_target_list_add_uri_targets) && LOOK_UP(gtk_target_list_add_image_targets) &&
           LOOK_UP(gtk_target_list_add_text_targets) && LOOK_UP(gtk_target_list_unref) &&
           LOOK_UP(gtk_target_table_new_from_list) && LOOK_UP(gtk_target_table_free) &&
           LOOK_UP(gtk_selection_data_set) && LOOK_UP(gtk_selection_data_set_uris) &&
           LOOK_UP(gtk_selection_data_set_text) && LOOK_UP(gtk_selection_data_set_pixbuf) &&
           LOOK_UP(gdk_pixbuf_new_from_file);
}

bool copyFileToClipboard(const std::string &path, bool picture)
{
    gchar *uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!uri || !lookUpClipboard()) {
        g_free(uri);
        return false;
    }
    CopiedFile *file = new CopiedFile{path, uri};
    g_free(uri);

    GtkTargetList  *forms     = makeCopiedForms(picture, true);
    gint            count;
    GtkTargetEntry *table     = p_gtk_target_table_new_from_list(forms, &count);
    GtkClipboard   *clipboard = p_gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    bool            copied    = p_gtk_clipboard_set_with_data(clipboard, table, count, gettingCopiedFile,
                                                              clearingCopiedFile, file);
    if (copied)
        // Kept by a clipboard manager, if there is one, after Pidgin quits
        p_gtk_clipboard_set_can_store(clipboard, nullptr, 0);
    else
        delete file;
    p_gtk_target_table_free(table, count);
    p_gtk_target_list_unref(forms);
    return copied;
}

bool copyTextToClipboard(const std::string &text)
{
    if (!LOOK_UP(gtk_clipboard_get) || !LOOK_UP(gtk_clipboard_set_text))
        return false;
    GtkClipboard *clipboard = p_gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    p_gtk_clipboard_set_text(clipboard, text.c_str(), -1);
    if (LOOK_UP(gtk_clipboard_set_can_store))
        p_gtk_clipboard_set_can_store(clipboard, nullptr, 0);
    return true;
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
        // Its text, its buttons or its reactions, whichever ends later: an edit's text goes in after
        // the end of reactions taken back, which is where the text ended
        bool before = false;
        for (MarkKind kind: {MarkKind::Reactions, MarkKind::Buttons, MarkKind::End}) {
            GtkTextMark *end = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, kind).c_str());
            GtkTextIter  endIter;
            if (end) {
                p_gtk_text_buffer_get_iter_at_mark(buffer, &endIter, end);
                before = before || (p_gtk_text_iter_compare(&where, &endIter) < 0);
            }
        }
        return before ? messageId : MessageId();
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

// The character at the point, in buffer coordinates, if the point is on it, not past the end of its
// line or beside the text
static bool getCharAt(GtkTextView *view, gint x, gint y, GtkTextIter &where)
{
    gint         trailing;
    GdkRectangle place;
    p_gtk_text_view_get_iter_at_position(view, &where, &trailing, x, y);
    p_gtk_text_view_get_iter_location(view, &where, &place);
    return (x >= place.x) && (x < place.x + place.width) && (y >= place.y) && (y < place.y + place.height);
}

// A file dragged out of a tab, to another program or another conversation: that of the link to it
// pressed on, as of a document or a video, the cover in it too, or that of the message whose picture
// was pressed on; told where it is once it is fetched
struct DraggedFile {
    GtkWidget      *view    = nullptr;
    gint            x, y;              // where the button was pressed, on the screen
    std::string     link;
    MessageId       messageId;
    bool            picture = false;   // dropped as a picture too
    GdkPixbuf      *icon    = nullptr; // the picture, smaller
    GdkDragContext *context = nullptr;
    bool            fetched = false;
    std::string     path;              // none if it cannot be had
    ~DraggedFile()
    {
        if (icon)
            p_g_object_unref(icon);
    }
};
// Pressed on, and dragged once the pointer moves far enough
static std::shared_ptr<DraggedFile> g_pressedFile;
static std::shared_ptr<DraggedFile> g_draggedFile;
static DraggedFileHandler           g_draggedFileHandler = nullptr;
static const char                   TEXT_DRAG_KEY[]      = "tdlib-purple-text-drag";
static const char                   LINK_URL_KEY[]       = "link_url";
static const char                   IMAGE_SAVE_KEY[]     = "image-save-data";
// A drop asks for the file at once, which is long enough to fetch it from TDLib, unless it is to be
// downloaded, which it is too, for a later drag
enum { DRAG_WAIT_SECONDS = 5, DRAG_ICON_SIZE = 128 };

void setDraggedFileHandler(DraggedFileHandler handler)
{
    g_draggedFileHandler = handler;
}

static GdkPixbuf *makeDragIcon(GtkWidget *image)
{
    // Pidgin's pictures are pixbufs, an animation's frames too, which it sets in turn
    GtkImage  *gtkImage = reinterpret_cast<GtkImage *>(image);
    GdkPixbuf *pixbuf   = (image && (p_gtk_image_get_storage_type(gtkImage) == GTK_IMAGE_PIXBUF))
                          ? p_gtk_image_get_pixbuf(gtkImage) : nullptr;
    if (!pixbuf)
        return nullptr;
    int width  = p_gdk_pixbuf_get_width(pixbuf);
    int height = p_gdk_pixbuf_get_height(pixbuf);
    if ((width <= DRAG_ICON_SIZE) && (height <= DRAG_ICON_SIZE))
        return static_cast<GdkPixbuf *>(p_g_object_ref(pixbuf));
    double scale = double(DRAG_ICON_SIZE) / std::max(width, height);
    return p_gdk_pixbuf_scale_simple(pixbuf, std::max(1, int(width * scale)), std::max(1, int(height * scale)),
                                     GDK_INTERP_BILINEAR);
}

// The reaction the character is part of, on its message's line, if any
static std::string getReactionKey(const GtkTextIter &where)
{
    std::string key;
    GSList     *tags = p_gtk_text_iter_get_tags(&where);
    for (GSList *item = tags; item && key.empty(); item = item->next) {
        const char *name = static_cast<GtkTextTag *>(item->data)->name;
        if (name && g_str_has_prefix(name, REACTION_TAG_PREFIX))
            key = name + strlen(REACTION_TAG_PREFIX);
    }
    g_slist_free(tags);
    return key;
}

// A reaction pressed on a message's line, clicked once let go where it was
namespace {
struct PressedReaction {
    GtkWidget  *view = nullptr;
    gdouble     x    = 0;
    gdouble     y    = 0;
    std::string key;
    MessageId   messageId;
};
}
static ReactionClickHandler g_reactionClickHandler = nullptr;
static PressedReaction      g_pressedReaction;

void setReactionClickHandler(ReactionClickHandler handler)
{
    g_reactionClickHandler = handler;
}

// The reaction under the pointer, and its message, if any
static bool findPointedReaction(GtkWidget *view, PurpleConversation *conv, std::string &key, MessageId &messageId)
{
    GtkTextView *textView = reinterpret_cast<GtkTextView *>(view);
    gint         x, y;
    GtkTextIter  where;
    p_gtk_widget_get_pointer(view, &x, &y);
    p_gtk_text_view_window_to_buffer_coords(textView, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
    if (!getCharAt(textView, x, y, where))
        return false;
    key       = getReactionKey(where);
    messageId = key.empty() ? MessageId() : findMessageAt(getTextBuffer(conv), where);
    return messageId.valid();
}

static void pressingReaction(GtkWidget *view, GdkEventButton *event)
{
    g_pressedReaction = PressedReaction();
    PurpleConversation *conv = (g_reactionClickHandler && (event->type == GDK_BUTTON_PRESS) && (event->button == 1))
                               ? findTabConversation(view) : nullptr;
    std::string         key;
    MessageId           messageId;
    if (conv && findPointedReaction(view, conv, key, messageId))
        g_pressedReaction = PressedReaction{view, event->x_root, event->y_root, key, messageId};
}

// Moved as far as a drag, it is no click, even if back where it was pressed by the time it is let go
static void movingOverReaction(GtkWidget *view, GdkEventMotion *event)
{
    if ((g_pressedReaction.view == view) &&
        p_gtk_drag_check_threshold(view, g_pressedReaction.x, g_pressedReaction.y, event->x_root, event->y_root))
        g_pressedReaction = PressedReaction();
}

// Let go on the reaction it was pressed on
static void releasingReaction(GtkWidget *view, GdkEventButton *event)
{
    PressedReaction     pressed = g_pressedReaction;
    g_pressedReaction = PressedReaction();
    PurpleConversation *conv = ((pressed.view == view) && (event->button == 1) && g_reactionClickHandler)
                               ? findTabConversation(view) : nullptr;
    std::string         key;
    MessageId           messageId;
    if (conv && findPointedReaction(view, conv, key, messageId) && (key == pressed.key) &&
        (messageId == pressed.messageId))
        g_reactionClickHandler(conv, pressed.messageId, pressed.key);
}

static gboolean pressingTab(GtkWidget *view, GdkEventButton *event, gpointer)
{
    pressingReaction(view, event);
    g_pressedFile.reset();
    PurpleConversation *conv = (g_draggedFileHandler && (event->type == GDK_BUTTON_PRESS) && (event->button == 1))
                               ? findTabConversation(view) : nullptr;
    if (!conv)
        return FALSE;
    GtkTextView *textView = reinterpret_cast<GtkTextView *>(view);
    gint         x, y;
    GtkTextIter  where;
    p_gtk_widget_get_pointer(view, &x, &y);
    p_gtk_text_view_window_to_buffer_coords(textView, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
    if (!getCharAt(textView, x, y, where))
        return FALSE;
    auto pressed = std::make_shared<DraggedFile>();
    // A picture, Pidgin's, is an event box of its own at an anchor of the text
    gpointer owner = nullptr;
    p_gdk_window_get_user_data(event->window, &owner);
    GtkWidget *box = (owner != view) ? static_cast<GtkWidget *>(owner) : nullptr;
    if (box) {
        GtkTextChildAnchor *anchor  = p_gtk_text_iter_get_child_anchor(&where);
        GList              *widgets = anchor ? p_gtk_text_child_anchor_get_widgets(anchor) : nullptr;
        bool                found   = g_list_find(widgets, box) && p_g_object_get_data(G_OBJECT(box), IMAGE_SAVE_KEY);
        g_list_free(widgets);
        if (!found)
            return FALSE;
        pressed->icon = makeDragIcon(p_gtk_bin_get_child(reinterpret_cast<GtkBin *>(box)));
    }
    // The link it is in, if any, of a video's cover too, rather than the message's own file
    GSList *tags = p_gtk_text_iter_get_tags(&where);
    for (GSList *item = tags; item && pressed->link.empty(); item = item->next) {
        const char *url = static_cast<const char *>(p_g_object_get_data(G_OBJECT(item->data), LINK_URL_KEY));
        if (url && g_str_has_prefix(url, MESSAGE_LINK_SCHEME))
            pressed->link = url;
    }
    g_slist_free(tags);
    if (pressed->link.empty()) {
        // A reaction's custom emoji is not the message's picture
        pressed->messageId = (box && getReactionKey(where).empty()) ? findMessageAt(getTextBuffer(conv), where)
                                                                     : MessageId();
        if (!pressed->messageId.valid())
            return FALSE;
        pressed->picture = true;
    }
    pressed->view = view;
    pressed->x    = event->x_root;
    pressed->y    = event->y_root;
    g_pressedFile = pressed;
    return FALSE;
}

static gboolean releasingTab(GtkWidget *view, GdkEventButton *event, gpointer)
{
    g_pressedFile.reset();
    releasingReaction(view, event);
    return FALSE;
}

static gboolean movingOverTab(GtkWidget *view, GdkEventMotion *event, gpointer)
{
    movingOverReaction(view, event);
    std::shared_ptr<DraggedFile> pressed = g_pressedFile;
    if (!pressed || (pressed->view != view) || !(event->state & GDK_BUTTON1_MASK) ||
        !p_gtk_drag_check_threshold(view, pressed->x, pressed->y, event->x_root, event->y_root))
        return FALSE;
    g_pressedFile.reset();
    PurpleConversation *conv = findTabConversation(view);
    if (!conv)
        return FALSE;
    // Nothing selected on the way, as GTK's own drag of the text would take that
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    cursor;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &cursor, p_gtk_text_buffer_get_insert(buffer));
    p_gtk_text_buffer_place_cursor(buffer, &cursor);

    GtkTargetList *forms = makeCopiedForms(pressed->picture, false);
    pressed->context     = p_gtk_drag_begin(view, forms, GDK_ACTION_COPY, 1, reinterpret_cast<GdkEvent *>(event));
    p_gtk_target_list_unref(forms);
    if (!pressed->context)
        return FALSE;
    if (pressed->icon)
        p_gtk_drag_set_icon_pixbuf(pressed->context, pressed->icon, p_gdk_pixbuf_get_width(pressed->icon) / 2,
                                   p_gdk_pixbuf_get_height(pressed->icon) / 2);
    g_draggedFile = pressed;
    std::weak_ptr<DraggedFile> dragged = pressed;
    g_draggedFileHandler(conv, pressed->messageId, pressed->link, [dragged](const std::string &path) {
        std::shared_ptr<DraggedFile> file = dragged.lock();
        if (file) {
            file->path    = path;
            file->fetched = true;
        }
    });
    return TRUE;
}

static gboolean dragWaitExpired(gpointer data)
{
    *static_cast<bool *>(data) = true;
    return FALSE;
}

static void gettingDraggedFile(GtkWidget *view, GdkDragContext *context, GtkSelectionData *data, guint form,
                               guint, gpointer)
{
    std::shared_ptr<DraggedFile> dragged = g_draggedFile;
    if (!dragged || (dragged->context != context))
        return;
    // Not GtkTextView's, which would give the text selected, if any
    p_g_signal_stop_emission_by_name(view, "drag-data-get");
    if (!dragged->fetched) {
        bool  expired = false;
        guint timer   = g_timeout_add_seconds(DRAG_WAIT_SECONDS, dragWaitExpired, &expired);
        while (!dragged->fetched && !expired)
            g_main_context_iteration(nullptr, TRUE);
        if (!expired)
            g_source_remove(timer);
    }
    gchar *uri = dragged->path.empty() ? nullptr : g_filename_to_uri(dragged->path.c_str(), nullptr, nullptr);
    if (!uri) {
        purple_debug_misc(config::pluginId, "Nothing to drop: the file is not here%s\n",
                          dragged->fetched ? "" : " yet");
        return;
    }
    CopiedFile file{dragged->path, uri};
    g_free(uri);
    gettingCopiedFile(nullptr, data, form, &file);
}

static void draggedFileEnded(GtkWidget *, GdkDragContext *context, gpointer)
{
    if (g_draggedFile && (g_draggedFile->context == context))
        g_draggedFile.reset();
}

struct TextDrag {
    std::vector<gulong> handlers;
};

static void deleteTextDrag(gpointer data)
{
    delete static_cast<TextDrag *>(data);
}

static void watchDrags(GtkWidget *imhtml)
{
    static bool found = lookUpClipboard() && LOOK_UP(gtk_text_view_get_iter_at_position) &&
                        LOOK_UP(gtk_text_view_get_iter_location) && LOOK_UP(gtk_text_iter_get_tags) &&
                        LOOK_UP(gtk_text_view_window_to_buffer_coords) && LOOK_UP(gtk_widget_get_pointer) &&
                        LOOK_UP(gtk_text_iter_get_marks) && LOOK_UP(gtk_text_mark_get_name) &&
                        LOOK_UP(gtk_drag_begin) && LOOK_UP(gtk_drag_check_threshold) &&
                        LOOK_UP(gtk_drag_set_icon_pixbuf) && LOOK_UP(gdk_window_get_user_data) &&
                        LOOK_UP(gtk_image_get_storage_type) && LOOK_UP(gtk_image_get_pixbuf) &&
                        LOOK_UP(gdk_pixbuf_scale_simple) && LOOK_UP(gdk_pixbuf_get_width) &&
                        LOOK_UP(gdk_pixbuf_get_height) && LOOK_UP(gtk_bin_get_child) &&
                        LOOK_UP(gtk_text_iter_get_child_anchor) && LOOK_UP(gtk_text_child_anchor_get_widgets) &&
                        LOOK_UP(gtk_text_buffer_place_cursor) && LOOK_UP(gtk_text_buffer_get_insert) &&
                        LOOK_UP(g_signal_stop_emission_by_name) && LOOK_UP(g_object_ref);
    if (!found) {
        static bool told = false;
        if (!told)
            purple_debug_warning(config::pluginId, "Cannot drag files out: some of GTK is not found\n");
        told = true;
        return;
    }
    if (p_g_object_get_data(G_OBJECT(imhtml), TEXT_DRAG_KEY))
        return;
    TextDrag *drag = new TextDrag;
    const std::pair<const char *, GCallback> signals[] = {
        {"button-press-event",   G_CALLBACK(pressingTab)},
        {"button-release-event", G_CALLBACK(releasingTab)},
        {"motion-notify-event",  G_CALLBACK(movingOverTab)},
        {"drag-data-get",        G_CALLBACK(gettingDraggedFile)},
        {"drag-end",             G_CALLBACK(draggedFileEnded)},
    };
    for (const auto &signal: signals)
        drag->handlers.push_back(p_g_signal_connect_data(imhtml, signal.first, signal.second, nullptr, nullptr,
                                                         GConnectFlags(0)));
    p_g_object_set_data_full(G_OBJECT(imhtml), TEXT_DRAG_KEY, drag, deleteTextDrag);
}

static void unwatchDrags(GtkWidget *imhtml)
{
    TextDrag *drag = imhtml ? static_cast<TextDrag *>(p_g_object_get_data(G_OBJECT(imhtml), TEXT_DRAG_KEY)) : nullptr;
    if (!drag)
        return;
    for (gulong handler: drag->handlers)
        p_g_signal_handler_disconnect(imhtml, handler);
    p_g_object_set_data_full(G_OBJECT(imhtml), TEXT_DRAG_KEY, nullptr, nullptr);
    g_pressedFile.reset();
    g_draggedFile.reset();
}

// The input field's text, images as U+FFFC, so that its offsets are the buffer's
static std::string getEntrySlice(GtkTextBuffer *buffer)
{
    GtkTextIter start, end;
    p_gtk_text_buffer_get_iter_at_offset(buffer, &start, 0);
    p_gtk_text_buffer_get_end_iter(buffer, &end);
    gchar      *slice = p_gtk_text_buffer_get_slice(buffer, &start, &end, TRUE);
    std::string text  = slice ? slice : "";
    g_free(slice);
    return text;
}

// The bracketed text, on one line, around the character at the offset, and where it starts
static bool findBracketed(const std::string &text, gint offset, std::string &bracketed, gint &start)
{
    if ((offset < 0) || (offset >= g_utf8_strlen(text.c_str(), -1)))
        return false;
    const char *at    = g_utf8_offset_to_pointer(text.c_str(), offset);
    const char *open  = at;
    while (*open != '[') {
        if ((open == text.c_str()) || (*open == '\n') || ((*open == ']') && (open != at)))
            return false;
        open = g_utf8_prev_char(open);
    }
    const char *close = (open == at) ? at + 1 : at;
    while (*close != ']') {
        if (!*close || (*close == '\n') || (*close == '['))
            return false;
        close++;
    }
    bracketed.assign(open, close + 1);
    start = g_utf8_pointer_to_offset(text.c_str(), open);
    return true;
}

// Whatever is at the offset now, should it still be the placeholder, takes its place
static void replacePlaceholder(GtkWidget *entry, gint start, const std::string &placeholder,
                               const std::string &replacement)
{
    GtkTextBuffer *buffer = reinterpret_cast<GtkIMHtml *>(entry)->text_buffer;
    std::string    text   = getEntrySlice(buffer);
    gint           length = g_utf8_strlen(placeholder.c_str(), -1);
    const char    *at     = g_utf8_offset_to_pointer(text.c_str(), start);
    if ((start + length > g_utf8_strlen(text.c_str(), -1)) || strncmp(at, placeholder.c_str(), placeholder.size()))
        return;
    // Taken out, with the space after it, which pasting put there
    if (replacement.empty() && (at[placeholder.size()] == ' '))
        length++;
    GtkTextIter from, to;
    p_gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
    p_gtk_text_buffer_get_iter_at_offset(buffer, &to, start + length);
    p_gtk_imhtml_delete(reinterpret_cast<GtkIMHtml *>(entry), &from, &to);
    if (!replacement.empty()) {
        p_gtk_text_buffer_get_iter_at_offset(buffer, &from, start);
        p_gtk_text_buffer_insert(buffer, &from, replacement.c_str(), -1);
    }
}

static void addPlaceholderMenu(PurpleConversation *conv, GtkWidget *entry, GtkWidget *menu, const GtkTextIter &where)
{
    std::string placeholder;
    gint        start;
    if (!g_placeholderHandler || !p_gtk_text_buffer_get_slice || !p_gtk_text_buffer_insert ||
        !findBracketed(getEntrySlice(reinterpret_cast<GtkIMHtml *>(entry)->text_buffer),
                       p_gtk_text_iter_get_offset(&where), placeholder, start))
        return;
    PastedForms forms;
    if (!g_placeholderHandler(conv, placeholder, forms))
        return;

    std::vector<MenuItem> items;
    if (!forms.other.empty()) {
        MenuItem item;
        item.label  = forms.otherLabel;
        std::string other = forms.other;
        item.action = [entry, start, placeholder, other]() { replacePlaceholder(entry, start, placeholder, other); };
        items.push_back(std::move(item));
    }
    MenuItem remove;
    remove.label  = forms.removeLabel;
    remove.action = [entry, start, placeholder]() { replacePlaceholder(entry, start, placeholder, ""); };
    items.push_back(std::move(remove));

    GtkWidget *separator = p_gtk_separator_menu_item_new();
    p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu), separator);
    addMenuItems(menu, items, 0);
    p_gtk_widget_show_all(menu);
}

static void addMessageMenu(PurpleConversation *conv, GtkWidget *menu, MessageId messageId, bool fileGiven = false);

// GTK builds the menu of a tab's text anew each time, before emitting "populate-popup" for others to
// add to it. The message is the one under the pointer, also when the menu is opened from the
// keyboard; none if the pointer is elsewhere.
static gboolean populatingPopup(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkWidget          *view = (paramCount > 1) ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    GtkWidget          *menu = view ? static_cast<GtkWidget *>(p_g_value_get_object(&params[1])) : nullptr;
    PurpleConversation *conv = menu ? findTabConversation(view) : nullptr;
    PurpleConversation *entryConv = (menu && !conv) ? findEntryConversation(view) : nullptr;
    if ((!conv || !g_messagePopupHandler) && !entryConv)
        return TRUE;
    gint x, y;
    p_gtk_widget_get_pointer(view, &x, &y);
    if ((x < 0) || (y < 0) || (x >= view->allocation.width) || (y >= view->allocation.height))
        return TRUE;
    GtkTextView *textView = reinterpret_cast<GtkTextView *>(view);
    GtkTextIter  where;
    p_gtk_text_view_window_to_buffer_coords(textView, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
    if (entryConv) {
        p_gtk_text_view_get_iter_at_location(textView, &where, x, y);
        addPlaceholderMenu(entryConv, view, menu, where);
        return TRUE;
    }
    // The character under the pointer, or the last of its line, if past it: not the place after,
    // which for the last of a message is past its end
    gint trailing;
    p_gtk_text_view_get_iter_at_position(textView, &where, &trailing, x, y);
    MessageId messageId = findMessageAt(getTextBuffer(conv), where);
    if (messageId.valid())
        addMessageMenu(conv, menu, messageId);
    return TRUE;
}

// The message's items go at the end of the menu: a placeholder waits for them
static void addMessageMenu(PurpleConversation *conv, GtkWidget *menu, MessageId messageId, bool fileGiven)
{
    OpenMenuRef openMenu = std::make_shared<OpenMenu>();
    openMenu->menu        = menu;
    openMenu->fileGiven   = fileGiven;
    // Apart from the menu's own items, if it has any
    if (reinterpret_cast<GtkMenuShell *>(menu)->children)
        openMenu->separator = p_gtk_separator_menu_item_new();
    openMenu->placeholder = p_gtk_menu_item_new_with_label("\xe2\x80\xa6");
    p_gtk_widget_set_sensitive(openMenu->placeholder, FALSE);
    for (GtkWidget *item: {openMenu->separator, openMenu->placeholder}) {
        if (!item)
            continue;
        p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu), item);
        p_gtk_widget_show(item);
    }
    openMenu->destroyHandler = p_g_signal_connect_data(menu, "destroy", G_CALLBACK(openMenuDestroyed),
                                                       new std::weak_ptr<OpenMenu>(openMenu), deleteOpenMenuRef,
                                                       GConnectFlags(0));
    purple_debug_misc(config::pluginId, "Menu opened on message %" G_GINT64_FORMAT "\n", messageId.value());
    if (!g_messagePopupHandler(conv, messageId, openMenu))
        fillOpenMenu(openMenu, {});
}

PurpleConversation *getTabConversation(GtkIMHtml *imhtml)
{
    return imhtml ? findTabConversation(imhtml) : nullptr;
}

bool addLinkMessageMenu(GtkIMHtml *imhtml, GtkWidget *menu, MessageId messageId, bool fileGiven)
{
    PurpleConversation *conv = (imhtml && menu && g_messagePopupHandler) ? findTabConversation(imhtml) : nullptr;
    if (!conv)
        return false;
    addMessageMenu(conv, menu, messageId, fileGiven);
    return true;
}

// A picture's menu, Pidgin's own with its "Save Image...", is built by the picture's event box when
// the right button is let go over it, with no signal to add to it: the message under the pointer is
// noted then, for its items to go into the menu shown next, before the main loop is idle again
struct PictureMenu {
    PurpleConversation *conv = nullptr;
    MessageId           messageId;
};
static PictureMenu g_pictureMenu;
static guint       g_pictureMenuSource  = 0;
static guint       g_pictureEventSignal = 0;
static gulong      g_pictureEventHook   = 0;
static guint       g_menuShowSignal     = 0;
static gulong      g_menuShowHook       = 0;

static gboolean forgetPictureMenu(gpointer)
{
    g_pictureMenu       = PictureMenu();
    g_pictureMenuSource = 0;
    return FALSE;
}

static gboolean pictureClicked(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkWidget *box   = (paramCount > 1) ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    GdkEvent  *event = box ? static_cast<GdkEvent *>(p_g_value_get_boxed(&params[1])) : nullptr;
    if (!event || (event->type != GDK_BUTTON_RELEASE) || (event->button.button != 3) || !g_messagePopupHandler ||
        !p_g_object_get_data(G_OBJECT(box), IMAGE_SAVE_KEY))
        return TRUE;
    GtkWidget          *view = p_gtk_widget_get_parent(box);
    PurpleConversation *conv = view ? findTabConversation(view) : nullptr;
    if (!conv)
        return TRUE;
    gint x, y;
    p_gtk_widget_get_pointer(view, &x, &y);
    GtkTextView *textView = reinterpret_cast<GtkTextView *>(view);
    GtkTextIter  where;
    gint         trailing;
    p_gtk_text_view_window_to_buffer_coords(textView, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
    p_gtk_text_view_get_iter_at_position(textView, &where, &trailing, x, y);
    MessageId messageId = findMessageAt(getTextBuffer(conv), where);
    if (!messageId.valid())
        return TRUE;
    if (!g_pictureMenuSource)
        g_pictureMenuSource = g_idle_add(forgetPictureMenu, nullptr);
    g_pictureMenu.conv      = conv;
    g_pictureMenu.messageId = messageId;
    return TRUE;
}

static gboolean menuShown(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    if (!g_pictureMenu.conv)
        return TRUE;
    GtkWidget *menu = (paramCount > 0) ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    if (!menu || !p_g_type_check_instance_is_a(reinterpret_cast<GTypeInstance *>(menu), p_gtk_menu_get_type()))
        return TRUE;
    PictureMenu picture = g_pictureMenu;
    g_pictureMenu.conv  = nullptr;
    if (!g_list_find(purple_get_conversations(), picture.conv))
        return TRUE;
    // Opened as the right button is let go, the menu still takes that button for the one that
    // picks, and so would its submenus, which hand a click of any other on to the menu, closing it
    // with nothing picked: a menu opened as the button is pressed forgets it once it is let go
    reinterpret_cast<GtkMenuShell *>(menu)->button = 0;
    addMessageMenu(picture.conv, menu, picture.messageId);
    return TRUE;
}

// A reaction under the pointer, on its message's line, lists who gave it, in GTK's tooltip; Pidgin's
// own, for links, is a window of its own
static ReactionTipHandler g_reactionTipHandler = nullptr;
static const char         REACTION_TIPS_KEY[]  = "tdlib-purple-reaction-tips";

void setReactionTipHandler(ReactionTipHandler handler)
{
    g_reactionTipHandler = handler;
}

static gboolean queryingTooltip(GtkWidget *view, gint x, gint y, gboolean keyboard, GtkTooltip *tooltip, gpointer)
{
    PurpleConversation *conv = (g_reactionTipHandler && !keyboard) ? findTabConversation(view) : nullptr;
    if (!conv)
        return FALSE;
    GtkTextView *textView = reinterpret_cast<GtkTextView *>(view);
    GtkTextIter  where;
    p_gtk_text_view_window_to_buffer_coords(textView, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
    if (!getCharAt(textView, x, y, where))
        return FALSE;
    std::string key = getReactionKey(where);
    MessageId   messageId = key.empty() ? MessageId() : findMessageAt(getTextBuffer(conv), where);
    std::string tip;
    if (!messageId.valid() || !g_reactionTipHandler(conv, messageId, key, tip) || tip.empty())
        return FALSE;
    p_gtk_tooltip_set_text(tooltip, tip.c_str());
    return TRUE;
}

static void connectReactionTips(GtkIMHtml *imhtml)
{
    if (p_g_object_get_data(G_OBJECT(imhtml), REACTION_TIPS_KEY) || !g_reactionTipHandler ||
        !LOOK_UP(gtk_text_iter_get_tags) || !LOOK_UP(gtk_text_view_get_iter_location) ||
        !LOOK_UP(gtk_text_view_get_iter_at_position) ||
        !LOOK_UP(gtk_widget_set_has_tooltip) || !LOOK_UP(gtk_tooltip_set_text) ||
        !LOOK_UP(gtk_widget_get_pointer) || !LOOK_UP(gtk_text_view_window_to_buffer_coords) ||
        !LOOK_UP(gtk_text_view_get_iter_at_location) || !LOOK_UP(gtk_text_iter_get_marks) ||
        !LOOK_UP(gtk_text_mark_get_name))
        return;
    gulong handler = p_g_signal_connect_data(imhtml, "query-tooltip", G_CALLBACK(queryingTooltip), nullptr, nullptr,
                                             GConnectFlags(0));
    p_g_object_set_data_full(G_OBJECT(imhtml), REACTION_TIPS_KEY, GSIZE_TO_POINTER(handler), nullptr);
    p_gtk_widget_set_has_tooltip(reinterpret_cast<GtkWidget *>(imhtml), TRUE);
}

static void disconnectReactionTips()
{
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        GtkIMHtml *imhtml  = getIMHtml(static_cast<PurpleConversation *>(item->data));
        gulong     handler = imhtml ? GPOINTER_TO_SIZE(p_g_object_get_data(G_OBJECT(imhtml), REACTION_TIPS_KEY)) : 0;
        if (handler) {
            p_g_signal_handler_disconnect(imhtml, handler);
            p_g_object_set_data_full(G_OBJECT(imhtml), REACTION_TIPS_KEY, nullptr, nullptr);
            p_gtk_widget_set_has_tooltip(reinterpret_cast<GtkWidget *>(imhtml), FALSE);
        }
    }
}

void refreshReactionTip(PurpleConversation *conv)
{
    GtkIMHtml *imhtml = getIMHtml(conv);
    if (imhtml && p_g_object_get_data(G_OBJECT(imhtml), REACTION_TIPS_KEY) && LOOK_UP(gtk_widget_trigger_tooltip_query))
        p_gtk_widget_trigger_tooltip_query(reinterpret_cast<GtkWidget *>(imhtml));
}

// The message that starts where the iterator is, if one does
static MessageId findMessageStart(const GtkTextIter &iter)
{
    GSList   *marks = p_gtk_text_iter_get_marks(&iter);
    MessageId messageId;
    for (GSList *item = marks; item && !messageId.valid(); item = item->next) {
        const char *name = p_gtk_text_mark_get_name(static_cast<GtkTextMark *>(item->data));
        if (name && g_str_has_prefix(name, START_MARK_PREFIX))
            messageId = MessageId::fromString(name + strlen(START_MARK_PREFIX));
    }
    g_slist_free(marks);
    return messageId;
}

MessageId findFirstMessage(PurpleConversation *conv)
{
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    iter;
    if (!buffer)
        return MessageId();
    p_gtk_text_buffer_get_iter_at_offset(buffer, &iter, 0);
    do {
        MessageId messageId = findMessageStart(iter);
        if (messageId.valid())
            return messageId;
    } while (p_gtk_text_iter_forward_char(&iter));
    return MessageId();
}

// Whether the message is in the tab still: trimmed away by Pidgin, its marks stay, together
static bool isMessageShown(GtkTextBuffer *buffer, MessageId messageId)
{
    GtkTextMark *start = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::Start).c_str());
    GtkTextMark *end   = p_gtk_text_buffer_get_mark(buffer, getMarkName(messageId, MarkKind::End).c_str());
    GtkTextIter  startIter, endIter;
    if (!start || !end)
        return false;
    p_gtk_text_buffer_get_iter_at_mark(buffer, &startIter, start);
    p_gtk_text_buffer_get_iter_at_mark(buffer, &endIter, end);
    return p_gtk_text_iter_compare(&startIter, &endIter) < 0;
}

MessageId findMessageAfter(PurpleConversation *conv, MessageId messageId)
{
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    iter;
    if (!buffer || !isMessageShown(buffer, messageId))
        return MessageId();
    p_gtk_text_buffer_get_iter_at_mark(buffer, &iter,
                                       p_gtk_text_buffer_get_mark(buffer,
                                                                  getMarkName(messageId, MarkKind::End).c_str()));
    while (p_gtk_text_iter_forward_char(&iter)) {
        MessageId next = findMessageStart(iter);
        if (next.valid())
            return isMessageShown(buffer, next) ? next : MessageId();
    }
    return MessageId();
}

MessageId findLastMessage(PurpleConversation *conv)
{
    GtkTextBuffer *buffer = getTextBuffer(conv);
    GtkTextIter    iter;
    if (!buffer)
        return MessageId();
    p_gtk_text_buffer_get_end_iter(buffer, &iter);
    do {
        MessageId messageId = findMessageStart(iter);
        if (messageId.valid())
            return messageId;
    } while (p_gtk_text_iter_backward_char(&iter));
    return MessageId();
}

// Whether a tab's lines can be counted and its messages taken out by the line, as GTK is found
static bool g_tabWindows = false;

static void findTabWindowFunctions()
{
    g_tabWindows = LOOK_UP(gtk_text_buffer_get_line_count) && LOOK_UP(gtk_text_buffer_get_iter_at_line) &&
                   LOOK_UP(gtk_text_iter_get_marks) && LOOK_UP(gtk_text_mark_get_name) &&
                   LOOK_UP(gtk_text_mark_get_deleted) && LOOK_UP(gtk_text_iter_get_line) &&
                   LOOK_UP(gtk_text_view_get_visible_rect) &&
                   LOOK_UP(gtk_text_view_get_iter_at_location);
    if (!g_tabWindows)
        purple_debug_warning(config::pluginId, "Cannot keep tabs to part of a chat: some of GTK is not found\n");
}

unsigned countTabLines(PurpleConversation *conv)
{
    GtkTextBuffer *buffer = g_tabWindows ? getTextBuffer(conv) : nullptr;
    return buffer ? p_gtk_text_buffer_get_line_count(buffer) : 0;
}

static const char TAB_END_KEY[]         = "tdlib-tab-end";
static const char HIDDEN_MESSAGES_KEY[] = "tdlib-hidden-messages";

static std::set<int64_t> *getHiddenMessages(GtkIMHtml *imhtml)
{
    return static_cast<std::set<int64_t> *>(p_g_object_get_data(reinterpret_cast<GObject *>(imhtml),
                                                                HIDDEN_MESSAGES_KEY));
}

static void deleteHiddenMessages(gpointer data)
{
    delete static_cast<std::set<int64_t> *>(data);
}

static void deleteTabEnd(gpointer data)
{
    delete static_cast<MessageId *>(data);
}

void setTabEnd(PurpleConversation *conv, MessageId messageId)
{
    GtkIMHtml *imhtml = g_tabWindows ? getIMHtml(conv) : nullptr;
    if (!imhtml)
        return;
    p_g_object_set_data_full(reinterpret_cast<GObject *>(imhtml), TAB_END_KEY,
                             messageId.valid() ? new MessageId(messageId) : nullptr, deleteTabEnd);
    // Those up to its end are to be shown now
    std::set<int64_t> *hidden = getHiddenMessages(imhtml);
    if (hidden && messageId.valid())
        hidden->erase(hidden->begin(), hidden->upper_bound(messageId.value()));
    else if (hidden)
        hidden->clear();
}

MessageId getTabEnd(PurpleConversation *conv)
{
    GtkIMHtml *imhtml = g_tabWindows ? getIMHtml(conv) : nullptr;
    MessageId *end    = imhtml ? static_cast<MessageId *>(p_g_object_get_data(reinterpret_cast<GObject *>(imhtml),
                                                                              TAB_END_KEY))
                               : nullptr;
    return end ? *end : MessageId();
}

void hideMessage(PurpleConversation *conv, MessageId messageId)
{
    GtkIMHtml *imhtml = g_tabWindows ? getIMHtml(conv) : nullptr;
    if (!imhtml)
        return;
    std::set<int64_t> *hidden = getHiddenMessages(imhtml);
    if (!hidden) {
        hidden = new std::set<int64_t>;
        p_g_object_set_data_full(reinterpret_cast<GObject *>(imhtml), HIDDEN_MESSAGES_KEY, hidden,
                                 deleteHiddenMessages);
    }
    hidden->insert(messageId.value());
}

static bool isMessageHidden(PurpleConversation *conv, MessageId messageId)
{
    GtkIMHtml         *imhtml = g_tabWindows ? getIMHtml(conv) : nullptr;
    std::set<int64_t> *hidden = imhtml ? getHiddenMessages(imhtml) : nullptr;
    return hidden && hidden->count(messageId.value());
}

// The line from which on, in either direction, the first message starts, if any does there but the
// first line
static gint findStartLine(GtkTextBuffer *buffer, gint line, bool forward)
{
    gint        lines = p_gtk_text_buffer_get_line_count(buffer);
    GtkTextIter iter;
    for (; (line > 0) && (line < lines); line += (forward ? 1 : -1)) {
        p_gtk_text_buffer_get_iter_at_line(buffer, &iter, line);
        if (findMessageStart(iter).valid())
            return line;
    }
    return -1;
}

bool trimTab(PurpleConversation *conv, unsigned maxLines, bool fromTop, MessageId keep)
{
    GtkIMHtml     *imhtml = g_tabWindows ? getIMHtml(conv) : nullptr;
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    gint           lines  = buffer ? p_gtk_text_buffer_get_line_count(buffer) : 0;
    if (!maxLines || (lines <= gint(maxLines)))
        return false;
    GtkTextMark *keepMark = keep.valid() ? p_gtk_text_buffer_get_mark(buffer, getMarkName(keep, true).c_str())
                                         : nullptr;
    // From the top, the first message to stay; from the end, the first to go
    gint line = findStartLine(buffer, fromTop ? (lines - maxLines) : maxLines, fromTop);
    if (keepMark) {
        GtkTextIter keepIter, cutIter;
        p_gtk_text_buffer_get_iter_at_mark(buffer, &keepIter, keepMark);
        if (line >= 0)
            p_gtk_text_buffer_get_iter_at_line(buffer, &cutIter, line);
        if (fromTop && ((line < 0) || (p_gtk_text_iter_compare(&cutIter, &keepIter) > 0)))
            line = p_gtk_text_iter_get_line(&keepIter);
        else if (!fromTop && ((line < 0) || (p_gtk_text_iter_compare(&cutIter, &keepIter) <= 0)))
            line = findStartLine(buffer, p_gtk_text_iter_get_line(&keepIter) + 1, true);
    }
    if (line <= 0)
        return false;

    // Taken out with the line break before the first message to go, or before the first to stay;
    // with the marks of those that go, which would otherwise stay, where the text was
    GtkTextIter from, to, cut;
    p_gtk_text_buffer_get_iter_at_line(buffer, &cut, line);
    if (fromTop) {
        p_gtk_text_buffer_get_iter_at_offset(buffer, &from, 0);
        to = cut;
    } else {
        from = cut;
        p_gtk_text_iter_backward_char(&from);
        p_gtk_text_buffer_get_end_iter(buffer, &to);
    }
    std::vector<GtkTextMark *> gone;
    GtkTextIter                iter = fromTop ? from : cut;
    for (bool more = true; more; more = p_gtk_text_iter_forward_char(&iter)) {
        if (fromTop && (p_gtk_text_iter_compare(&iter, &to) >= 0))
            break;
        GSList *marks = p_gtk_text_iter_get_marks(&iter);
        for (GSList *item = marks; item; item = item->next) {
            const char *name = p_gtk_text_mark_get_name(static_cast<GtkTextMark *>(item->data));
            if (name && g_str_has_prefix(name, MESSAGE_MARK_PREFIX))
                gone.push_back(static_cast<GtkTextMark *>(item->data));
        }
        g_slist_free(marks);
    }
    purple_debug_misc(config::pluginId, "Taking lines %s %d out of %d in the tab\n", fromTop ? "up to" : "from",
                      line, lines);
    p_gtk_imhtml_delete(imhtml, &from, &to);
    for (GtkTextMark *mark: gone)
        p_gtk_text_buffer_delete_mark(buffer, mark);
    return true;
}


namespace {
struct ScrollToMessage {
    PurpleConversation *conv;
    MessageId           messageId;
    unsigned            checks = 0;
};
}

// GtkIMHtml scrolls to the end when idle, smoothly for up to 0.4 s after the latest message written
// while it was there, and so for as long as messages keep coming: only once it is done is the
// tab scrolled back, after which what is written at the end leaves it be
enum { SCROLLED_CHECK_MS = 100, SCROLLED_CHECKS = 50 };

static TopReachedHandler g_topReachedHandler = nullptr;

static void checkBottomReached(PurpleConversation *conv, GtkIMHtml *imhtml);

static gboolean scrollToMessageTop(gpointer data)
{
    ScrollToMessage *scroll = static_cast<ScrollToMessage *>(data);
    GtkIMHtml       *imhtml = g_list_find(purple_get_conversations(), scroll->conv) ? getIMHtml(scroll->conv) : nullptr;
    if (imhtml && imhtml->scroll_src && (++scroll->checks < SCROLLED_CHECKS))
        return G_SOURCE_CONTINUE;
    GtkTextBuffer *buffer = imhtml ? imhtml->text_buffer : nullptr;
    GtkTextMark   *start  = buffer ? p_gtk_text_buffer_get_mark(buffer, getMarkName(scroll->messageId, true).c_str())
                                   : nullptr;
    if (start)
        p_gtk_text_view_scroll_to_mark(reinterpret_cast<GtkTextView *>(imhtml), start, 0.0, TRUE, 0.0, 0.0);
    // Still no more than fits, where the tab is shown, its size known: earlier messages fill it
    GtkAdjustment *adjustment = imhtml ? reinterpret_cast<GtkTextView *>(imhtml)->vadjustment : nullptr;
    if (adjustment && (reinterpret_cast<GtkObject *>(imhtml)->flags & GTK_MAPPED) &&
        (adjustment->upper <= adjustment->page_size) && g_topReachedHandler)
        g_topReachedHandler(scroll->conv, (adjustment->upper > 0) ? adjustment->page_size / adjustment->upper : 0);
    // Or what follows does, where the tab stops short of the newest; but not taken to be at its
    // end otherwise, which it still seems to be until GTK has scrolled it
    if (adjustment && (adjustment->upper <= adjustment->page_size))
        checkBottomReached(scroll->conv, imhtml);
    delete scroll;
    return G_SOURCE_REMOVE;
}

void scrollToMessageLater(PurpleConversation *conv, MessageId messageId)
{
    if (getIMHtml(conv))
        g_timeout_add_full(G_PRIORITY_LOW, SCROLLED_CHECK_MS, scrollToMessageTop,
                           new ScrollToMessage{conv, messageId}, nullptr);
}

namespace {
struct KeptView {
    PurpleConversation *conv;
    GtkTextMark        *mark;
    unsigned            checks = 0;
};
}

static BottomReachedHandler g_bottomReachedHandler = nullptr;

void setBottomReachedHandler(BottomReachedHandler handler)
{
    g_bottomReachedHandler = handler;
}

// An item of ours in the menu of a window's tabs, which Pidgin puts its own in, and the separator
// before it; shown for a tab that stops short of its chat's newest message
static LatestAskedHandler g_latestAskedHandler = nullptr;
static std::string        g_latestLabel;
static guint              g_pressSignal = 0;
static gulong             g_pressHook   = 0;
static const char         LATEST_ITEM_KEY[]      = "tdlib-purple-latest-item";
static const char         LATEST_SEPARATOR_KEY[] = "tdlib-purple-latest-separator";

void setLatestAskedHandler(LatestAskedHandler handler, const std::string &label)
{
    g_latestAskedHandler = handler;
    g_latestLabel        = label;
}

// The tab Pidgin says the menu was opened on, as it does for its own items
static void latestChosen(GtkWidget *, gpointer menu)
{
    PidginConversation *gtkconv = static_cast<PidginConversation *>(
        p_g_object_get_data(static_cast<GObject *>(menu), "clicked_tab"));
    if (gtkconv && gtkconv->active_conv && g_latestAskedHandler &&
        g_list_find(purple_get_conversations(), gtkconv->active_conv))
        g_latestAskedHandler(gtkconv->active_conv);
}

// The right button pressed on a window's tabs: ahead of Pidgin and of GTK, which opens the menu
static gboolean tabsPressed(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkWidget      *widget = (paramCount > 1) ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    GdkEventButton *event  = widget ? static_cast<GdkEventButton *>(p_g_value_get_boxed(&params[1])) : nullptr;
    if (!event || (event->type != GDK_BUTTON_PRESS) || (event->button != 3) || !g_latestAskedHandler)
        return TRUE;
    for (GList *item = p_pidgin_conv_windows_get_list(); item; item = item->next) {
        PidginWindow *win = static_cast<PidginWindow *>(item->data);
        if (win->notebook != widget)
            continue;
        GtkWidget *menu = reinterpret_cast<GtkNotebook *>(win->notebook)->menu;
        if (!menu)
            break;
        PidginConversation *gtkconv = p_pidgin_conv_window_get_gtkconv_at_index(
            win, p_pidgin_conv_get_tab_at_xy(win, event->x_root, event->y_root, nullptr));
        PurpleConversation *conv    = gtkconv ? gtkconv->active_conv : nullptr;
        bool show = conv && !strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)),
                                    config::pluginId) && getTabEnd(conv).valid();
        GtkWidget *latest    = static_cast<GtkWidget *>(p_g_object_get_data(G_OBJECT(menu), LATEST_ITEM_KEY));
        GtkWidget *separator = static_cast<GtkWidget *>(p_g_object_get_data(G_OBJECT(menu), LATEST_SEPARATOR_KEY));
        if (!latest && show) {
            separator = p_gtk_separator_menu_item_new();
            p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu), separator);
            latest = p_gtk_menu_item_new_with_label(g_latestLabel.c_str());
            p_gtk_menu_shell_append(reinterpret_cast<GtkMenuShell *>(menu), latest);
            p_g_signal_connect_data(latest, "activate", G_CALLBACK(latestChosen), menu, nullptr, GConnectFlags(0));
            p_g_object_set_data(G_OBJECT(menu), LATEST_ITEM_KEY, latest);
            p_g_object_set_data(G_OBJECT(menu), LATEST_SEPARATOR_KEY, separator);
        }
        for (GtkWidget *menuItem: {separator, latest})
            if (menuItem && show)
                p_gtk_widget_show(menuItem);
            else if (menuItem)
                p_gtk_widget_hide(menuItem);
        break;
    }
    return TRUE;
}

static void connectTabMenu()
{
    if (!LOOK_UP(pidgin_conv_windows_get_list) || !LOOK_UP(pidgin_conv_get_tab_at_xy) ||
        !LOOK_UP(pidgin_conv_window_get_gtkconv_at_index) || !LOOK_UP(gtk_separator_menu_item_new) ||
        !LOOK_UP(gtk_widget_show) || !LOOK_UP(gtk_widget_hide) || !LOOK_UP(g_object_set_data) ||
        !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook) || !LOOK_UP(g_value_get_object) ||
        !LOOK_UP(gtk_widget_get_type) || !LOOK_UP(g_value_get_boxed))
    {
        purple_debug_warning(config::pluginId, "Cannot add to the menu of tabs: some of GTK is not found\n");
        return;
    }
    g_pressSignal = p_g_signal_lookup("button-press-event", p_gtk_widget_get_type());
    if (g_pressSignal)
        g_pressHook = p_g_signal_add_emission_hook(g_pressSignal, 0, tabsPressed, nullptr, nullptr);
}

static void disconnectTabMenu()
{
    if (g_pressHook)
        p_g_signal_remove_emission_hook(g_pressSignal, g_pressHook);
    g_pressHook = 0;
}

// Whether the tab's text is scrolled to its end, or has no more than fits
static bool isAtBottom(GtkIMHtml *imhtml)
{
    GtkAdjustment *adjustment = reinterpret_cast<GtkTextView *>(imhtml)->vadjustment;
    return adjustment && (adjustment->value + adjustment->page_size >= adjustment->upper);
}

// While what was in view is still to be put back, the tab is at the end only as written to
static const char KEEPING_VIEW_KEY[] = "tdlib-purple-keeping-view";

// Still at the end, where the tab stops short of the chat's newest, it goes on to later messages
static void checkBottomReached(PurpleConversation *conv, GtkIMHtml *imhtml)
{
    if (g_bottomReachedHandler && (reinterpret_cast<GtkObject *>(imhtml)->flags & GTK_MAPPED) &&
        !purple_conversation_get_data(conv, KEEPING_VIEW_KEY) && isAtBottom(imhtml) && getTabEnd(conv).valid())
        g_bottomReachedHandler(conv);
}

// Scrolled back so that what was at the top is there again, once Pidgin is done scrolling to
// the end; with no more than fits, it goes on
static gboolean keptViewBack(gpointer data)
{
    KeptView  *kept   = static_cast<KeptView *>(data);
    GtkIMHtml *imhtml = g_list_find(purple_get_conversations(), kept->conv) ? getIMHtml(kept->conv) : nullptr;
    if (imhtml && imhtml->scroll_src && (++kept->checks < SCROLLED_CHECKS))
        return G_SOURCE_CONTINUE;
    if (imhtml) {
        purple_conversation_set_data(kept->conv, KEEPING_VIEW_KEY, nullptr);
        if (!p_gtk_text_mark_get_deleted(kept->mark)) {
            p_gtk_text_view_scroll_to_mark(reinterpret_cast<GtkTextView *>(imhtml), kept->mark, 0.0, TRUE, 0.0, 0.0);
            p_gtk_text_buffer_delete_mark(imhtml->text_buffer, kept->mark);
        }
        // Still at the end only if it has no more than fits: otherwise it seems to be until GTK
        // has scrolled it
        GtkAdjustment *adjustment = reinterpret_cast<GtkTextView *>(imhtml)->vadjustment;
        if (adjustment && (adjustment->upper <= adjustment->page_size))
            checkBottomReached(kept->conv, imhtml);
    }
    delete kept;
    return G_SOURCE_REMOVE;
}

void keepViewLater(PurpleConversation *conv)
{
    GtkIMHtml *imhtml = g_tabWindows ? getIMHtml(conv) : nullptr;
    if (!imhtml)
        return;
    GtkTextView *view = reinterpret_cast<GtkTextView *>(imhtml);
    GdkRectangle rect;
    GtkTextIter  top;
    p_gtk_text_view_get_visible_rect(view, &rect);
    p_gtk_text_view_get_iter_at_location(view, &top, rect.x, rect.y);
    GtkTextMark *mark = p_gtk_text_buffer_create_mark(imhtml->text_buffer, nullptr, &top, TRUE);
    purple_conversation_set_data(conv, KEEPING_VIEW_KEY, conv);
    g_timeout_add_full(G_PRIORITY_LOW, SCROLLED_CHECK_MS, keptViewBack, new KeptView{conv, mark}, nullptr);
}

// A tab's text scrolled to its top: not again until it is scrolled down from there, nor while it
// fits, emptied to be filled anew, which starts at the top too
static guint             g_scrollSignal = 0;
static gulong            g_scrollHook   = 0;
static guint             g_wheelSignal  = 0;
static gulong            g_wheelHook    = 0;
static const char        AT_TOP_KEY[]   = "tdlib-purple-at-top";
static const char        AT_BOTTOM_KEY[] = "tdlib-purple-at-bottom";

void setTopReachedHandler(TopReachedHandler handler)
{
    g_topReachedHandler = handler;
}

static gboolean scrolled(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkAdjustment *adjustment = paramCount ? static_cast<GtkAdjustment *>(p_g_value_get_object(&params[0])) : nullptr;
    if (!adjustment)
        return TRUE;
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv   = static_cast<PurpleConversation *>(item->data);
        GtkIMHtml          *imhtml = strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)),
                                            config::pluginId) ? nullptr : getIMHtml(conv);
        if (!imhtml || (reinterpret_cast<GtkTextView *>(imhtml)->vadjustment != adjustment))
            continue;
        scheduleViewCheck(conv);
        // Not while Pidgin scrolls to the end by itself, as it is written to
        bool atBottom  = isAtBottom(imhtml);
        bool wasBottom = purple_conversation_get_data(conv, AT_BOTTOM_KEY) != nullptr;
        purple_conversation_set_data(conv, AT_BOTTOM_KEY, atBottom ? conv : nullptr);
        if (atBottom && !wasBottom && !imhtml->scroll_src)
            checkBottomReached(conv, imhtml);
        if (!g_topReachedHandler)
            break;
        bool atTop  = adjustment->value <= adjustment->lower;
        bool wasTop = purple_conversation_get_data(conv, AT_TOP_KEY) != nullptr;
        if (atTop && (adjustment->upper <= adjustment->page_size))
            break;
        purple_conversation_set_data(conv, AT_TOP_KEY, atTop ? conv : nullptr);
        if (atTop && !wasTop)
            g_topReachedHandler(conv, 0);
        break;
    }
    return TRUE;
}

// The wheel turned up at the top, or down at the end, or in a tab with no more than fits, where
// nothing scrolls
static gboolean wheelTurned(GSignalInvocationHint *, guint paramCount, const GValue *params, gpointer)
{
    GtkWidget      *widget = (paramCount > 1) ? static_cast<GtkWidget *>(p_g_value_get_object(&params[0])) : nullptr;
    GdkEventScroll *event  = widget ? static_cast<GdkEventScroll *>(p_g_value_get_boxed(&params[1])) : nullptr;
    if (event && (event->direction == GDK_SCROLL_DOWN)) {
        for (GList *item = purple_get_conversations(); item; item = item->next) {
            PurpleConversation *conv   = static_cast<PurpleConversation *>(item->data);
            GtkIMHtml          *imhtml = strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)),
                                                config::pluginId) ? nullptr : getIMHtml(conv);
            if (imhtml && (reinterpret_cast<GtkWidget *>(imhtml) == widget)) {
                checkBottomReached(conv, imhtml);
                break;
            }
        }
        return TRUE;
    }
    if (!event || (event->direction != GDK_SCROLL_UP) || !g_topReachedHandler)
        return TRUE;
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv   = static_cast<PurpleConversation *>(item->data);
        GtkIMHtml          *imhtml = strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)),
                                            config::pluginId) ? nullptr : getIMHtml(conv);
        if (!imhtml || (reinterpret_cast<GtkWidget *>(imhtml) != widget))
            continue;
        GtkAdjustment *adjustment = reinterpret_cast<GtkTextView *>(imhtml)->vadjustment;
        if (adjustment && (adjustment->value <= adjustment->lower))
            g_topReachedHandler(conv, 0);
        break;
    }
    return TRUE;
}

static void connectScroll()
{
    if (!LOOK_UP(gtk_adjustment_get_type) || !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook) || !LOOK_UP(g_value_get_object) ||
        !LOOK_UP(gtk_widget_get_type) || !LOOK_UP(g_value_get_boxed))
    {
        purple_debug_warning(config::pluginId, "Cannot follow the scrolling of tabs: some of GTK is not found\n");
        return;
    }
    g_scrollSignal = p_g_signal_lookup("value-changed", p_gtk_adjustment_get_type());
    if (g_scrollSignal)
        g_scrollHook = p_g_signal_add_emission_hook(g_scrollSignal, 0, scrolled, nullptr, nullptr);
    g_wheelSignal = p_g_signal_lookup("scroll-event", p_gtk_widget_get_type());
    if (g_wheelSignal)
        g_wheelHook = p_g_signal_add_emission_hook(g_wheelSignal, 0, wheelTurned, nullptr, nullptr);
}

static void disconnectScroll()
{
    if (g_scrollHook)
        p_g_signal_remove_emission_hook(g_scrollSignal, g_scrollHook);
    if (g_wheelHook)
        p_g_signal_remove_emission_hook(g_wheelSignal, g_wheelHook);
    g_scrollHook = 0;
    g_wheelHook  = 0;
}

// The tabs shown, as last told, worked out anew whenever that may have changed: until it can be,
// every tab is taken to be shown
static bool                           g_tracksShown       = false;
static std::set<PurpleConversation *> g_shownConversations;
static ShownHandler                   g_shownHandler      = nullptr;
static ViewedHandler                  g_viewedHandler     = nullptr;
static guint                          g_windowStateSignal = 0;
static gulong                         g_windowStateHook   = 0;
static guint                          g_focusInSignal     = 0;
static gulong                         g_focusInHook       = 0;
static guint                          g_focusOutSignal    = 0;
static gulong                         g_focusOutHook      = 0;
static guint                          g_focusSource       = 0;
static guint                          g_shownSource       = 0;
static const char                     VIEW_SOURCE_KEY[]   = "tdlib-purple-view-source";

// Not on screen until it has stood still this long
enum { VIEW_SETTLE_MS = 1000 };

bool clearUnseen(PurpleConversation *conv)
{
    // As Pidgin's gtkconv_set_unseen() does, which it does not export: the tab's state is what
    // the tab and the tray's menu go by, the conversation's data what the counts do
    PidginConversation *gtkconv = getIMHtml(conv) ? PIDGIN_CONVERSATION(conv) : nullptr;
    if (gtkconv && (gtkconv->active_conv != conv))
        return false;
    bool unseen = gtkconv ? (gtkconv->unseen_state != PIDGIN_UNSEEN_NONE)
                          : (purple_conversation_get_data(conv, "unseen-state") != nullptr);
    if (!unseen)
        return false;
    if (gtkconv) {
        gtkconv->unseen_count = 0;
        gtkconv->unseen_state = PIDGIN_UNSEEN_NONE;
    }
    purple_conversation_set_data(conv, "unseen-count", GINT_TO_POINTER(0));
    purple_conversation_set_data(conv, "unseen-state", GINT_TO_POINTER(PIDGIN_UNSEEN_NONE));
    purple_conversation_update(conv, PURPLE_CONV_UPDATE_UNSEEN);
    return true;
}

bool tracksShownConversations()
{
    return g_tracksShown;
}

bool isConversationShown(PurpleConversation *conv)
{
    return !g_tracksShown || g_shownConversations.count(conv);
}

void setShownHandler(ShownHandler handler)
{
    g_shownHandler = handler;
}

bool viewsMessages(PurpleConversation *conv)
{
    return g_tracksShown && getIMHtml(conv);
}

void setViewedHandler(ViewedHandler handler)
{
    g_viewedHandler = handler;
}

static bool isOurConversation(PurpleConversation *conv)
{
    return !strcmp(purple_account_get_protocol_id(purple_conversation_get_account(conv)), config::pluginId);
}

static bool findShown(PurpleConversation *conv)
{
    if (!getIMHtml(conv))
        return false;
    PidginWindow *win    = PIDGIN_CONVERSATION(conv)->win;
    GtkWidget    *window = win ? win->window : nullptr;
    return window && (p_pidgin_conv_window_get_active_conversation(win) == conv) &&
           (reinterpret_cast<GtkObject *>(window)->flags & GTK_MAPPED) && window->window &&
           !(p_gdk_window_get_state(window->window) & GDK_WINDOW_STATE_ICONIFIED);
}

// Screen lockers say whether they are on over the session's D-Bus, each desktop's under a name of its
// own, all alike: asked once, and followed after. One not there never answers.
struct ScreenLocker {
    const char *name; // its interface too
    const char *path;
};
static const ScreenLocker SCREEN_LOCKERS[] = {
    {"org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver"}, // KDE's, xfce4-screensaver, light-locker
    {"org.gnome.ScreenSaver",       "/org/gnome/ScreenSaver"},
    {"org.mate.ScreenSaver",        "/org/mate/ScreenSaver"},
    {"org.cinnamon.ScreenSaver",    "/org/cinnamon/ScreenSaver"},
    {"org.xfce.ScreenSaver",        "/org/xfce/ScreenSaver"},
};
enum { SCREEN_LOCKER_COUNT = G_N_ELEMENTS(SCREEN_LOCKERS) };
static GDBusConnection *g_sessionBus                              = nullptr;
static GCancellable    *g_busCancellable                          = nullptr;
static guint            g_lockerSubscriptions[SCREEN_LOCKER_COUNT] = {};
static bool             g_lockerActive[SCREEN_LOCKER_COUNT]        = {};

static void setLockerActive(size_t index, GVariant *value)
{
    if (!value || !g_variant_is_of_type(value, G_VARIANT_TYPE("(b)")))
        return;
    gboolean active = FALSE;
    g_variant_get(value, "(b)", &active);
    if (active != g_lockerActive[index])
        purple_debug_misc(config::pluginId, "%s is %s\n", SCREEN_LOCKERS[index].name, active ? "on" : "off");
    g_lockerActive[index] = active;
}

static void lockerChanged(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *,
                          GVariant *parameters, gpointer data)
{
    setLockerActive(GPOINTER_TO_SIZE(data), parameters);
}

static void lockerAnswered(GObject *source, GAsyncResult *result, gpointer data)
{
    GError   *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (!reply) {
        g_error_free(error);
        return;
    }
    // Gone meanwhile
    if (g_sessionBus) {
        purple_debug_misc(config::pluginId, "Screen locking is told by %s\n", SCREEN_LOCKERS[GPOINTER_TO_SIZE(data)].name);
        setLockerActive(GPOINTER_TO_SIZE(data), reply);
    }
    g_variant_unref(reply);
}

static void sessionBusFound(GObject *, GAsyncResult *result, gpointer)
{
    GError          *error = nullptr;
    GDBusConnection *bus   = g_bus_get_finish(result, &error);
    if (!bus) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            purple_debug_misc(config::pluginId, "No session bus to ask screen lockers on: %s\n", error->message);
        g_error_free(error);
        return;
    }
    g_sessionBus = bus;
    for (size_t i = 0; i < SCREEN_LOCKER_COUNT; i++) {
        // On any path: KDE's says it on two
        g_lockerSubscriptions[i] = g_dbus_connection_signal_subscribe(bus, SCREEN_LOCKERS[i].name,
            SCREEN_LOCKERS[i].name, "ActiveChanged", nullptr, nullptr, G_DBUS_SIGNAL_FLAGS_NONE, lockerChanged,
            GSIZE_TO_POINTER(i), nullptr);
        g_dbus_connection_call(bus, SCREEN_LOCKERS[i].name, SCREEN_LOCKERS[i].path, SCREEN_LOCKERS[i].name,
                               "GetActive", nullptr, G_VARIANT_TYPE("(b)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, -1,
                               g_busCancellable, lockerAnswered, GSIZE_TO_POINTER(i));
    }
}

static void connectScreenLockers()
{
    g_busCancellable = g_cancellable_new();
    g_bus_get(G_BUS_TYPE_SESSION, g_busCancellable, sessionBusFound, nullptr);
}

static void disconnectScreenLockers()
{
    if (g_busCancellable) {
        g_cancellable_cancel(g_busCancellable);
        g_object_unref(g_busCancellable);
    }
    g_busCancellable = nullptr;
    if (g_sessionBus) {
        for (guint &subscription: g_lockerSubscriptions)
            if (subscription)
                g_dbus_connection_signal_unsubscribe(g_sessionBus, subscription);
        g_object_unref(g_sessionBus);
    }
    g_sessionBus = nullptr;
    for (size_t i = 0; i < SCREEN_LOCKER_COUNT; i++) {
        g_lockerSubscriptions[i] = 0;
        g_lockerActive[i]        = false;
    }
}

// The screen locked, as above; or blanked by the X server, or its monitor put to sleep, as asked of
// its extensions, which Pidgin links against for its idle time: without them, or without X, it is
// taken not to be. A screensaver program that tells neither grabs the keyboard, which takes the
// focus from Pidgin's windows.
struct ScreenSaverInfo {        // XScreenSaverInfo, as in X11/extensions/scrnsaver.h
    unsigned long window;
    int           state;
    int           kind;
    unsigned long tilOrSince;
    unsigned long idle;
    unsigned long eventMask;
};
enum { SCREEN_SAVER_ON = 1, DPMS_MODE_ON = 0 };
using GetDisplay        = void *(*)();
using GetRootWindow     = unsigned long (*)();
using QueryExtension    = int (*)(void *display, int *eventBase, int *errorBase);
using QueryScreenSaver  = int (*)(void *display, unsigned long drawable, ScreenSaverInfo *info);
using DpmsCapable       = int (*)(void *display);
using DpmsInfo          = int (*)(void *display, unsigned short *powerLevel, unsigned char *state);

static bool isScreenBlanked()
{
    if (std::find(std::begin(g_lockerActive), std::end(g_lockerActive), true) != std::end(g_lockerActive))
        return true;
    static bool             lookedUp = false;
    static void            *display  = nullptr;
    static unsigned long    root     = 0;
    static QueryScreenSaver querySaver = nullptr;
    static DpmsInfo         dpmsInfo   = nullptr;
    if (!lookedUp) {
        lookedUp = true;
        GetDisplay    getDisplay = reinterpret_cast<GetDisplay>(findFrontEndSymbol("gdk_x11_get_default_xdisplay"));
        GetRootWindow getRoot    = reinterpret_cast<GetRootWindow>(findFrontEndSymbol("gdk_x11_get_default_root_xwindow"));
        display = getDisplay ? getDisplay() : nullptr;
        root    = (display && getRoot) ? getRoot() : 0;
        int eventBase, errorBase;
        QueryExtension saverExtension = reinterpret_cast<QueryExtension>(findFrontEndSymbol("XScreenSaverQueryExtension"));
        if (root && saverExtension && saverExtension(display, &eventBase, &errorBase))
            querySaver = reinterpret_cast<QueryScreenSaver>(findFrontEndSymbol("XScreenSaverQueryInfo"));
        QueryExtension dpmsExtension = reinterpret_cast<QueryExtension>(findFrontEndSymbol("DPMSQueryExtension"));
        DpmsCapable    dpmsCapable   = reinterpret_cast<DpmsCapable>(findFrontEndSymbol("DPMSCapable"));
        if (display && dpmsExtension && dpmsCapable && dpmsExtension(display, &eventBase, &errorBase) &&
            dpmsCapable(display))
            dpmsInfo = reinterpret_cast<DpmsInfo>(findFrontEndSymbol("DPMSInfo"));
        purple_debug_misc(config::pluginId, "Screen blanking %s told by the X server's saver, %s by DPMS\n",
                          querySaver ? "is" : "is not", dpmsInfo ? "is" : "is not");
    }
    ScreenSaverInfo info;
    if (querySaver && querySaver(display, root, &info) && (info.state == SCREEN_SAVER_ON))
        return true;
    unsigned short powerLevel;
    unsigned char  enabled;
    return dpmsInfo && dpmsInfo(display, &powerLevel, &enabled) && enabled && (powerLevel != DPMS_MODE_ON);
}

// Whether the tab, shown, is before the user's eyes: its window has the focus
static bool isWindowFocused(PurpleConversation *conv)
{
    PidginWindow *win = getIMHtml(conv) ? PIDGIN_CONVERSATION(conv)->win : nullptr;
    return win && win->window && p_gtk_window_is_active(reinterpret_cast<GtkWindow *>(win->window));
}

// The messages any of which is between the top and the bottom of the tab's text on screen
static std::vector<MessageId> findViewedMessages(GtkIMHtml *imhtml)
{
    GtkTextView *view = reinterpret_cast<GtkTextView *>(imhtml);
    GdkRectangle rect;
    GtkTextIter  iter, last;
    p_gtk_text_view_get_visible_rect(view, &rect);
    p_gtk_text_view_get_iter_at_location(view, &iter, rect.x, rect.y);
    p_gtk_text_view_get_iter_at_location(view, &last, rect.x + rect.width, rect.y + rect.height - 1);

    std::vector<MessageId> messageIds;
    MessageId              first = findMessageAt(imhtml->text_buffer, iter);
    if (first.valid())
        messageIds.push_back(first);
    do {
        GSList *marks = p_gtk_text_iter_get_marks(&iter);
        for (GSList *item = marks; item; item = item->next) {
            const char *name = p_gtk_text_mark_get_name(static_cast<GtkTextMark *>(item->data));
            MessageId   messageId = (name && g_str_has_prefix(name, START_MARK_PREFIX))
                                    ? MessageId::fromString(name + strlen(START_MARK_PREFIX)) : MessageId();
            if (messageId.valid() && (messageIds.empty() || !(messageIds.back() == messageId)))
                messageIds.push_back(messageId);
        }
        g_slist_free(marks);
    } while ((p_gtk_text_iter_compare(&iter, &last) < 0) && p_gtk_text_iter_forward_char(&iter));
    return messageIds;
}

static gboolean checkViewed(gpointer data)
{
    // Its source is removed with it
    PurpleConversation *conv   = static_cast<PurpleConversation *>(data);
    GtkIMHtml          *imhtml = getIMHtml(conv);
    // Not focused: checked again once it is
    bool focused = imhtml && g_shownConversations.count(conv) && isWindowFocused(conv);
    // Still scrolling to the end, written to; or nobody sees it until the screen is woken up, which
    // nothing tells
    if (imhtml && (imhtml->scroll_src || (focused && isScreenBlanked())))
        return G_SOURCE_CONTINUE;
    purple_conversation_set_data(conv, VIEW_SOURCE_KEY, nullptr);
    if (focused && g_viewedHandler) {
        std::vector<MessageId> messageIds = findViewedMessages(imhtml);
        if (!messageIds.empty())
            g_viewedHandler(conv, messageIds);
    }
    // Written to, as messages that waited for something came, a tab stopping short of the newest
    // with no more than fits goes on to later ones, nothing else telling it to
    GtkAdjustment *adjustment = imhtml ? reinterpret_cast<GtkTextView *>(imhtml)->vadjustment : nullptr;
    if (adjustment && (adjustment->upper <= adjustment->page_size))
        checkBottomReached(conv, imhtml);
    return G_SOURCE_REMOVE;
}

static void stopViewCheck(PurpleConversation *conv)
{
    guint source = GPOINTER_TO_UINT(purple_conversation_get_data(conv, VIEW_SOURCE_KEY));
    if (source) {
        g_source_remove(source);
        purple_conversation_set_data(conv, VIEW_SOURCE_KEY, nullptr);
    }
}

static void scheduleViewCheck(PurpleConversation *conv)
{
    if (!g_tracksShown || !g_shownConversations.count(conv))
        return;
    stopViewCheck(conv);
    guint source = g_timeout_add(VIEW_SETTLE_MS, checkViewed, conv);
    purple_conversation_set_data(conv, VIEW_SOURCE_KEY, GUINT_TO_POINTER(source));
}

void checkViewedLater(PurpleConversation *conv)
{
    scheduleViewCheck(conv);
}

static void refreshShown()
{
    std::set<PurpleConversation *> shown;
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv = static_cast<PurpleConversation *>(item->data);
        if (isOurConversation(conv) && findShown(conv))
            shown.insert(conv);
    }
    std::vector<PurpleConversation *> hidden, revealed;
    for (PurpleConversation *conv: g_shownConversations)
        if (!shown.count(conv))
            hidden.push_back(conv);
    for (PurpleConversation *conv: shown)
        if (!g_shownConversations.count(conv))
            revealed.push_back(conv);
    g_shownConversations = std::move(shown);
    for (PurpleConversation *conv: hidden) {
        stopViewCheck(conv);
        if (g_shownHandler)
            g_shownHandler(conv, false);
    }
    for (PurpleConversation *conv: revealed) {
        if (g_shownHandler)
            g_shownHandler(conv, true);
        scheduleViewCheck(conv);
    }
}

static gboolean refreshShownNow(gpointer)
{
    g_shownSource = 0;
    refreshShown();
    return G_SOURCE_REMOVE;
}

// Once Pidgin is done: its window's chosen tab is not changed yet when it says it is switching
static void refreshShownLater()
{
    if (!g_shownSource)
        g_shownSource = g_idle_add(refreshShownNow, nullptr);
}

static void conversationSwitched(gpointer)
{
    refreshShownLater();
}

static void deletingConversation(PurpleConversation *conv)
{
    stopViewCheck(conv);
    if (g_shownConversations.erase(conv) && g_shownHandler)
        g_shownHandler(conv, false);
    refreshShownLater();
}

// Any window minimized or restored, Pidgin's or not
static gboolean windowStateChanged(GSignalInvocationHint *, guint, const GValue *, gpointer)
{
    refreshShownLater();
    return TRUE;
}

// The shown tabs of the windows focused are looked at again, and none of the others is
static gboolean refreshFocusedNow(gpointer)
{
    g_focusSource = 0;
    for (PurpleConversation *conv: g_shownConversations)
        if (isWindowFocused(conv))
            scheduleViewCheck(conv);
        else
            stopViewCheck(conv);
    return G_SOURCE_REMOVE;
}

// Any widget given the focus or losing it, Pidgin's or not, its window among them: once the
// window's own handler has said whether it is active, which it does only after the hooks
static gboolean focusChanged(GSignalInvocationHint *, guint, const GValue *, gpointer)
{
    if (!g_focusSource)
        g_focusSource = g_idle_add(refreshFocusedNow, nullptr);
    return TRUE;
}

static void connectShown(gpointer plugin)
{
    if (!LOOK_UP(pidgin_conv_window_get_active_conversation) || !LOOK_UP(gdk_window_get_state) ||
        !LOOK_UP(gtk_text_view_get_visible_rect) || !LOOK_UP(gtk_text_view_get_iter_at_location) ||
        !LOOK_UP(gtk_text_iter_get_marks) || !LOOK_UP(gtk_text_mark_get_name) || !LOOK_UP(gtk_widget_get_type) ||
        !LOOK_UP(gtk_window_is_active) ||
        !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook))
    {
        purple_debug_warning(config::pluginId, "Cannot tell which tabs are shown: some of GTK is not found\n");
        return;
    }
    g_windowStateSignal = p_g_signal_lookup("window-state-event", p_gtk_widget_get_type());
    if (g_windowStateSignal)
        g_windowStateHook = p_g_signal_add_emission_hook(g_windowStateSignal, 0, windowStateChanged, nullptr,
                                                         nullptr);
    // Not GtkWindow's is-active: GObject's notify, which says it, takes no emission hooks
    g_focusInSignal  = p_g_signal_lookup("focus-in-event", p_gtk_widget_get_type());
    g_focusOutSignal = p_g_signal_lookup("focus-out-event", p_gtk_widget_get_type());
    if (g_focusInSignal)
        g_focusInHook = p_g_signal_add_emission_hook(g_focusInSignal, 0, focusChanged, nullptr, nullptr);
    if (g_focusOutSignal)
        g_focusOutHook = p_g_signal_add_emission_hook(g_focusOutSignal, 0, focusChanged, nullptr, nullptr);
    if (!g_windowStateHook || !g_focusInHook || !g_focusOutHook) {
        if (g_windowStateHook)
            p_g_signal_remove_emission_hook(g_windowStateSignal, g_windowStateHook);
        if (g_focusInHook)
            p_g_signal_remove_emission_hook(g_focusInSignal, g_focusInHook);
        if (g_focusOutHook)
            p_g_signal_remove_emission_hook(g_focusOutSignal, g_focusOutHook);
        g_windowStateHook = 0;
        g_focusInHook     = 0;
        g_focusOutHook    = 0;
        purple_debug_warning(config::pluginId, "Cannot tell which tabs are shown: windows cannot be followed\n");
        return;
    }
    purple_signal_connect(p_pidgin_conversations_get_handle(), "conversation-switched", plugin,
                          PURPLE_CALLBACK(conversationSwitched), nullptr);
    purple_signal_connect(p_pidgin_conversations_get_handle(), "conversation-displayed", plugin,
                          PURPLE_CALLBACK(conversationSwitched), nullptr);
    purple_signal_connect(purple_conversations_get_handle(), "deleting-conversation", plugin,
                          PURPLE_CALLBACK(deletingConversation), nullptr);
    // Those that have been taken to be shown so far, which most are not
    for (GList *item = purple_get_conversations(); item; item = item->next) {
        PurpleConversation *conv = static_cast<PurpleConversation *>(item->data);
        if (isOurConversation(conv) && getIMHtml(conv))
            g_shownConversations.insert(conv);
    }
    g_tracksShown = true;
    connectScreenLockers();
    refreshShown();
}

static void disconnectShown()
{
    if (g_windowStateHook)
        p_g_signal_remove_emission_hook(g_windowStateSignal, g_windowStateHook);
    if (g_focusInHook)
        p_g_signal_remove_emission_hook(g_focusInSignal, g_focusInHook);
    if (g_focusOutHook)
        p_g_signal_remove_emission_hook(g_focusOutSignal, g_focusOutHook);
    g_windowStateHook = 0;
    g_focusInHook     = 0;
    g_focusOutHook    = 0;
    if (g_focusSource)
        g_source_remove(g_focusSource);
    g_focusSource = 0;
    if (g_shownSource)
        g_source_remove(g_shownSource);
    g_shownSource = 0;
    for (GList *item = purple_get_conversations(); item; item = item->next)
        stopViewCheck(static_cast<PurpleConversation *>(item->data));
    g_shownConversations.clear();
    disconnectScreenLockers();
    g_tracksShown = false;
}

static void connectPopup()
{
    if (!LOOK_UP(gtk_imhtml_get_type) || !LOOK_UP(g_signal_lookup) || !LOOK_UP(g_signal_add_emission_hook) ||
        !LOOK_UP(g_signal_remove_emission_hook) || !LOOK_UP(g_value_get_object) ||
        !LOOK_UP(gtk_widget_get_pointer) || !LOOK_UP(gtk_text_view_window_to_buffer_coords) ||
        !LOOK_UP(gtk_text_view_get_iter_at_location) || !LOOK_UP(gtk_text_view_get_iter_at_position) ||
        !LOOK_UP(gtk_text_iter_get_marks) || !LOOK_UP(gtk_text_mark_get_name) ||
        !LOOK_UP(gtk_separator_menu_item_new) || !LOOK_UP(gtk_widget_show))
    {
        purple_debug_warning(config::pluginId, "Cannot add to the menu of a tab's text: some of GTK is not found\n");
        return;
    }
    // The placeholders' menu is not added without them
    LOOK_UP(gtk_text_buffer_get_slice);
    LOOK_UP(gtk_text_buffer_insert);
    g_popupSignal = p_g_signal_lookup("populate-popup", p_gtk_imhtml_get_type());
    if (g_popupSignal)
        g_popupHook = p_g_signal_add_emission_hook(g_popupSignal, 0, populatingPopup, nullptr, nullptr);
    if (!LOOK_UP(gtk_widget_get_type) || !LOOK_UP(g_value_get_boxed) || !LOOK_UP(gtk_widget_get_parent) ||
        !LOOK_UP(gtk_menu_get_type) || !LOOK_UP(g_type_check_instance_is_a))
    {
        purple_debug_warning(config::pluginId, "Cannot add to the menu of a picture: some of GTK is not found\n");
        return;
    }
    g_pictureEventSignal = p_g_signal_lookup("event", p_gtk_widget_get_type());
    g_menuShowSignal     = p_g_signal_lookup("show", p_gtk_widget_get_type());
    if (g_pictureEventSignal && g_menuShowSignal) {
        g_pictureEventHook = p_g_signal_add_emission_hook(g_pictureEventSignal, 0, pictureClicked, nullptr, nullptr);
        g_menuShowHook     = p_g_signal_add_emission_hook(g_menuShowSignal, 0, menuShown, nullptr, nullptr);
    }
}

static void disconnectPopup()
{
    if (g_popupHook)
        p_g_signal_remove_emission_hook(g_popupSignal, g_popupHook);
    if (g_pictureEventHook)
        p_g_signal_remove_emission_hook(g_pictureEventSignal, g_pictureEventHook);
    if (g_menuShowHook)
        p_g_signal_remove_emission_hook(g_menuShowSignal, g_menuShowHook);
    g_popupHook        = 0;
    g_pictureEventHook = 0;
    g_menuShowHook     = 0;
    if (g_pictureMenuSource)
        g_source_remove(g_pictureMenuSource);
    g_pictureMenuSource = 0;
    g_pictureMenu       = PictureMenu();
}

// The dialog choosing out of a list: what is typed in its field narrows the list down to the items
// with that in them, the first of which is chosen by Enter; a double-click chooses too. Several are
// chosen by ticking them, which narrowing the list leaves ticked; then those are what is chosen.
namespace {
struct ListChooser {
    std::function<void(const std::vector<int> &)> chosen;
    GtkWidget                                    *entry  = nullptr;
    GtkWidget                                    *view   = nullptr;
    GtkListStore                                 *store  = nullptr;
    GtkTreeModel                                 *filter = nullptr;
    std::string                                   needle;  // what is typed, case folded
};
enum { COLUMN_LABEL, COLUMN_INDEX, COLUMN_TICKED };
}

static gboolean listItemVisible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    const ListChooser *chooser = static_cast<const ListChooser *>(data);
    if (chooser->needle.empty())
        return TRUE;
    gchar *label = nullptr;
    p_gtk_tree_model_get(model, iter, COLUMN_LABEL, &label, -1);
    if (!label)
        return FALSE;
    gchar   *folded  = g_utf8_casefold(label, -1);
    gboolean visible = (strstr(folded, chooser->needle.c_str()) != nullptr);
    g_free(folded);
    g_free(label);
    return visible;
}

static void listSearchChanged(GtkEditable *, gpointer data)
{
    ListChooser *chooser = static_cast<ListChooser *>(data);
    gchar *folded = g_utf8_casefold(p_gtk_entry_get_text(reinterpret_cast<GtkEntry *>(chooser->entry)), -1);
    chooser->needle = folded;
    g_free(folded);
    p_gtk_tree_model_filter_refilter(reinterpret_cast<GtkTreeModelFilter *>(chooser->filter));
    // The first of what is left is what Enter takes
    GtkTreeIter first;
    if (p_gtk_tree_model_get_iter_first(chooser->filter, &first))
        p_gtk_tree_selection_select_iter(p_gtk_tree_view_get_selection(reinterpret_cast<GtkTreeView *>(chooser->view)),
                                         &first);
}

static void listItemActivated(GtkTreeView *, GtkTreePath *, GtkTreeViewColumn *, gpointer data)
{
    p_gtk_dialog_response(reinterpret_cast<GtkDialog *>(data), GTK_RESPONSE_OK);
}

// The path is of the list as narrowed down; the tick goes into the whole one
static void listItemToggled(GtkCellRendererToggle *, gchar *path, gpointer data)
{
    ListChooser *chooser = static_cast<ListChooser *>(data);
    GtkTreeIter  shown, item;
    gboolean     ticked = FALSE;
    if (!p_gtk_tree_model_get_iter_from_string(chooser->filter, &shown, path))
        return;
    p_gtk_tree_model_filter_convert_iter_to_child_iter(reinterpret_cast<GtkTreeModelFilter *>(chooser->filter),
                                                       &item, &shown);
    p_gtk_tree_model_get(reinterpret_cast<GtkTreeModel *>(chooser->store), &item, COLUMN_TICKED, &ticked, -1);
    p_gtk_list_store_set(chooser->store, &item, COLUMN_TICKED, !ticked, -1);
}

static void listChooserResponse(GtkDialog *dialog, gint response, gpointer data)
{
    ListChooser     *chooser = static_cast<ListChooser *>(data);
    std::vector<int> indices;
    GtkTreeModel    *store   = reinterpret_cast<GtkTreeModel *>(chooser->store);
    GtkTreeIter      iter;
    for (bool more = (response == GTK_RESPONSE_OK) && p_gtk_tree_model_get_iter_first(store, &iter); more;
         more = p_gtk_tree_model_iter_next(store, &iter))
    {
        gboolean ticked = FALSE;
        int      index  = -1;
        p_gtk_tree_model_get(store, &iter, COLUMN_TICKED, &ticked, COLUMN_INDEX, &index, -1);
        if (ticked)
            indices.push_back(index);
    }
    // None ticked: the one picked
    GtkTreeModel *model = nullptr;
    if ((response == GTK_RESPONSE_OK) && indices.empty() &&
        p_gtk_tree_selection_get_selected(p_gtk_tree_view_get_selection(reinterpret_cast<GtkTreeView *>(chooser->view)),
                                          &model, &iter))
    {
        int index = -1;
        p_gtk_tree_model_get(model, &iter, COLUMN_INDEX, &index, -1);
        if (index >= 0)
            indices.push_back(index);
    }
    std::function<void(const std::vector<int> &)> chosen = std::move(chooser->chosen);
    p_gtk_widget_destroy(reinterpret_cast<GtkWidget *>(dialog));
    if (!indices.empty())
        chosen(indices);
}

static void deleteListChooser(gpointer data)
{
    ListChooser *chooser = static_cast<ListChooser *>(data);
    if (chooser->store)
        p_g_object_unref(chooser->store);
    delete chooser;
}

bool chooseFromList(const std::string &title, const std::string &prompt, const std::vector<std::string> &labels,
                    std::function<void(const std::vector<int> &indices)> chosen)
{
    if (!g_pidginUi || !g_chooser)
        return false;

    auto *chooser = new ListChooser;
    chooser->chosen = std::move(chosen);
    GtkWidget *dialog = p_gtk_dialog_new_with_buttons(title.c_str(), nullptr, GtkDialogFlags(0),
                                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                      GTK_STOCK_OK, GTK_RESPONSE_OK, nullptr);
    p_gtk_dialog_set_default_response(reinterpret_cast<GtkDialog *>(dialog), GTK_RESPONSE_OK);
    p_gtk_window_set_default_size(reinterpret_cast<GtkWindow *>(dialog), 400, 500);
    // Let go with the dialog
    p_g_object_set_data_full(reinterpret_cast<GObject *>(dialog), "tdlib-purple-chooser", chooser, deleteListChooser);

    // Kept with the chooser, for the ticks of the items not shown
    GtkListStore *store = p_gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    for (size_t i = 0; i < labels.size(); i++) {
        GtkTreeIter iter;
        p_gtk_list_store_append(store, &iter);
        p_gtk_list_store_set(store, &iter, COLUMN_LABEL, labels[i].c_str(), COLUMN_INDEX, int(i),
                             COLUMN_TICKED, FALSE, -1);
    }
    chooser->store  = store;
    chooser->filter = p_gtk_tree_model_filter_new(reinterpret_cast<GtkTreeModel *>(store), nullptr);
    p_gtk_tree_model_filter_set_visible_func(reinterpret_cast<GtkTreeModelFilter *>(chooser->filter),
                                             listItemVisible, chooser, nullptr);
    chooser->view = p_gtk_tree_view_new_with_model(chooser->filter);
    p_g_object_unref(chooser->filter);
    p_gtk_tree_view_set_headers_visible(reinterpret_cast<GtkTreeView *>(chooser->view), FALSE);
    GtkCellRenderer *toggle = p_gtk_cell_renderer_toggle_new();
    p_g_signal_connect_data(toggle, "toggled", G_CALLBACK(listItemToggled), chooser, nullptr, GConnectFlags(0));
    p_gtk_tree_view_append_column(reinterpret_cast<GtkTreeView *>(chooser->view),
                                  p_gtk_tree_view_column_new_with_attributes("", toggle, "active", COLUMN_TICKED,
                                                                             nullptr));
    p_gtk_tree_view_append_column(reinterpret_cast<GtkTreeView *>(chooser->view),
                                  p_gtk_tree_view_column_new_with_attributes("", p_gtk_cell_renderer_text_new(),
                                                                             "text", COLUMN_LABEL, nullptr));
    p_g_signal_connect_data(chooser->view, "row-activated", G_CALLBACK(listItemActivated), dialog, nullptr,
                            GConnectFlags(0));

    chooser->entry = p_gtk_entry_new();
    p_gtk_entry_set_activates_default(reinterpret_cast<GtkEntry *>(chooser->entry), TRUE);
    p_g_signal_connect_data(chooser->entry, "changed", G_CALLBACK(listSearchChanged), chooser, nullptr,
                            GConnectFlags(0));

    GtkWidget *scrolled = p_gtk_scrolled_window_new(nullptr, nullptr);
    p_gtk_scrolled_window_set_policy(reinterpret_cast<GtkScrolledWindow *>(scrolled), GTK_POLICY_NEVER,
                                     GTK_POLICY_AUTOMATIC);
    p_gtk_container_add(reinterpret_cast<GtkContainer *>(scrolled), chooser->view);
    GtkWidget *content = p_gtk_dialog_get_content_area(reinterpret_cast<GtkDialog *>(dialog));
    p_gtk_box_pack_start(reinterpret_cast<GtkBox *>(content), p_gtk_label_new(prompt.c_str()), FALSE, FALSE, 4);
    p_gtk_box_pack_start(reinterpret_cast<GtkBox *>(content), chooser->entry, FALSE, FALSE, 4);
    p_gtk_box_pack_start(reinterpret_cast<GtkBox *>(content), scrolled, TRUE, TRUE, 4);
    p_g_signal_connect_data(dialog, "response", G_CALLBACK(listChooserResponse), chooser, nullptr, GConnectFlags(0));
    listSearchChanged(nullptr, chooser);
    p_gtk_widget_show_all(dialog);
    return true;
}

#else

void pidginUiLoad(PurplePlugin *)
{
}

bool chooseFromList(const std::string &, const std::string &, const std::vector<std::string> &,
                    std::function<void(const std::vector<int> &indices)>)
{
    return false;
}

void pidginUiUnload()
{
}

void setReactionClickHandler(ReactionClickHandler)
{
}

bool pidginUiFound()
{
    return false;
}

unsigned getTextLineHeight()
{
    return 0;
}

int makeStandInPicture(const std::string &, int, int, bool)
{
    return 0;
}

MessageMarkScope::MessageMarkScope(MessageId, const char *, bool, bool)
{
}

MessageMarkScope::~MessageMarkScope()
{
}

void showMessageReactions(PurpleConversation *, MessageId, const std::vector<ShownReaction> &)
{
}

void showMessageButtons(PurpleConversation *, MessageId, const std::vector<ShownReaction> &)
{
}

void setReactionTipHandler(ReactionTipHandler)
{
}

void refreshReactionTip(PurpleConversation *)
{
}

bool readPidginClipboard(PurpleConversation *, ClipboardContent &)
{
    return false;
}

void setPastedHandler(PastedHandler)
{
}

void pasteIntoEntryLater(PurpleConversation *, const std::vector<PastedPlaceholder> &)
{
}


bool beginEntryEdit(PurpleConversation *, const EntryEdit &, const std::string &,
                    const std::string &, std::function<void()>)
{
    return false;
}

bool endEntryEdit(PurpleConversation *, EntryEdit *)
{
    return false;
}

void showEditOutcome(PurpleConversation *, const std::string &)
{
}

bool isEntryEditing(PurpleConversation *, MessageId)
{
    return false;
}

bool canRedrawConversation(PurpleConversation *)
{
    return false;
}

void presentConversation(PurpleConversation *conv)
{
    purple_conversation_present(conv);
}

void appendTabLine(PurpleConversation *, const std::string &, MessageId)
{
}

void startProgress(PurpleConversation *, const std::string &, bool)
{
}

void runAfterDrawing(std::function<void()> then)
{
    then();
}

void stopProgress(PurpleConversation *)
{
}

void updateProgress(PurpleConversation *, const std::string &)
{
}

bool canReplaceMessage(PurpleConversation *, MessageId)
{
    return false;
}

bool canMarkChanges(PurpleConversation *, MessageId, bool &)
{
    return false;
}

void setChangesMarked(PurpleConversation *, MessageId, bool)
{
}

void strikeOutMessage(PurpleConversation *, MessageId)
{
}

void removeMessage(PurpleConversation *, MessageId)
{
}

MessageMarks findMessageMarks(PurpleConversation *, MessageId)
{
    return MessageMarks();
}

void renameMessageMarks(PurpleAccount *, MessageId, MessageId)
{
}

bool scrollToMessage(PidginIMHtml *, MessageId)
{
    return false;
}

void setMessagePopupHandler(MessagePopupHandler)
{
}

void setBuddyMarkHandler(BuddyMarkHandler, const char *)
{
}

MessageId findFirstMessage(PurpleConversation *)
{
    return MessageId();
}

void scrollToMessageLater(PurpleConversation *, MessageId)
{
}

void setTopReachedHandler(TopReachedHandler)
{
}

unsigned countTabLines(PurpleConversation *)
{
    return 0;
}

MessageId findMessageAfter(PurpleConversation *, MessageId)
{
    return MessageId();
}

MessageId findLastMessage(PurpleConversation *)
{
    return MessageId();
}

bool trimTab(PurpleConversation *, unsigned, bool, MessageId)
{
    return false;
}

void setTabEnd(PurpleConversation *, MessageId)
{
}

MessageId getTabEnd(PurpleConversation *)
{
    return MessageId();
}

void hideMessage(PurpleConversation *, MessageId)
{
}

void keepViewLater(PurpleConversation *)
{
}

void setBottomReachedHandler(BottomReachedHandler)
{
}

void setLatestAskedHandler(LatestAskedHandler, const std::string &)
{
}

void fillOpenMenu(const OpenMenuRef &, const std::vector<MenuItem> &)
{
}

void setPlaceholderHandler(PlaceholderHandler)
{
}

void addLinkMenuItems(PidginWidget *, const std::vector<MenuItem> &)
{
}

bool addLinkMessageMenu(PidginIMHtml *, PidginWidget *, MessageId, bool)
{
    return false;
}

PurpleConversation *getTabConversation(PidginIMHtml *)
{
    return nullptr;
}

bool copyFileToClipboard(const std::string &, bool)
{
    return false;
}

bool copyTextToClipboard(const std::string &)
{
    return false;
}

void setDraggedFileHandler(DraggedFileHandler)
{
}

bool clearUnseen(PurpleConversation *)
{
    return false;
}

bool tracksShownConversations()
{
    return false;
}

bool isConversationShown(PurpleConversation *)
{
    return true;
}

void setShownHandler(ShownHandler)
{
}

bool viewsMessages(PurpleConversation *)
{
    return false;
}

void setViewedHandler(ViewedHandler)
{
}

void checkViewedLater(PurpleConversation *)
{
}

#endif
