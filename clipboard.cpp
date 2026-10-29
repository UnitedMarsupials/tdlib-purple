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

int storeXclipPicture(std::string &error)
{
    std::string data, err;
    int         status     = 0;
    GError     *spawnError = nullptr;
    if (!runCommand({XCLIP, "-selection", "clipboard", "-t", "image/png", "-o"}, data, err, status, &spawnError)) {
        error = spawnError->message;
        g_error_free(spawnError);
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) || data.empty()) {
        while (!err.empty() && strchr(" \t\r\n", err.back()))
            err.pop_back();
        // TRANSLATOR: Error after the /paste command, when xclip fails without saying why.
        error = !err.empty() ? err
                             : formatMessage(_("xclip failed, with status {}"),
                                             std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : status));
        return 0;
    }
    purple_debug_misc(config::pluginId, "Read a picture of %zu bytes with xclip\n", data.size());
    // The store takes the copy
    gpointer copy = g_malloc(data.size());
    memcpy(copy, data.data(), data.size());
    return purple_imgstore_add_with_id(copy, data.size(), "clipboard.png");
}
#else
int storeXclipPicture(std::string &error)
{
    error = "xclip cannot be run on Windows";
    return 0;
}
#endif
