#include "clipboard.h"
#include "config.h"
#include "format.h"
#include "translate.h"
#include <purple.h>
#include <string.h>
#include <vector>
#ifndef _WIN32
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

static const char XCLIP[] = "xclip";

std::vector<std::string> getLocalPaths(char **uris)
{
    std::vector<std::string> paths;
    for (gchar **uri = uris; uri && *uri; uri++) {
        gchar *path = g_filename_from_uri(*uri, nullptr, nullptr);
        if (path)
            paths.push_back(path);
        g_free(path);
    }
    return paths;
}

std::vector<std::string> parseUriList(const std::string &list)
{
    gchar                  **uris  = g_uri_list_extract_uris(list.c_str());
    std::vector<std::string> paths = getLocalPaths(uris);
    g_strfreev(uris);
    return paths;
}

#ifndef _WIN32
// Runs the command, taking what it writes; false, with the error, if it cannot be started
static bool runCommand(const std::vector<const char *> &args, std::string &out, std::string &err, int &status,
                       GError **error)
{
    std::vector<gchar *> argv;
    for (const char *arg: args)
        argv.push_back(const_cast<gchar *>(arg));
    argv.push_back(nullptr);

    GPid pid;
    int  outFd, errFd;
    if (!g_spawn_async_with_pipes(nullptr, argv.data(), nullptr,
                                  GSpawnFlags(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD), nullptr, nullptr,
                                  &pid, nullptr, &outFd, &errFd, error))
        return false;

    struct pollfd fds[2] = {{outFd, POLLIN, 0}, {errFd, POLLIN, 0}};
    std::string  *into[2]   = {&out, &err};
    int           remaining = 2;
    char          buffer[65536];
    while (remaining) {
        if (poll(fds, 2, -1) < 0)
            break;
        for (int i = 0; i < 2; i++)
            if (fds[i].fd >= 0 && fds[i].revents) {
                ssize_t len = read(fds[i].fd, buffer, sizeof(buffer));
                if (len > 0)
                    into[i]->append(buffer, len);
                else {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    remaining--;
                }
            }
    }
    for (struct pollfd &fd: fds)
        if (fd.fd >= 0)
            close(fd.fd);
    if (waitpid(pid, &status, 0) < 0)
        status = -1;
    g_spawn_close_pid(pid);
    return true;
}

// xclip's output of the type; false, with what failed, if it gives none
static bool runXclip(const char *type, std::string &data, std::string &error)
{
    std::string err;
    int         status     = 0;
    GError     *spawnError = nullptr;
    data.clear();
    if (!runCommand({XCLIP, "-selection", "clipboard", "-t", type, "-o"}, data, err, status, &spawnError)) {
        error = spawnError->message;
        g_error_free(spawnError);
        return false;
    }
    if (WIFEXITED(status) && !WEXITSTATUS(status) && !data.empty())
        return true;
    while (!err.empty() && strchr(" \t\r\n", err.back()))
        err.pop_back();
    // TRANSLATOR: Error after the /paste command, when xclip fails without saying why.
    error = !err.empty() ? err
                         : formatMessage(_("xclip failed, with status {}"),
                                         std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : status));
    return false;
}

bool readXclip(ClipboardContent &content, std::string &error)
{
    std::string data;
    if (!runXclip("TARGETS", data, error))
        return false;
    bool    haveFiles = false;
    gchar **targets   = g_strsplit(data.c_str(), "\n", -1);
    for (gchar **target = targets; *target; target++)
        if (!strcmp(g_strstrip(*target), "text/uri-list"))
            haveFiles = true;
    g_strfreev(targets);

    if (haveFiles) {
        if (!runXclip("text/uri-list", data, error))
            return false;
        content.files = parseUriList(data);
        if (!content.files.empty())
            return true;
    }
    if (!runXclip("image/png", data, error))
        return false;
    purple_debug_misc(config::pluginId, "Read a picture of %zu bytes with xclip\n", data.size());
    // The store takes the copy
    gpointer copy = g_malloc(data.size());
    memcpy(copy, data.data(), data.size());
    content.imageId = purple_imgstore_add_with_id(copy, data.size(), "clipboard.png");
    return true;
}
#else
bool readXclip(ClipboardContent &content, std::string &error)
{
    error = "xclip cannot be run on Windows";
    return false;
}
#endif
