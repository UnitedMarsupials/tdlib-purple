#include "clipboard.h"
#include <purple.h>
#include <gtest/gtest.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string>

// xclip as /paste runs it, outside Pidgin: a script of the test's in its place
class ClipboardTest: public testing::Test {
protected:
    std::string directory;
    std::string path;

    void SetUp() override
    {
        gchar *dir = g_dir_make_tmp("clipboard-test-XXXXXX", nullptr);
        ASSERT_NE(nullptr, dir);
        directory = dir;
        g_free(dir);
        const char *old = getenv("PATH");
        path = old ? old : "";
        // Ahead of the real one, if any; the script needs the rest of the path
        setenv("PATH", (directory + ":" + path).c_str(), 1);
    }

    void TearDown() override
    {
        setenv("PATH", path.c_str(), 1);
        g_unlink((directory + "/xclip").c_str());
        g_rmdir(directory.c_str());
    }

    // The script is given xclip's arguments, the type asked for fourth
    void installXclip(const std::string &body)
    {
        std::string script = directory + "/xclip";
        std::string text   = "#!/bin/sh\n" + body;
        ASSERT_TRUE(g_file_set_contents(script.c_str(), text.c_str(), -1, nullptr));
        g_chmod(script.c_str(), 0755);
    }
};

TEST_F(ClipboardTest, FailingSaysWhy)
{
    installXclip("echo 'Error: Can'\"'\"'t open display: (null)' >&2\nexit 1\n");
    ClipboardContent content;
    std::string      error;
    EXPECT_FALSE(readXclip(content, error));
    EXPECT_EQ("Error: Can't open display: (null)", error);
}

// Copied from a file manager, local files only
TEST_F(ClipboardTest, ReadsFiles)
{
    installXclip("case \"$4\" in\n"
                 "TARGETS) echo TARGETS; echo text/uri-list; echo image/png;;\n"
                 "text/uri-list) printf '# a comment\\r\\nfile:///tmp/a%%20b\\r\\nhttp://x/y\\r\\nfile:///c\\r\\n';;\n"
                 "*) exit 1;;\n"
                 "esac\n");
    ClipboardContent content;
    std::string      error;
    ASSERT_TRUE(readXclip(content, error)) << error;
    EXPECT_EQ(std::vector<std::string>({"/tmp/a b", "/c"}), content.files);
    EXPECT_EQ(0, content.imageId);
}

TEST_F(ClipboardTest, ReadsPng)
{
    installXclip("case \"$4\" in\n"
                 "TARGETS) echo TARGETS; echo image/png;;\n"
                 "image/png) printf 'PNG\\000data';;\n"
                 "*) exit 1;;\n"
                 "esac\n");
    ClipboardContent content;
    std::string      error;
    ASSERT_TRUE(readXclip(content, error)) << error;
    EXPECT_TRUE(content.files.empty());
    PurpleStoredImage *image = purple_imgstore_find_by_id(content.imageId);
    ASSERT_NE(nullptr, image);
    EXPECT_EQ(std::string("PNG\0data", 8),
              std::string(static_cast<const char *>(purple_imgstore_get_data(image)), purple_imgstore_get_size(image)));
}
