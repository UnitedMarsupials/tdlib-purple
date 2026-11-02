#include "thumbnail.h"
#include "config.h"
#include <purple.h>
#include <glib/gstdio.h>
#include <memory>
#include <string.h>
#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#endif

// A thumbnailer taking longer is given up on
static const guint THUMBNAILER_SECONDS = 10;

static std::vector<std::string> g_thumbnailerDirectories;
static bool                     g_directoriesSet = false;

void setThumbnailerDirectories(const std::vector<std::string> &directories)
{
    g_thumbnailerDirectories = directories;
    g_directoriesSet         = true;
}

static std::vector<std::string> getThumbnailerDirectories()
{
    if (g_directoriesSet)
        return g_thumbnailerDirectories;
    std::vector<std::string> directories;
    directories.push_back(std::string(g_get_user_data_dir()) + G_DIR_SEPARATOR_S "thumbnailers");
    for (const gchar *const *dir = g_get_system_data_dirs(); *dir; dir++)
        directories.push_back(std::string(*dir) + G_DIR_SEPARATOR_S "thumbnailers");
    return directories;
}

// The command of the first thumbnailer for the type there is, whose program is there too
static std::string findThumbnailer(const std::string &mimeType)
{
    for (const std::string &directory: getThumbnailerDirectories()) {
        GDir *dir = g_dir_open(directory.c_str(), 0, nullptr);
        if (!dir)
            continue;
        std::string command;
        while (const gchar *name = g_dir_read_name(dir)) {
            if (!g_str_has_suffix(name, ".thumbnailer"))
                continue;
            GKeyFile   *file  = g_key_file_new();
            std::string entry = directory + G_DIR_SEPARATOR_S + name;
            if (g_key_file_load_from_file(file, entry.c_str(), G_KEY_FILE_NONE, nullptr)) {
                gchar **types = g_key_file_get_string_list(file, "Thumbnailer Entry", "MimeType", nullptr, nullptr);
                gchar  *exec  = g_key_file_get_string(file, "Thumbnailer Entry", "Exec", nullptr);
                gchar  *tryExec = g_key_file_get_string(file, "Thumbnailer Entry", "TryExec", nullptr);
                gchar  *found   = tryExec ? g_find_program_in_path(tryExec) : nullptr;
                bool    usable  = exec && (!tryExec || found);
                for (gchar **type = types; usable && type && *type; type++)
                    if (mimeType == *type)
                        command = exec;
                g_free(found);
                g_free(tryExec);
                g_free(exec);
                g_strfreev(types);
            }
            g_key_file_free(file);
            if (!command.empty())
                break;
        }
        g_dir_close(dir);
        if (!command.empty())
            return command;
    }
    return "";
}

namespace {
struct ThumbnailerRun {
    ThumbnailFound found;
    std::string    output;
    std::string    directory;
    GPid           pid;
    guint          timeout = 0;
};
}

static gboolean thumbnailerTooLong(gpointer data)
{
    ThumbnailerRun *run = static_cast<ThumbnailerRun *>(data);
    run->timeout = 0;
    purple_debug_misc(config::pluginId, "Thumbnailer for %s takes too long\n", run->output.c_str());
#ifndef _WIN32
    kill(run->pid, SIGTERM);
#endif
    return FALSE;
}

static void thumbnailerDone(GPid pid, gint status, gpointer data)
{
    std::unique_ptr<ThumbnailerRun> run(static_cast<ThumbnailerRun *>(data));
    if (run->timeout)
        g_source_remove(run->timeout);
    g_spawn_close_pid(pid);
#ifndef _WIN32
    bool succeeded = WIFEXITED(status) && !WEXITSTATUS(status);
#else
    bool succeeded = !status;
#endif
    bool made = succeeded && g_file_test(run->output.c_str(), G_FILE_TEST_IS_REGULAR);
    run->found(made ? run->output : "");
    remove(run->output.c_str());
    remove(run->directory.c_str());
}

