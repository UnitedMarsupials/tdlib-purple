#ifndef _LIBPURPLE_MOCK_H
#define _LIBPURPLE_MOCK_H

#include <glib.h>
#include <map>
#include <vector>
#include <string>
#include <purple.h>

void printToConsole(const std::string &text);

extern "C" {

void setFakeFileSize(const char *path, size_t size);
void clearFakeFiles();
int  getLastImgstoreId();
guint8 *arrayDup(gpointer data, size_t size);
void setUiName(const char *name);
void setUiInfoMissing(bool missing);
void clearGroups();
// Calls the handlers connected to a signal of libpurple's that passes nothing but their data
void emitSignal(const char *signal);

};

// What the mock GdkPixbuf has loaders for, out of "webp", "tgs" and "webm"; WebP by default, as
// Pidgin mostly has
void setImageLoaders(std::initializer_list<std::string> formats);
// Whether the front-end takes custom smileys, as Pidgin does; off by default
void setCustomSmileys(bool supported);
// Whether the front-end has request dialogs, so that purple_request_input asks; on by default
void setRequestInputSupported(bool supported);
// Which item of its list the next request of fields is answered with, by its OK button; given up
// at once otherwise, as by default
void setRequestListChoice(int index);
// The next dialogs asking for text answered in turn, each by its fields' values ("1" for a box
// ticked); those asked for with none left are given up
void setRequestAnswers(std::vector<std::map<std::string, std::string>> answers);
size_t getRequestAnswersLeft();
void setRequestListChoices(std::vector<int> indices);
void clearConversationData();
// What conversations have written while logging, as Pidgin would log it, oldest first; taken, so
// that the next call has only what is written since
std::vector<std::string> takeLoggedMessages();
// Whether Pidgin is set to log new conversations, as purple_prefs_get_bool tells; on by default
void setLoggingPreferred(bool logging);
// Whether the account's presence is idle, as libpurple tells; not by default
void setPresenceIdle(bool idle);

using RoomlistData = std::vector<std::pair<PurpleRoomlistFieldType, std::string>>;

#if !GLIB_CHECK_VERSION(2, 34, 0)
static void g_list_free_full (GList *list, GDestroyNotify free_func)
{
    if (free_func)
        for (GList *item = list; item; item = item->next)
            free_func(item->data);
    g_list_free(list);
}
#endif

#endif
