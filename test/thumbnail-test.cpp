#include "thumbnail.h"
#include <gtest/gtest.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <string>

// Thumbnailers as freedesktop.org registers them, for the type of a file without one kept for it
class ThumbnailTest: public testing::Test {
protected:
    std::string directory;
    std::string thumbnailers;
    std::string file;

    void SetUp() override
    {
        gchar *dir = g_dir_make_tmp("thumbnail-test-XXXXXX", nullptr);
        ASSERT_NE(nullptr, dir);
        directory = dir;
        g_free(dir);
        thumbnailers = directory + "/thumbnailers";
        g_mkdir(thumbnailers.c_str(), 0755);
        file = directory + "/notes.txt";
        ASSERT_TRUE(g_file_set_contents(file.c_str(), "Notes\n", -1, nullptr));
        setThumbnailerDirectories({directory + "/none", thumbnailers});
    }

    void TearDown() override
    {
        setThumbnailerDirectories({});
        GDir *dir = g_dir_open(thumbnailers.c_str(), 0, nullptr);
        while (const gchar *name = dir ? g_dir_read_name(dir) : nullptr)
            g_unlink((thumbnailers + "/" + name).c_str());
        if (dir)
            g_dir_close(dir);
        g_rmdir(thumbnailers.c_str());
        g_unlink(file.c_str());
        g_rmdir(directory.c_str());
    }

    void addThumbnailer(const std::string &name, const std::string &entry)
    {
        std::string path = thumbnailers + "/" + name + ".thumbnailer";
        ASSERT_TRUE(g_file_set_contents(path.c_str(), ("[Thumbnailer Entry]\n" + entry).c_str(), -1, nullptr));
    }

    // What the thumbnailer made, read before it goes, or nothing
    std::string find(const std::string &mimeType)
    {
        bool        told = false;
        std::string content;
        findThumbnail(file, mimeType, [&](const std::string &png) {
            told = true;
            gchar *data = nullptr;
            if (!png.empty() && g_file_get_contents(png.c_str(), &data, nullptr, nullptr))
                content = data;
            g_free(data);
        });
        for (int i = 0; !told && (i < 1000); i++)
            g_main_context_iteration(nullptr, TRUE);
        EXPECT_TRUE(told);
        return content;
    }
};

TEST_F(ThumbnailTest, MadeByThumbnailer)
{
    // Of another type, and one whose program is not there, first
    addThumbnailer("a-other", "Exec=cp %i %o\nMimeType=image/png;\n");
    addThumbnailer("b-missing", "TryExec=no-such-thumbnailer\nExec=no-such-thumbnailer %i %o\nMimeType=text/plain;\n");
    addThumbnailer("c-copying", "TryExec=cp\nExec=sh -c 'echo %s >> %o; cat %i >> %o'\nMimeType=text/x-foo;text/plain;\n");
    EXPECT_EQ("128\nNotes\n", find("text/plain"));
}

TEST_F(ThumbnailTest, NoThumbnailer)
{
    addThumbnailer("failing", "Exec=false %i %o\nMimeType=text/plain;\n");
    EXPECT_EQ("", find("text/plain"));
    EXPECT_EQ("", find("application/pdf"));
}