// Runs the thumbnailer's command, its %i, %u, %o and %s given their values
static bool runThumbnailer(const std::string &command, const std::string &path, unsigned size,
                           ThumbnailFound &found)
{
    gchar **argv = nullptr;
    if (!g_shell_parse_argv(command.c_str(), nullptr, &argv, nullptr))
        return false;
    gchar *directory = g_dir_make_tmp("tdlib_thumbnail_XXXXXX", nullptr);
    if (!directory) {
        g_strfreev(argv);
        return false;
    }
    std::string output = std::string(directory) + G_DIR_SEPARATOR_S "thumbnail.png";
    gchar      *uri    = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    std::vector<std::string> args;
    for (gchar **arg = argv; *arg; arg++) {
        std::string expanded;
        for (const char *s = *arg; *s; s++) {
            if ((*s != '%') || !s[1]) {
                expanded += *s;
                continue;
            }
            switch (*++s) {
            case 'i': expanded += path; break;
            case 'u': expanded += uri ? uri : ""; break;
            case 'o': expanded += output; break;
            case 's': expanded += std::to_string(size); break;
            case '%': expanded += '%'; break;
            default: break;
            }
        }
        args.push_back(expanded);
    }
    g_free(uri);
    g_strfreev(argv);

    std::vector<gchar *> spawnArgs;
    for (std::string &arg: args)
        spawnArgs.push_back(&arg[0]);
    spawnArgs.push_back(nullptr);
    GPid pid;
    if (!g_spawn_async(nullptr, spawnArgs.data(), nullptr,
                       GSpawnFlags(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL |
                                   G_SPAWN_STDERR_TO_DEV_NULL),
                       nullptr, nullptr, &pid, nullptr))
    {
        remove(directory);
        g_free(directory);
        return false;
    }
    purple_debug_misc(config::pluginId, "Making a thumbnail of %s: %s\n", path.c_str(), command.c_str());
    ThumbnailerRun *run = new ThumbnailerRun{std::move(found), output, directory, pid};
    g_free(directory);
    run->timeout = g_timeout_add_seconds(THUMBNAILER_SECONDS, thumbnailerTooLong, run);
    g_child_watch_add(pid, thumbnailerDone, run);
    return true;
}

// As file managers keep them, named by the MD5 of the file's URI, unless made before the file last
// changed: the smallest at least as big, or else the biggest. GIO tells this too, but would start
// its VFS, modules and all, for it.
static std::string findKeptThumbnail(const std::string &path, unsigned size)
{
    gchar *uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    if (!uri)
        return "";
    gchar *md5 = g_compute_checksum_for_string(G_CHECKSUM_MD5, uri, -1);
    g_free(uri);
    std::string name = std::string(md5) + ".png";
    g_free(md5);
    GStatBuf file;
    if (g_stat(path.c_str(), &file))
        return "";
    // As the specification sizes them
    static const struct { const char *name; unsigned size; } kinds[] = {
        {"normal", 128}, {"large", 256}, {"x-large", 512}, {"xx-large", 1024}
    };
    enum { KINDS = sizeof(kinds) / sizeof(kinds[0]) };
    int first = 0;
    while ((first < KINDS - 1) && (kinds[first].size < size))
        first++;
    std::vector<const char *> order;
    for (int i = first; i < KINDS; i++)
        order.push_back(kinds[i].name);
    for (int i = first - 1; i >= 0; i--)
        order.push_back(kinds[i].name);
    for (const char *kind: order) {
        std::string thumbnail = std::string(g_get_user_cache_dir()) + G_DIR_SEPARATOR_S "thumbnails" G_DIR_SEPARATOR_S +
                                kind + G_DIR_SEPARATOR_S + name;
        GStatBuf kept;
        if (!g_stat(thumbnail.c_str(), &kept) && S_ISREG(kept.st_mode) && (kept.st_mtime >= file.st_mtime))
            return thumbnail;
    }
    return "";
}

void findThumbnail(const std::string &path, const std::string &mimeType, unsigned size, ThumbnailFound found)
{
    std::string kept = findKeptThumbnail(path, size);
    if (!kept.empty()) {
        found(kept);
        return;
    }

    std::string command = findThumbnailer(mimeType);
    if (command.empty() || !runThumbnailer(command, path, size, found))
        found("");
}
