/* Kai-Term — unit tests for the file manager model (src/fm.c).
 *
 * No display, no Wayland, no EGL: the tests build a REAL directory tree under /tmp (directories, a
 * dotfile, a dangling symlink, a FIFO, an unreadable directory, four image formats, a binary file) and
 * drive the model through it. That is the only honest way to test a directory browser — a fake
 * filesystem would test the fake, not the scanner.
 *
 * The tree is removed at the end. The same sources are built as bin/test_fm_asan and run under
 * LeakSanitizer by `make asan`, so every fm_* allocation must be reachable from fm_destroy() or
 * fm_scan_free()/fm_preview_free().
 */
#include "kai-term.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

static int fails, checks;

#define CHECK(cond, msg) do {                                  \
    checks++;                                                  \
    if (!(cond)) {                                             \
        fails++;                                               \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);  \
    }                                                          \
} while (0)

#define BASE_FG 0xffe5e5e5u
#define BASE_BG 0xff101014u

/* ---------- the temporary tree ---------- */

static char root[512];

static void p_join(char *out, size_t cap, const char *rel)
{
    snprintf(out, cap, "%s/%s", root, rel);
}

static void wfile(const char *rel, const void *data, size_t n)
{
    char p[1024];
    p_join(p, sizeof p, rel);
    FILE *f = fopen(p, "wb");
    if (!f) {
        printf("cannot create %s: %s\n", p, strerror(errno));
        fails++;
        return;
    }
    if (n)
        fwrite(data, 1, n, f);
    fclose(f);
}

/* Count a directory's entries independently of fm_scan_build(): the scanner is then compared against
   a second, dumber implementation instead of against a number I typed in. */
static int count_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
            n++;
    closedir(d);
    return n;
}

/* The entry at a position of the view. The model only exposes fm_sel_entry(), but a painting test has
   to know which entry the painter put on a given ROW, which is the view's `top`, not the selection. */
static const FmEntry *vis_at(const FileMgr *fm, int pos)
{
    return &fm->ent[fm->vis[pos]];
}

/* Position of a name in the current view, -1 if it is not in it. The tests select by name so no check
   depends on the sort order or on which entries the hidden filter happens to drop. */
static int find_pos(const FileMgr *fm, const char *name)
{
    for (int i = 0; i < fm->nvis; i++)
        if (!strcmp(vis_at(fm, i)->name, name))
            return i;
    return -1;
}

/* Is NAME one of the entries a scan produced (the whole listing, not the filtered view)? */
static int has_entry(const FmScan *s, const char *name)
{
    for (int i = 0; i < s->n; i++)
        if (!strcmp(s->ent[i].name, name))
            return 1;
    return 0;
}

/* A text file, written by strlen() and never by a hand-typed byte count. The first draft typed the
   counts: code.c lost its last 11 bytes (so the `return` a later check looks for was not in the file),
   and notes.md / Makefile asked for MORE bytes than their literal holds, i.e. read past it. */
static void wstr(const char *rel, const char *s)
{
    wfile(rel, s, strlen(s));
}

static void rm_tree(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            char p[1024];
            snprintf(p, sizeof p, "%s/%s", path, de->d_name);
            rm_tree(p);
        }
        closedir(d);
        rmdir(path);
    } else {
        unlink(path);
    }
}

/* The four image formats are embedded rather than generated: the test must not depend on any tool
   being installed, and the bytes are what the decoders must accept forever. Generated once with PIL
   (8x8 px; the GIF has transparent index 1). */
static const unsigned char png8[87] = {
    0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,
    0x44,0x52,0x00,0x00,0x00,0x08,0x00,0x00,0x00,0x08,0x08,0x06,0x00,0x00,
    0x00,0xc4,0x0f,0xbe,0x8b,0x00,0x00,0x00,0x1e,0x49,0x44,0x41,0x54,0x78,
    0x9c,0x63,0x64,0x60,0x60,0xf8,0xaf,0xc0,0x20,0xc0,0x80,0x0b,0xb3,0x30,
    0x28,0x08,0x30,0x30,0x30,0xe0,0xc6,0xc3,0x43,0x01,0x00,0x5b,0x48,0x06,
    0xcd,0x78,0xf8,0x2e,0x87,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,
    0x42,0x60,0x82,
};
static const unsigned char jpg8[659] = {
    0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,
    0x00,0x01,0x00,0x01,0x00,0x00,0xff,0xdb,0x00,0x43,0x00,0x03,0x02,0x02,
    0x03,0x02,0x02,0x03,0x03,0x03,0x03,0x04,0x03,0x03,0x04,0x05,0x08,0x05,
    0x05,0x04,0x04,0x05,0x0a,0x07,0x07,0x06,0x08,0x0c,0x0a,0x0c,0x0c,0x0b,
    0x0a,0x0b,0x0b,0x0d,0x0e,0x12,0x10,0x0d,0x0e,0x11,0x0e,0x0b,0x0b,0x10,
    0x16,0x10,0x11,0x13,0x14,0x15,0x15,0x15,0x0c,0x0f,0x17,0x18,0x16,0x14,
    0x18,0x12,0x14,0x15,0x14,0xff,0xdb,0x00,0x43,0x01,0x03,0x04,0x04,0x05,
    0x04,0x05,0x09,0x05,0x05,0x09,0x14,0x0d,0x0b,0x0d,0x14,0x14,0x14,0x14,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,0x14,
    0x14,0x14,0x14,0x14,0xff,0xc0,0x00,0x11,0x08,0x00,0x08,0x00,0x08,0x03,
    0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01,0xff,0xc4,0x00,0x1f,0x00,
    0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
    0xff,0xc4,0x00,0xb5,0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,
    0x05,0x04,0x04,0x00,0x00,0x01,0x7d,0x01,0x02,0x03,0x00,0x04,0x11,0x05,
    0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,
    0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,
    0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,
    0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,
    0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,
    0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,
    0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,
    0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,
    0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,
    0xfa,0xff,0xc4,0x00,0x1f,0x01,0x00,0x03,0x01,0x01,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,
    0x06,0x07,0x08,0x09,0x0a,0x0b,0xff,0xc4,0x00,0xb5,0x11,0x00,0x02,0x01,
    0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,0x04,0x00,0x01,0x02,0x77,0x00,
    0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,
    0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,
    0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,
    0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,
    0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,
    0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,
    0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,
    0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,
    0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,
    0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,
    0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,
    0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,
    0x02,0x11,0x03,0x11,0x00,0x3f,0x00,0xe5,0x3e,0x0b,0xfe,0xc8,0x7f,0xea,
    0x3f,0xd0,0xbd,0x3f,0x86,0x8a,0x28,0xaa,0xc2,0x62,0x6a,0xfb,0x25,0xa9,
    0xd5,0xc0,0x7c,0x6b,0x9d,0x7f,0x62,0xd3,0xfd,0xef,0xf5,0xf7,0x9f,0xff,
    0xd9,
};
static const unsigned char webp8[84] = {
    0x52,0x49,0x46,0x46,0x4c,0x00,0x00,0x00,0x57,0x45,0x42,0x50,0x56,0x50,
    0x38,0x20,0x40,0x00,0x00,0x00,0xd0,0x01,0x00,0x9d,0x01,0x2a,0x08,0x00,
    0x08,0x00,0x01,0x40,0x26,0x25,0xa8,0x02,0x74,0x01,0x0f,0x0c,0x06,0xc5,
    0xe0,0x00,0xfe,0xfc,0xd2,0xfe,0x92,0x7b,0xb2,0xf7,0x72,0xc7,0x79,0xa0,
    0xf3,0x66,0x26,0x95,0xf3,0x4f,0xff,0xf5,0x1f,0xcd,0x05,0xc9,0x25,0xfe,
    0xff,0x9f,0xb4,0x46,0x43,0xff,0x80,0xff,0xa2,0x97,0x0c,0x00,0x00,0x00,
};
static const unsigned char gif8[77] = {
    0x47,0x49,0x46,0x38,0x39,0x61,0x08,0x00,0x08,0x00,0x81,0x00,0x00,0xff,
    0x00,0x00,0x00,0xff,0x00,0x00,0x00,0xff,0xff,0xff,0x00,0x21,0xf9,0x04,
    0x01,0x00,0x00,0x01,0x00,0x2c,0x00,0x00,0x00,0x00,0x08,0x00,0x08,0x00,
    0x00,0x08,0x1e,0x00,0x01,0x04,0x10,0x30,0x40,0x20,0xc1,0x81,0x05,0x11,
    0x02,0x20,0x68,0x30,0x61,0x42,0x86,0x03,0x1b,0x36,0x54,0xa8,0x10,0x22,
    0xc4,0x87,0x0f,0x03,0x02,0x00,0x3b,
};

static int have_fifo;      /* set when mkfifo worked (some filesystems refuse one) */
static int readable_test;  /* set when we are not root, so chmod 0000 really denies */

static void make_tree(void)
{
    snprintf(root, sizeof root, "/tmp/kai-term-fm-%ld", (long)getpid());
    if (mkdir(root, 0755) != 0) {
        printf("cannot create %s: %s\n", root, strerror(errno));
        fails++;
        return;
    }
    char p[1024];
    const char *dirs[] = { "Zdir", "adir", "many", "noread" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        p_join(p, sizeof p, dirs[i]);
        if (mkdir(p, 0755) != 0) {
            printf("cannot create %s: %s\n", p, strerror(errno));
            fails++;
        }
    }
    p_join(p, sizeof p, "adir/inside.txt");
    FILE *f = fopen(p, "wb");
    if (f) { fputs("inside\n", f); fclose(f); }

    wstr("file.txt", "hello world\nsecond line\n");
    /* The preview-pane selection group's file: a hyphenated word, trailing punctuation, two full-width
       glyphs, a word that starts with punctuation, and a path. Every span the new group pins was read out of
       this content by .scratch/s115/selprobe2 before the CHECK was written. */
    wstr("words.txt", "two-three, four.\n"
                       "\xe4\xbd\xa0\xe5\xa5\xbd \xe4\xb8\x96\xe7\x95\x8c\n"
                       "/etc/passwd (a path)\n"
                       "\xe5\x85\x83\xe3\x80\x82\n");   /* 元。 - a full-width mark ends this line */
    /* One 130-character line (the alphabet five times). The preview CUTS a long line at the pane's right edge
       rather than wrapping it, so this is the file that pins what a row gesture can reach in a line wider than
       the pane — .scratch/s116/longline printed the numbers before the CHECK was written. */
    wstr("long.txt", "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                     "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"
                     "abcdefghijklmnopqrstuvwxyz\n");
    wstr("code.c",
         "#include <stdio.h>\n"
         "int main(void) { /* greet */ printf(\"hi\"); return 0; }\n");
    wstr("data.json", "{\"key\": 1, \"b\": true}\n");
    wstr("notes.md", "# Title\nplain\n");
    wstr("script.py", "def f(x):  # hi\n    return x\n");
    wstr("Makefile", "CC = gcc\nall:\n\techo hi\n");
    wfile("empty.txt", "", 0);
    wstr(".hidden", "secret\n");
    /* A NUL inside the first 512 bytes is what makes fm_preview_build() call a file binary. */
    {
        static const unsigned char elf[] = {
            0x7f, 'E', 'L', 'F', 0, 1, 2, 3, 'g', 'a', 'r', 'b', 'a', 'g', 'e', 0
        };
        wfile("binary.bin", elf, sizeof elf);
    }
    wstr("notimg.png", "this is text with a png name\n");
    /* PNG magic followed by garbage: the sniff says image, the decoder must refuse it. */
    {
        unsigned char bad[16];
        memcpy(bad, png8, 8);
        memcpy(bad + 8, "garbage!", 8);
        wfile("broken.png", bad, sizeof bad);
    }
    wfile("img.png", png8, sizeof png8);
    wfile("img.jpg", jpg8, sizeof jpg8);
    wfile("img.webp", webp8, sizeof webp8);
    wfile("img.gif", gif8, sizeof gif8);
    for (int i = 0; i < 9; i++) {
        char rel[64];
        snprintf(rel, sizeof rel, "many/f%d", i);
        wfile(rel, "x", 1);
    }
    /* A text file longer than any preview pane the tests build — what the wheel over the preview pane
       exists for (s96). Every line is a 3-character label the painting checks look for by name. */
    {
        char big[40 * 8];
        size_t o = 0;
        for (int i = 1; i <= 40; i++)
            o += (size_t)snprintf(big + o, sizeof big - o, "L%02d\n", i);
        wstr("many.txt", big);
    }
    p_join(p, sizeof p, "exec.sh");
    f = fopen(p, "wb");
    if (f) { fputs("#!/bin/sh\necho hi\n", f); fclose(f); }
    chmod(p, 0755);

    p_join(p, sizeof p, "link_ok");
    if (symlink("adir", p) != 0) { printf("symlink: %s\n", strerror(errno)); fails++; }
    p_join(p, sizeof p, "link_bad");
    if (symlink("no-such-target", p) != 0) { printf("symlink: %s\n", strerror(errno)); fails++; }

    p_join(p, sizeof p, "fifo");
    have_fifo = (mkfifo(p, 0644) == 0);

    readable_test = (geteuid() != 0);
    if (readable_test) {
        p_join(p, sizeof p, "noread");
        chmod(p, 0000);
    }
}

/* ---------- formatters ---------- */

static void test_formatters(void)
{
    char s[64];
    fm_human_size(0, s, sizeof s);
    CHECK(!strcmp(s, "0 B"), "fm_human_size: 0");
    fm_human_size(999, s, sizeof s);
    CHECK(!strcmp(s, "999 B"), "fm_human_size: below 1 KiB stays bytes");
    fm_human_size(1024, s, sizeof s);
    CHECK(!strcmp(s, "1.0 KiB"), "fm_human_size: 1024");
    fm_human_size(1536, s, sizeof s);
    CHECK(!strcmp(s, "1.5 KiB"), "fm_human_size: 1536");
    fm_human_size(12345, s, sizeof s);
    CHECK(!strcmp(s, "12.1 KiB"), "fm_human_size: 12345 (was 1.2 KiB before the tenth math was fixed)");
    fm_human_size(1023, s, sizeof s);
    CHECK(!strcmp(s, "1023 B"), "fm_human_size: 1023 does not round up to 1.0 KiB");
    fm_human_size(1048576, s, sizeof s);
    CHECK(!strcmp(s, "1.0 MiB"), "fm_human_size: 1 MiB");
    fm_human_size(1099511627776ull, s, sizeof s);
    CHECK(!strcmp(s, "1.0 TiB"), "fm_human_size: 1 TiB");
    fm_human_size(1500000000000000ull, s, sizeof s);
    CHECK(strstr(s, "PiB") != NULL, "fm_human_size: reaches PiB");

    char m[11];
    fm_mode_string(0100755u, m);
    CHECK(!strcmp(m, "-rwxr-xr-x"), "fm_mode_string: regular 0755");
    fm_mode_string((mode_t)(S_IFDIR | 0755), m);
    CHECK(!strcmp(m, "drwxr-xr-x"), "fm_mode_string: directory");
    fm_mode_string((mode_t)(S_IFLNK | 0777), m);
    CHECK(!strcmp(m, "lrwxrwxrwx"), "fm_mode_string: symlink");
    fm_mode_string((mode_t)(S_IFREG | 0644), m);
    CHECK(!strcmp(m, "-rw-r--r--"), "fm_mode_string: regular 0644");
    fm_mode_string((mode_t)(S_IFDIR | 01777), m);
    CHECK(!strcmp(m, "drwxrwxrwt"), "fm_mode_string: sticky directory");

    fm_human_time(0, s, sizeof s);
    CHECK(strlen(s) >= 10 && strchr(s, '-') != NULL, "fm_human_time: a date, not a raw count");
}

/* ---------- language choice and colouring ---------- */

static uint32_t hlc(FmHl *st, const char *line, size_t off)
{
    static uint32_t fg[512];
    size_t n = strlen(line);
    if (n > sizeof fg / sizeof fg[0])
        n = sizeof fg / sizeof fg[0];
    fm_hl_line(st, line, n, fg, BASE_FG);
    return off < n ? fg[off] : BASE_FG;
}

static void test_lang(void)
{
    CHECK(fm_lang_for_path("x.c") == FM_LANG_C, "lang: .c");
    CHECK(fm_lang_for_path("/a/b/h.h") == FM_LANG_C, "lang: .h with a directory in front");
    CHECK(fm_lang_for_path("s.py") == FM_LANG_PY, "lang: .py");
    CHECK(fm_lang_for_path("Makefile") == FM_LANG_MAKE, "lang: Makefile by name");
    CHECK(fm_lang_for_path(".gitignore") == FM_LANG_SH, "lang: .gitignore by name");
    CHECK(fm_lang_for_path("d.json") == FM_LANG_JSON, "lang: .json");
    CHECK(fm_lang_for_path("R.md") == FM_LANG_MD, "lang: .md");
    CHECK(fm_lang_for_path("no_extension") == FM_LANG_NONE, "lang: no extension");
    CHECK(fm_lang_for_path("pic.png") == FM_LANG_NONE, "lang: an image extension is not a text language");
    CHECK(fm_lang_for_path("a.CSS") == FM_LANG_CSS, "lang: extension case does not matter");

    FmHl st;
    fm_hl_init(&st, FM_LANG_C);
    const char *line = "int x = 5; /* c */ char *s = \"hi\"; // note";
    CHECK(hlc(&st, line, 0) == FM_TYPE_COL,
          "hl C: a basic type is coloured as a type (the split editors make, keyword vs type)");
    CHECK(hlc(&st, "return 0;", 0) == FM_KEY_COL, "hl C: keyword");
    CHECK(hlc(&st, line, 4) == BASE_FG, "hl C: a plain identifier keeps the base colour");
    CHECK(hlc(&st, line, 8) == FM_NUM_COL, "hl C: number");
    CHECK(hlc(&st, line, 11) == FM_CMT_COL, "hl C: block comment start");
    CHECK(hlc(&st, line, 17) == FM_CMT_COL, "hl C: block comment end on the same line");
    CHECK(hlc(&st, line, 19) == FM_TYPE_COL, "hl C: type name");
    CHECK(hlc(&st, line, 29) == FM_STR_COL, "hl C: string literal");
    CHECK(hlc(&st, line, 35) == FM_CMT_COL, "hl C: line comment");

    /* A block comment has no reason to stop at the end of a line, so the state must carry. */
    fm_hl_init(&st, FM_LANG_C);
    CHECK(hlc(&st, "int a; /* begin", 9) == FM_CMT_COL, "hl C: block comment opens");
    CHECK(hlc(&st, "still comment", 0) == FM_CMT_COL, "hl C: block comment continues on the next line");
    CHECK(hlc(&st, "still comment", 12) == FM_CMT_COL, "hl C: the whole continued line is a comment");
    CHECK(hlc(&st, "end */ int b;", 0) == FM_CMT_COL, "hl C: the closing line starts inside the comment");
    CHECK(hlc(&st, "end */ return b;", 7) == FM_KEY_COL,
          "hl C: code after the closing */ is coloured again");

    fm_hl_init(&st, FM_LANG_C);
    CHECK(hlc(&st, "#include <stdio.h>", 0) == FM_PP_COL, "hl C: preprocessor line");

    fm_hl_init(&st, FM_LANG_PY);
    CHECK(hlc(&st, "def f(x):  # hi", 0) == FM_KEY_COL, "hl py: keyword");
    CHECK(hlc(&st, "def f(x):  # hi", 11) == FM_CMT_COL, "hl py: hash comment");

    fm_hl_init(&st, FM_LANG_SH);
    CHECK(hlc(&st, "echo $HOME now", 6) == FM_NUM_COL, "hl sh: variable");
    CHECK(hlc(&st, "#!/bin/sh", 0) == FM_CMT_COL, "hl sh: shebang is a comment");

    fm_hl_init(&st, FM_LANG_JSON);
    CHECK(hlc(&st, "{\"key\": 1, \"b\": true}", 2) == FM_TYPE_COL, "hl json: a quoted key");
    CHECK(hlc(&st, "{\"key\": 1, \"b\": true}", 8) == FM_NUM_COL, "hl json: number");
    CHECK(hlc(&st, "{\"key\": 1, \"b\": true}", 16) == FM_KEY_COL, "hl json: true is a keyword");

    fm_hl_init(&st, FM_LANG_MD);
    CHECK(hlc(&st, "# Title", 0) == FM_KEY_COL, "hl md: heading");

    fm_hl_init(&st, FM_LANG_MAKE);
    CHECK(hlc(&st, "CC = gcc", 0) == FM_TYPE_COL, "hl make: variable");
    CHECK(hlc(&st, "\techo hi", 1) == BASE_FG, "hl make: a recipe line is not make syntax");

    fm_hl_init(&st, FM_LANG_INI);
    CHECK(hlc(&st, "[section]", 0) == FM_TYPE_COL, "hl ini: section");
    CHECK(hlc(&st, "key = 1", 0) == FM_KEY_COL, "hl ini: key");
    CHECK(hlc(&st, "; note", 0) == FM_CMT_COL, "hl ini: comment");

    fm_hl_init(&st, FM_LANG_YAML);
    CHECK(hlc(&st, "key: value", 0) == FM_KEY_COL, "hl yaml: key");

    fm_hl_init(&st, FM_LANG_HTML);
    CHECK(hlc(&st, "<b>hi</b>", 0) == FM_PP_COL, "hl html: tag");

    /* No language: every byte keeps the base colour. */
    fm_hl_init(&st, FM_LANG_NONE);
    CHECK(hlc(&st, "int x = 5; // note", 0) == BASE_FG, "hl none: nothing is coloured");
}

/* ---------- listing ---------- */

static void test_scan(void)
{
    char sub[1024];
    int real = count_dir(root);
    CHECK(real > 0, "the test tree exists");

    FmScan *s = fm_scan_build(root, 1);
    CHECK(s != NULL, "scan: a listing is produced");
    CHECK(s && s->n == real + 1,
          "scan: every entry is listed (plain opendir count) plus the .. row the model adds");
    CHECK(s && !s->err[0], "scan: a readable directory reports no error");
    CHECK(s && !s->truncated, "scan: a small directory is not truncated");
    CHECK(s && !strcmp(s->dir, root), "scan: the answer names the directory it scanned");
    CHECK(s && !strcmp(s->ent[0].name, "..") && s->ent[0].kind == FMK_DIR && !s->ent[0].hidden,
          "scan: .. is the first entry, is a directory, and is never hidden");
    CHECK(s && s->ent[0].mtime > 0 && (s->ent[0].mode & S_IFDIR) == S_IFDIR,
          "scan: .. carries the PARENT's own stat, not a made-up one");

    /* Directories first, then case-insensitive: Zdir sorts with the other directories, not after them. */
    int zdir = -1, adir = -1, first_file = -1;
    for (int i = 0; i < s->n; i++) {
        if (!strcmp(s->ent[i].name, "Zdir")) zdir = i;
        if (!strcmp(s->ent[i].name, "adir")) adir = i;
        if (!strcmp(s->ent[i].name, "file.txt")) first_file = i;
    }
    CHECK(adir == 1, "scan: the first entry under .. is a directory");
    CHECK(zdir > adir && zdir < first_file, "scan: directories come first, case-insensitively");
    CHECK(s->ent[adir].kind == FMK_DIR, "scan: a directory is FMK_DIR");
    CHECK(s->ent[adir].mode & 0755, "scan: the mode is kept");
    CHECK(s->ent[adir].mtime > 0, "scan: the mtime is kept");

    int hidden = -1, linkok = -1, linkbad = -1, exec = -1, empty = -1;
    for (int i = 0; i < s->n; i++) {
        if (!strcmp(s->ent[i].name, ".hidden")) hidden = i;
        if (!strcmp(s->ent[i].name, "link_ok")) linkok = i;
        if (!strcmp(s->ent[i].name, "link_bad")) linkbad = i;
        if (!strcmp(s->ent[i].name, "exec.sh")) exec = i;
        if (!strcmp(s->ent[i].name, "empty.txt")) empty = i;
    }
    CHECK(hidden >= 0 && s->ent[hidden].hidden, "scan: a dotfile is listed and marked hidden");
    CHECK(linkok >= 0 && s->ent[linkok].kind == FMK_SYMLINK,
          "scan: a symlink is a symlink, not what it points at");
    CHECK(linkbad >= 0 && s->ent[linkbad].kind == FMK_SYMLINK,
          "scan: a DANGLING symlink is still listed (lstat does not follow it)");
    CHECK(exec >= 0 && s->ent[exec].exec, "scan: an owner-executable file is marked");
    CHECK(empty >= 0 && s->ent[empty].size == 0, "scan: an empty file has size 0");
    if (have_fifo) {
        int fifo = -1;
        for (int i = 0; i < s->n; i++)
            if (!strcmp(s->ent[i].name, "fifo")) fifo = i;
        CHECK(fifo >= 0 && s->ent[fifo].kind == FMK_FIFO, "scan: a FIFO is FMK_FIFO");
    }

    /* The cap: truncation must be reported, not silently dropped. The .. row costs one slot — it is an
       entry like any other, and a listing that dropped it under a cap would hide the way out. */
    FmScan *t = fm_scan_build_n(root, 2, 5);
    CHECK(t && t->n == 5, "scan: max_entries is honoured");
    CHECK(t && t->truncated, "scan: a capped listing says so");
    CHECK(t && !strcmp(t->ent[0].name, ".."), "scan: a capped listing still starts with ..");
    fm_scan_free(t);

    /* The root has no parent to show, so it gets no .. row — the same move fm_parent() refuses. */
    FmScan *r = fm_scan_build("/", 6);
    CHECK(r && r->n > 0, "scan: / lists something");
    CHECK(r && strcmp(r->ent[0].name, "..") && !has_entry(r, ".."),
          "scan: / has no .. row");
    fm_scan_free(r);

    /* Failure paths. */
    FmScan *bad = fm_scan_build("/kai-term-no-such-directory", 3);
    CHECK(bad && bad->n == 0, "scan: a missing directory lists nothing");
    CHECK(bad && bad->err[0], "scan: a missing directory reports why");
    fm_scan_free(bad);

    p_join(sub, sizeof sub, "file.txt");
    FmScan *nota = fm_scan_build(sub, 4);
    CHECK(nota && nota->n == 0 && nota->err[0], "scan: a regular file is not a directory, and says so");
    fm_scan_free(nota);

    if (readable_test) {
        p_join(sub, sizeof sub, "noread");
        FmScan *denied = fm_scan_build(sub, 5);
        CHECK(denied && denied->n == 0 && denied->err[0],
              "scan: a directory we may not read lists nothing and reports the error");
        fm_scan_free(denied);
    }

    fm_scan_free(s);
}

/* ---------- navigation, selection, stale answers ---------- */

static void test_nav(void)
{
    FileMgr fm;
    fm_init(&fm, root);
    CHECK(!strcmp(fm.dir, root), "nav: the model starts in the requested directory");
    CHECK(fm.focus == FM_FOCUS_LIST && fm.preview_on, "nav: sane initial focus/preview state");
    /* The row the band opens ON is the row the first Return / Right acts on. Measured live (s76): it
       opened on the `..` row, so the first Right went UP a level instead of into a directory. */
    CHECK(fm.sel == 1, "init: a non-root directory opens on its first REAL row, not on the .. row");
    FileMgr r0;
    fm_init(&r0, "/");
    CHECK(r0.sel == 0, "init: / opens on row 0, because it has no .. row");
    fm_destroy(&r0);

    unsigned long seq = 0;
    CHECK(fm_go(&fm, root, &seq) == 1, "nav: the first listing is requested");
    unsigned long first = seq;
    FmScan *s = fm_scan_build(root, first);
    /* A second request for the same directory after a refresh, so the first answer is stale. */
    CHECK(fm_refresh(&fm, &seq) == 1, "nav: refresh issues a new request");
    CHECK(fm_install_scan(&fm, s) == 0, "nav: an answer to an older request is dropped");
    CHECK(fm.n == 0, "nav: a dropped answer changes nothing");

    FmScan *fresh = fm_scan_build(root, seq);
    CHECK(fm_install_scan(&fm, fresh) == 1, "nav: the newest answer is installed");
    CHECK(fm.n > 0 && fm.nvis < fm.n, "nav: the view hides the dotfile");
    CHECK(fm.have_listing, "nav: a listing is now installed");
    CHECK(fm_sel(&fm) == 1, "nav: a fresh listing selects the first REAL entry, not the .. row");
    CHECK(fm_count(&fm) == fm.nvis, "nav: fm_count is the view size");

    /* Asking for the directory we are already showing must not re-request it. */
    CHECK(fm_go(&fm, root, &seq) == 0, "nav: re-entering the current directory is a no-op");
    /* ... but a path that only LOOKS different is the same directory. (root + "/../" alone really is
       the parent, so the round trip has to name the last component again.) */
    const char *base = strrchr(root, '/');
    char messy[1024];
    snprintf(messy, sizeof messy, "%s//./../%s", root, base + 1);
    CHECK(fm_go(&fm, messy, &seq) == 0, "nav: //, /. and /a/../ collapse before the comparison");
    CHECK(!strcmp(fm.dir, root), "nav: the normalized path is what is stored");

    fm_set_size(&fm, 40, 10, 40, 10);
    int avail = 10 - 2;                       /* header + footer */
    CHECK(fm_sel(&fm) == 1 && fm.top == 0, "nav: fresh view starts at the top");
    fm_move(&fm, 1);
    CHECK(fm_sel(&fm) == 2, "nav: move down");
    fm_move(&fm, -1);
    CHECK(fm_sel(&fm) == 1, "nav: move up");
    fm_move(&fm, -5);
    CHECK(fm_sel(&fm) == 0, "nav: the selection cannot go above the .. row");
    fm_page(&fm, 1);
    CHECK(fm_sel(&fm) == avail, "nav: one page is one pane of entries");
    CHECK(fm.top == 1, "nav: the view scrolled to keep the selection visible");
    fm_end(&fm);
    CHECK(fm_sel(&fm) == fm.nvis - 1, "nav: End selects the last entry");
    CHECK(fm.top + avail > fm.sel, "nav: the last entry is visible after End");
    fm_home(&fm);
    CHECK(fm_sel(&fm) == 0 && fm.top == 0, "nav: Home goes back to the top");

    /* The selected entry and its path. */
    const FmEntry *e = fm_sel_entry(&fm);
    CHECK(e != NULL && !strcmp(e->name, ".."), "nav: Home lands on the .. row");
    char path[1024];
    CHECK(fm_entry_path(&fm, 0, path, sizeof path), "nav: the path of entry 0 is built");
    CHECK(!strcmp(path, "/tmp"), "nav: the .. row's path is the parent, not dir/.. left as text");
    CHECK(fm_entry_path(&fm, 9999, path, sizeof path) == 0 && path[0] == 0,
          "nav: an out-of-range position builds nothing");

    /* Enter on a directory descends; Enter on a file does nothing. */
    fm_select(&fm, find_pos(&fm, "adir"));
    CHECK(fm_sel_entry(&fm) && !strcmp(fm_sel_entry(&fm)->name, "adir"),
          "nav: the selection moved to a real directory");
    CHECK(fm_go_sel(&fm, &seq) == 1, "nav: Enter on a directory requests the new listing");
    char want[1024];
    snprintf(want, sizeof want, "%s/adir", root);
    CHECK(!strcmp(fm.dir, want), "nav: the model moved into adir");
    /* Until adir's listing lands, the entries in memory belong to root. Building a preview path out of
       them asked the worker to stat `<adir>/<a name from root>` — measured live (s76) on every single
       navigation (`fm: preview: cannot stat …/dirA: No such file or directory`). */
    CHECK(!fm.have_listing, "nav: the old listing no longer describes this directory");
    char ppath[1024];
    unsigned long pseq = 0;
    CHECK(fm_preview_request(&fm, &pseq, ppath, sizeof ppath) == 1 && !strcmp(ppath, want),
          "nav: while the new listing loads, the preview asked for is the directory itself");
    FmScan *as = fm_scan_build(fm.dir, seq);
    CHECK(fm_install_scan(&fm, as) == 1, "nav: adir's listing installs");
    CHECK(fm_count(&fm) == 2 && !strcmp(vis_at(&fm, 0)->name, ".."),
          "nav: the directory entered starts with its own .. row");
    CHECK(fm_count(&fm) == 2 && !strcmp(fm_sel_entry(&fm)->name, "inside.txt"),
          "nav: adir holds the one file created in it, and lands on it");
    CHECK(fm_go_sel(&fm, &seq) == 0, "nav: Enter on a plain file does not navigate");
    CHECK(fm_parent(&fm, &seq) == 1, "nav: Backspace goes to the parent");
    CHECK(!strcmp(fm.dir, root), "nav: the parent of root/adir is root");
    CHECK(fm_parent(&fm, &seq) == 1 && !strcmp(fm.dir, "/tmp"), "nav: and again to /tmp");
    CHECK(fm_parent(&fm, &seq) == 1 && !strcmp(fm.dir, "/"), "nav: the parent of /tmp is /");
    CHECK(fm_parent(&fm, &seq) == 0, "nav: / has no parent");

    /* An empty directory holds nothing but the .. row, and that row is a way out, not a dead end. */
    FileMgr e2;
    fm_init(&e2, "/tmp");
    char emptydir[512];
    snprintf(emptydir, sizeof emptydir, "/tmp/kai-term-fm-empty-%ld", (long)getpid());
    mkdir(emptydir, 0755);
    unsigned long es = 0;
    fm_go(&e2, emptydir, &es);
    FmScan *es_scan = fm_scan_build(emptydir, es);
    fm_install_scan(&e2, es_scan);
    CHECK(fm_count(&e2) == 1 && !strcmp(fm_sel_entry(&e2)->name, ".."),
          "nav: an empty directory still offers the .. row");
    CHECK(fm_sel(&e2) == 0, "nav: and selects it, because there is nothing else");
    CHECK(fm_go_sel(&e2, &es) == 1 && !strcmp(e2.dir, "/tmp"),
          "nav: Enter on .. goes up one level");
    fm_destroy(&e2);
    rmdir(emptydir);

    /* A model with no listing at all (the directory could not be read) has no selection, and every
       navigation on it is a no-op rather than a crash. */
    FileMgr e3;
    fm_init(&e3, "/kai-term-no-such-directory");
    CHECK(fm_count(&e3) == 0 && fm_sel(&e3) == -1, "nav: an unreadable directory has no selection");
    fm_move(&e3, 1);
    fm_page(&e3, 1);
    fm_end(&e3);
    CHECK(fm_sel(&e3) == -1, "nav: navigation on an empty view stays empty (no crash, no wrap)");
    CHECK(fm_go_sel(&e3, &es) == 0, "nav: Enter with nothing selected does nothing");
    fm_destroy(&e3);

    fm_destroy(&fm);
}

/* ---------- previews ---------- */

static FmPreview *pv_of(const char *rel, unsigned long seq)
{
    char p[1024];
    p_join(p, sizeof p, rel);
    return fm_preview_build(p, seq);
}

static void test_preview(void)
{
    FmPreview *p = pv_of("", 1);          /* the directory itself */
    CHECK(p && p->kind == FMP_DIR, "preview: a directory previews as a directory");
    CHECK(p && p->n == count_dir(root) + 1,
          "preview: a directory preview lists its entries (and the .. row it starts with)");
    CHECK(p && p->nmeta >= 3, "preview: metadata is built for every kind");
    int found_type = 0;
    if (p)
        for (int i = 0; i < p->nmeta; i++)
            if (!strncmp(p->meta[i], "type", 4)) found_type = 1;
    CHECK(found_type, "preview: the metadata says what the entry is");
    fm_preview_free(p);

    p = pv_of("code.c", 2);
    CHECK(p && p->kind == FMP_TEXT, "preview: a source file previews as text");
    CHECK(p && p->lang == FM_LANG_C, "preview: the language comes from the name");
    CHECK(p && p->text && strstr(p->text, "int main") != NULL, "preview: the text is really the file");
    CHECK(p && !p->truncated, "preview: a small file is not truncated");
    fm_preview_free(p);

    p = pv_of("empty.txt", 3);
    CHECK(p && p->kind == FMP_TEXT && p->len == 0, "preview: an empty file is empty text, not an error");
    fm_preview_free(p);

    p = pv_of("binary.bin", 4);
    CHECK(p && p->kind == FMP_META, "preview: a binary file falls back to metadata");
    CHECK(p && strstr(p->err, "binary") != NULL, "preview: and says why");
    fm_preview_free(p);

    p = pv_of("notimg.png", 5);
    CHECK(p && p->kind == FMP_TEXT,
          "preview: a text file with a .png name is shown as text - the bytes decide, not the name");
    fm_preview_free(p);

    p = pv_of("broken.png", 6);
    CHECK(p && p->kind == FMP_META, "preview: an undecodable image falls back to metadata");
    CHECK(p && p->err[0], "preview: and reports the decode failure");
    fm_preview_free(p);

    struct { const char *name; const char *fmt; } imgs[] = {
        { "img.png", "png" }, { "img.jpg", "jpg" }, { "img.webp", "webp" }, { "img.gif", "gif" },
    };
    for (size_t i = 0; i < sizeof imgs / sizeof imgs[0]; i++) {
        p = pv_of(imgs[i].name, 7);
        CHECK(p && p->kind == FMP_IMAGE, "preview: the image is decoded, not described");
        CHECK(p && p->w == 8 && p->h == 8, "preview: at its real size");
        CHECK(p && p->rgba != NULL, "preview: and its pixels are kept");
        CHECK(p && !strcmp(p->fmt, imgs[i].fmt), "preview: the format comes from the bytes");
        fm_preview_free(p);
    }

    p = pv_of("no-such-entry", 8);
    CHECK(p && p->kind == FMP_META && strstr(p->err, "cannot stat"),
          "preview: a file that vanished between listing and preview is metadata with an error");
    fm_preview_free(p);

    if (have_fifo) {
        p = pv_of("fifo", 9);
        CHECK(p && p->kind == FMP_META && strstr(p->err, "not a regular file"),
              "preview: a FIFO is never read (that would block the preview forever)");
        fm_preview_free(p);
    }

    if (readable_test) {
        p = pv_of("noread", 10);
        CHECK(p && p->kind == FMP_DIR && p->n == 0 && p->err[0],
              "preview: an unreadable directory previews as an empty one with the error");
        fm_preview_free(p);
    }

    /* Which preview the model wants, and the stale-answer rule. */
    FileMgr fm;
    fm_init(&fm, root);
    fm_set_size(&fm, 40, 12, 40, 12);
    unsigned long seq = 0;
    fm_go(&fm, root, &seq);
    FmScan *s = fm_scan_build(root, seq);
    fm_install_scan(&fm, s);

    char want[1024];
    CHECK(fm_preview_request(&fm, &seq, want, sizeof want) == 1, "preview: a request is issued");
    char sel_path[1024];
    fm_entry_path(&fm, fm_sel(&fm), sel_path, sizeof sel_path);
    CHECK(!strcmp(want, sel_path), "preview: the request names the selected entry");
    FmPreview *first = fm_preview_build(want, seq);
    CHECK(fm_install_preview(&fm, first) == 1, "preview: the newest answer installs");
    CHECK(fm_preview_request(&fm, &seq, want, sizeof want) == 0,
          "preview: nothing is re-requested while the selection has not moved");

    FmPreview *stale = fm_preview_build(root, 1);
    CHECK(fm_install_preview(&fm, stale) == 0, "preview: an answer to an older request is dropped");

    fm_move(&fm, 1);
    CHECK(fm_preview_request(&fm, &seq, want, sizeof want) == 1,
          "preview: moving the selection asks for a new preview");
    fm_refresh(&fm, &seq);
    CHECK(fm_preview_request(&fm, &seq, want, sizeof want) == 1,
          "preview: a refresh re-previews the same path (its content may have changed)");

    fm_destroy(&fm);
}

/* ---------- painting ---------- */

static uint32_t pch(const FmPane *p, int x, int y) { return p->cell[y * p->cols + x].ch; }
static uint32_t pbg(const FmPane *p, int x, int y) { return p->cell[y * p->cols + x].bg; }
static uint32_t pfg(const FmPane *p, int x, int y) { return p->cell[y * p->cols + x].fg; }

static void test_paint(void)
{
    FileMgr fm;
    fm_init(&fm, root);
    fm_set_size(&fm, 40, 12, 40, 12);
    CHECK(fm.list.cell && fm.list.cols == 40 && fm.list.rows == 12, "paint: the panes are allocated");
    fm_set_size(&fm, 30, 8, 20, 6);
    CHECK(fm.list.cols == 30 && fm.list.rows == 8 && fm.prev.cols == 20 && fm.prev.rows == 6,
          "paint: a resize really reallocs (the first draft compared the new size to itself)");
    fm_set_size(&fm, 40, 12, 40, 12);

    unsigned long seq = 0;
    fm_go(&fm, root, &seq);
    FmScan *s = fm_scan_build(root, seq);
    fm_install_scan(&fm, s);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(!fm.dirty, "paint: painting clears the dirty flag");

    CHECK(pch(&fm.list, 1, 0) == '/', "paint: the header row shows the directory path");
    const FmEntry *e = fm_sel_entry(&fm);
    CHECK(e != NULL, "paint: something is selected");
    /* Which ROW the painter drew the selection on is the painter's business: entries start below the
       header and the pane shows a window of the view starting at `top`. Derive it, never hard-code it. */
    int selrow = 1 + (fm.sel - fm.top);
    CHECK(pbg(&fm.list, 0, selrow) == SEL_BG && pbg(&fm.list, 20, selrow) == SEL_BG,
          "paint: the selected row is highlighted across its whole width");
    CHECK(selrow + 1 < fm.list.rows - 1 && pbg(&fm.list, 0, selrow + 1) == BASE_BG,
          "paint: an unselected row is not");
    /* The highlight is the value the CALLER passed, not the SEL_BG macro: that is what lets a theme or a
       `selection_color =` line change the band too. Written as a different colour on purpose — passing
       SEL_BG and expecting SEL_BG would also pass if fm.c ignored its argument and used the macro, which
       is exactly how the dead `cursor_color =` key stayed invisible (ISSUES.md). */
    fm_paint(&fm, BASE_FG, BASE_BG, 0xff00ff00u);
    CHECK(pbg(&fm.list, 0, selrow) == 0xff00ff00u && pbg(&fm.list, 20, selrow) == 0xff00ff00u,
          "paint: the selected row uses the sel colour it was handed, not the SEL_BG macro");
    CHECK(pbg(&fm.list, 0, selrow + 1) == BASE_BG,
          "paint: a custom sel colour still leaves the unselected rows alone");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);   /* restore what the checks below assume */
    CHECK(pbg(&fm.list, 0, selrow) == SEL_BG, "paint: repainting with the default restores the highlight");
    CHECK(pch(&fm.list, 1, selrow) == (uint32_t)e->name[0], "paint: the selected name starts at column 1");
    CHECK(pch(&fm.list, 2, selrow) == (uint32_t)e->name[1], "paint: and continues cell by cell");
    CHECK(pfg(&fm.list, 1, selrow) == FM_DIR_COL, "paint: a directory name is drawn in the directory colour");
    CHECK(pbg(&fm.list, 1, selrow) == SEL_BG, "paint: the glyph keeps the row's highlight as its background");
    /* The .. row is drawn like the directory it is, and it is the first row of the pane. */
    CHECK(fm.top == 0 && !strcmp(vis_at(&fm, 0)->name, "..") &&
          pch(&fm.list, 1, 1) == '.' && pch(&fm.list, 2, 1) == '.' && pfg(&fm.list, 1, 1) == FM_DIR_COL,
          "paint: the .. row is the first entry row, drawn as a directory");

    int footer_ink = 0;
    for (int x = 0; x < fm.list.cols; x++)
        if (pch(&fm.list, x, fm.list.rows - 1))
            footer_ink = 1;
    CHECK(footer_ink, "paint: the footer row says something");

    /* A preview of text: the keyword colour must reach the cells. The selection MOVES to code.c so the
       preview goes through the same path main.c will use - move, request a fresh id, build, install. */
    char path[1024];
    char path2[1024];
    fm_preview_request(&fm, &seq, path, sizeof path);
    FmPreview *pdir = fm_preview_build(path, seq);
    fm_install_preview(&fm, pdir);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    int dir_ink = 0;
    for (int y = 0; y < fm.prev.rows; y++)
        for (int x = 0; x < fm.prev.cols; x++)
            if (pch(&fm.prev, x, y))
                dir_ink++;
    CHECK(dir_ink > 10, "paint: a directory preview paints its contents");

    /* code.c's second line is 54 columns, so the preview pane has to be wider than that for the
       keyword in it to have a cell to land in - paint_preview_text() cuts a line at the pane edge
       (pinned below), it does not wrap it. */
    fm_set_size(&fm, 40, 12, 80, 12);
    int cpos = find_pos(&fm, "code.c");
    CHECK(cpos >= 0, "paint: the test tree has code.c to preview");
    fm_select(&fm, cpos);
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1,
          "paint: moving the selection asks for a new preview");
    FmPreview *ptxt = fm_preview_build(path, seq);
    CHECK(fm_install_preview(&fm, ptxt) == 1, "paint: and its answer installs");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    int keyword_cells = 0, string_cells = 0;
    for (int y = 0; y < fm.prev.rows; y++)
        for (int x = 0; x < fm.prev.cols; x++) {
            if (pfg(&fm.prev, x, y) == FM_KEY_COL && pch(&fm.prev, x, y))
                keyword_cells++;
            if (pfg(&fm.prev, x, y) == FM_STR_COL && pch(&fm.prev, x, y))
                string_cells++;
        }
    CHECK(keyword_cells == 6, "paint: syntax highlighting reaches the cells (`return` = 6 cells)");
    CHECK(string_cells == 4, "paint: and so does the string literal (`\"hi\"` = 4 cells)");

    /* The same preview in a narrow pane: the line is CUT at the right edge and does NOT continue on
       the next row. A preview pane that wrapped would push the metadata and the rest of the file off.
       Rows here: 0 name, 1 path+kind, 2 `#include <stdio.h>`, 3 the 54-column line, 4 nothing. */
    fm_set_size(&fm, 40, 12, 20, 12);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    int r3 = 0, r4 = 0;
    for (int x = 0; x < fm.prev.cols; x++) {
        if (pch(&fm.prev, x, 3))
            r3++;
        if (pch(&fm.prev, x, 4))
            r4++;
    }
    CHECK(r3 == fm.prev.cols - 2, "paint: a line longer than the pane fills it to the right edge");
    CHECK(r4 == 0, "paint: and is cut there - the text preview does not wrap");
    fm_set_size(&fm, 40, 12, 40, 12);

    /* A preview of a binary/undecodable file: metadata lines, no crash. */
    fm_preview_request(&fm, &seq, path, sizeof path);
    p_join(path2, sizeof path2, "broken.png");
    FmPreview *pbad = fm_preview_build(path2, seq);
    fm_install_preview(&fm, pbad);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    int err_ink = 0;
    for (int y = 0; y < fm.prev.rows; y++)
        for (int x = 0; x < fm.prev.cols; x++)
            if (pfg(&fm.prev, x, y) == FM_ERR_COL && pch(&fm.prev, x, y))
                err_ink = 1;
    CHECK(err_ink, "paint: a failed preview shows its error line in the error colour");

    /* Zero-size panes must be paintable (a window can be resized to almost nothing). */
    fm_set_size(&fm, 0, 0, 0, 0);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(fm.list.cell == NULL || fm.list.cols == 0, "paint: a 0x0 pane is skipped, not drawn");
    fm_set_size(&fm, 3, 2, 3, 2);      /* too short for header + footer: the entries win */
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pch(&fm.list, 1, 0) == (uint32_t)vis_at(&fm, fm.top)->name[0],
          "paint: a pane too short for its chrome still shows the entries, starting at the view's top");

    fm_destroy(&fm);
}

/* ---------- the preview pane's own scroll (s96) ---------- */

static void pane_row_text(const FmPane *p, int y, char *out, size_t cap)
{
    size_t o = 0;
    for (int x = 0; x < p->cols && o + 8 < cap; x++)
        if (p->cell[y * p->cols + x].ch)
            o += (size_t)utf8_encode(p->cell[y * p->cols + x].ch, out + o);
    out[o] = 0;
}

/* Which row of the preview pane shows WHAT, or -1. The test asks the pane what it PAINTS, so a scroll
   that only moved a field and not the pixels cannot pass. */
static int pane_shows(const FileMgr *fm, const char *what)
{
    char line[4096];
    for (int y = 0; y < fm->prev.rows; y++) {
        pane_row_text(&fm->prev, y, line, sizeof line);
        if (strstr(line, what))
            return y;
    }
    return -1;
}

static void test_preview_scroll(void)
{
    FileMgr fm;
    fm_init(&fm, root);
    fm_set_size(&fm, 40, 12, 30, 12);
    unsigned long seq = 0;
    fm_go(&fm, root, &seq);
    fm_install_scan(&fm, fm_scan_build(root, seq));

    char path[1024];
    int pos = find_pos(&fm, "many.txt");
    CHECK(pos >= 0, "pvscroll: the tree has many.txt to preview");
    fm_select(&fm, pos);
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvscroll: a preview is asked for");
    CHECK(fm_install_preview(&fm, fm_preview_build(path, seq)) == 1, "pvscroll: and it installs");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);

    /* 40 lines, a 12-row pane, 2 header rows: 10 shown, 30 to scroll through. */
    CHECK(fm_preview_scroll_max(&fm) == 40 - (12 - 2), "pvscroll: max is what the pane cannot show");
    CHECK(pane_shows(&fm, "L01") >= 0, "pvscroll: the first line is painted before any scroll");
    CHECK(pane_shows(&fm, "L40") < 0, "pvscroll: and the last one is not");

    fm_preview_scroll(&fm, 1);
    CHECK(fm.pv_scroll == 1, "pvscroll: the wheel moves the body down one line");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "L01") < 0 && pane_shows(&fm, "L02") >= 0,
          "pvscroll: the painter obeys it - L02 is the new top line");

    fm_preview_scroll(&fm, -1);
    CHECK(fm.pv_scroll == 0, "pvscroll: wheel up walks back");
    fm_preview_scroll(&fm, -100);
    CHECK(fm.pv_scroll == 0, "pvscroll: never above the first line");
    fm_preview_scroll(&fm, 1000);
    CHECK(fm.pv_scroll == fm_preview_scroll_max(&fm), "pvscroll: never past the last line");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "L40") >= 0, "pvscroll: the last line is reachable");

    /* The resize clamp bites when the pane GROWS: a taller pane shows more of the body, so the range
       shrinks and a body scrolled to the OLD bottom would be painted into blank rows. The other direction
       is the counter-intuitive one — a SHORTER pane has MORE to scroll (40 lines in a 6-row pane is 36
       rows of range, not less) — so a check written the obvious way round passes with no clamp at all. */
    fm_set_size(&fm, 40, 12, 30, 40);
    CHECK(fm_preview_scroll_max(&fm) == 40 - (40 - 2), "pvscroll: a taller pane has less left to scroll");
    CHECK(fm.pv_scroll == 2, "pvscroll: and the resize pulls the scroll back inside what it can show");
    fm_set_size(&fm, 40, 12, 30, 6);
    CHECK(fm_preview_scroll_max(&fm) == 40 - (6 - 2), "pvscroll: a shorter pane has more to scroll");
    CHECK(fm.pv_scroll == 2, "pvscroll: growing the range does not move the body by itself");
    fm_set_size(&fm, 40, 12, 30, 12);

    /* A new preview starts at the beginning, however far the last one was scrolled. */
    fm_preview_scroll(&fm, -1000);
    CHECK(fm.pv_scroll == 0, "pvscroll: back to the top first, so the reset below means something");
    fm_preview_scroll(&fm, 5);
    CHECK(fm.pv_scroll == 5, "pvscroll: scrolled, to prove the reset below means something");
    fm_select(&fm, find_pos(&fm, "file.txt"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1,
          "pvscroll: selecting another file asks for a new preview");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    CHECK(fm.pv_scroll == 0, "pvscroll: and the new preview starts at its beginning");

    /* A preview with no body — metadata only — has nothing to scroll, and scrolling it is a no-op
       rather than a jump into blank rows. */
    p_join(path, sizeof path, "broken.png");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    CHECK(fm_preview_scroll_max(&fm) == 0, "pvscroll: a metadata-only preview has nothing to scroll");
    fm_preview_scroll(&fm, 5);
    CHECK(fm.pv_scroll == 0, "pvscroll: and scrolling it does nothing");

    /* A directory preview scrolls too: its body is the entry list. */
    fm_set_size(&fm, 40, 12, 30, 5);
    fm_select(&fm, find_pos(&fm, "many"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvscroll: a directory preview is asked for");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    int body = fm.pv->n + 1;                 /* the entries (the .. row included) + the count line */
    CHECK(fm_preview_scroll_max(&fm) == body - (5 - 2),
          "pvscroll: a directory preview's body is its entries plus the count line");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "f0") >= 0, "pvscroll: the first entry is painted");
    CHECK(pane_shows(&fm, "f8") < 0, "pvscroll: the last one is below the fold");
    fm_preview_scroll(&fm, 100);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "f8") >= 0, "pvscroll: and the wheel reaches it");

    fm_destroy(&fm);
}

/* The same body, driven by KEYS while the preview pane owns the keyboard (main.c routes the band's
   Page_Up/Page_Down/Home/End here when focus == FM_FOCUS_PREVIEW). What these pin is the part the wheel
   does not exercise: what ONE page is, and that Home/End mean the first and last line of the BODY.
   A page must be exactly what the pane shows at once — one row short and a held Page key re-shows a
   line, one row long and it skips one, and both are invisible until the pixels are checked. */
static void test_preview_keys(void)
{
    FileMgr fm;
    fm_init(&fm, root);
    fm_set_size(&fm, 40, 12, 30, 12);
    unsigned long seq = 0;
    fm_go(&fm, root, &seq);
    fm_install_scan(&fm, fm_scan_build(root, seq));

    char path[1024];
    fm_select(&fm, find_pos(&fm, "many.txt"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvkeys: a text preview to scroll");
    CHECK(fm_install_preview(&fm, fm_preview_build(path, seq)) == 1, "pvkeys: and it installs");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);

    /* 12-row pane, 2 header rows: one page is the 10 body rows, and 40 lines leave 30 rows to scroll. */
    CHECK(fm_preview_page_rows(&fm) == 12 - 2, "pvkeys: one page is the body the pane shows at once");
    CHECK(fm_preview_scroll_max(&fm) == 40 - (12 - 2), "pvkeys: and 30 rows are below the fold");

    fm_preview_page(&fm, 1);
    CHECK(fm.pv_scroll == 10, "pvkeys: Page_Down moves by a whole page, not by a row or by a guess");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "L11") >= 0 && pane_shows(&fm, "L10") < 0,
          "pvkeys: the page is contiguous - L11 is the new top, no line shown twice");
    fm_preview_page(&fm, 1);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "L21") >= 0 && pane_shows(&fm, "L20") < 0,
          "pvkeys: and the second page continues where the first stopped");
    fm_preview_page(&fm, -2);
    CHECK(fm.pv_scroll == 0, "pvkeys: Page_Up the same number of pages is back to the top");

    /* End / Home: the last line and the first line of the body. */
    fm_preview_bottom(&fm);
    CHECK(fm.pv_scroll == fm_preview_scroll_max(&fm), "pvkeys: End lands on the last line of the body");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "L40") >= 0, "pvkeys: and the last line is really painted");
    fm_preview_top(&fm);
    CHECK(fm.pv_scroll == 0, "pvkeys: Home goes back to the first line");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "L01") >= 0, "pvkeys: and the first line is really painted");

    /* Repeated End / Home are no-ops, and a page past either end clamps instead of wrapping. */
    fm_preview_bottom(&fm);
    fm_preview_bottom(&fm);
    CHECK(fm.pv_scroll == fm_preview_scroll_max(&fm), "pvkeys: End twice does not go past the bottom");
    fm_preview_page(&fm, -99);
    fm_preview_top(&fm);
    CHECK(fm.pv_scroll == 0, "pvkeys: and a page past the top does not wrap");

    /* A pane too short for its own header still answers a page of at least one row, so a Page key can
       never be a no-op on a body that has rows to show. */
    fm_set_size(&fm, 40, 12, 30, 2);
    CHECK(fm_preview_page_rows(&fm) == 1, "pvkeys: a 2-row pane (header only) still pages by one row");
    fm_set_size(&fm, 40, 12, 30, 12);

    /* A preview with no body — a picture that failed to decode, so metadata only — has nothing to
       scroll: the keys must not move a field that the painter never reads. */
    p_join(path, sizeof path, "broken.png");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    fm_preview_bottom(&fm);
    fm_preview_page(&fm, 1);
    CHECK(fm.pv_scroll == 0 && fm_preview_scroll_max(&fm) == 0,
          "pvkeys: a metadata-only preview does not move at all");

    /* A directory preview pages too — its body is the entry list. */
    fm_select(&fm, find_pos(&fm, "many"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvkeys: a directory preview is asked for");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    fm_preview_bottom(&fm);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "f8") >= 0, "pvkeys: End reaches the last entry of a directory preview");
    fm_preview_top(&fm);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "f0") >= 0, "pvkeys: and Home is back at the first one");

    fm_destroy(&fm);
}

/* ---------- the preview picture box ---------- */

/* Glyphs painted INSIDE the box fm_image_fit() reports. The renderer draws the picture into that box and
   emits the pane's glyphs in the batch AFTER it, so any glyph here is text drawn across the image — the
   bug the user reported on 2026-10-19 ("the metadata sometimes get written on the image preview"). */
static int ink_in_box(const FileMgr *fm, int col, int row, int cols, int rows)
{
    int ink = 0;
    for (int y = row; y < row + rows && y < fm->prev.rows; y++)
        for (int x = col; x < col + cols && x < fm->prev.cols; x++)
            if (pch(&fm->prev, x, y))
                ink++;
    return ink;
}

/* Rows in [from,to) that hold at least one glyph — how many metadata lines the painter really drew. */
static int ink_rows(const FileMgr *fm, int from, int to)
{
    int n = 0;
    for (int y = from; y < to && y < fm->prev.rows; y++) {
        int ink = 0;
        for (int x = 0; x < fm->prev.cols; x++)
            if (pch(&fm->prev, x, y))
                ink = 1;
        if (ink)
            n++;
    }
    return n;
}

static void test_fit(void)
{
    FileMgr fm;
    fm_init(&fm, root);
    fm_set_size(&fm, 40, 40, 40, 20);
    int c, r, cols, rows, pw, ph;
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(cols == 0 && rows == 0 && pw == 0 && ph == 0, "fit: no preview means no box");

    char path[1024];
    p_join(path, sizeof path, "img.png");
    FmPreview *p = fm_preview_build(path, 1);
    fm.pv_req = 1;
    CHECK(fm_install_preview(&fm, p) == 1, "fit: an 8x8 preview installs");
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(cols == 1 && rows == 1, "fit: a picture smaller than the pane is NOT enlarged");
    CHECK(pw == 8 && ph == 8, "fit: and its pixel box is its own size");
    /* 20 rows, 2 of header, 8 of metadata (path, type, size, mode, owner, group, modified, format): the
       8x8 px picture is centred in the 10 rows left, so row 14 — NOT row 11, which is where a box that
       ignored the metadata lines would sit. */
    CHECK(c == 19 && r == 14, "fit: and is centred in the area below the header AND the metadata");

    /* The direct pin of the user's complaint: paint the pane, ask where the picture goes, and require
       that no glyph of the pane sits in that box. Before the row budget was shared, the metadata lines
       from row 3 down were painted exactly there. */
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(ink_in_box(&fm, c, r, cols, rows) == 0,
          "fit: no painted glyph sits inside the box the picture is drawn into");
    int nmeta = p->nmeta;      /* install frees the preview it replaces, so keep the count */
    CHECK(r >= 2 + nmeta,
          "fit: with room to spare the picture starts below every metadata line");
    CHECK(ink_rows(&fm, 2, r) == nmeta,
          "fit: and every metadata line that fits above the picture is still drawn");

    /* The shape that actually overlapped before the row budget was shared: a picture big enough that its
       box starts at the TOP of the picture area, which is exactly where the metadata lines are painted.
       The pixels are the real img.png's; only the reported size is widened, because the fit is geometry
       and generating a 1000x500 PNG here would test the encoder instead. */
    FmPreview *wide = calloc(1, sizeof *wide);
    wide->kind = FMP_IMAGE;
    wide->w = 1000;
    wide->h = 500;
    wide->seq = 4;
    for (int i = 0; i < nmeta && i < FM_META_LINES; i++)
        snprintf(wide->meta[i], FM_META_WIDTH, "%s", p->meta[i]);
    wide->nmeta = nmeta;
    fm.pv_req = 4;
    CHECK(fm_install_preview(&fm, wide) == 1, "fit: a picture that fills the pane, with metadata, installs");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(rows == 10 && cols == 40 && r + rows <= 20,
          "fit: that picture takes the whole area below the metadata");
    CHECK(ink_in_box(&fm, c, r, cols, rows) == 0,
          "fit: a picture that fills the area has no metadata line drawn across it");
    CHECK(r == 2 + nmeta && ink_rows(&fm, 2, r) == nmeta,
          "fit: the metadata sits in the rows above it, all of them");
    fm_set_size(&fm, 40, 40, 40, 20);

    /* A pane too short for header + all the metadata + a row of picture: the metadata is cut back, the
       picture keeps its row, and the two still never share a cell. 5 rows = 2 header + 2 meta + 1 image. */
    fm_set_size(&fm, 40, 40, 40, 5);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(rows > 0 && r == 4, "fit: a short pane keeps the picture a row and gives the metadata only what is left");
    CHECK(ink_in_box(&fm, c, r, cols, rows) == 0,
          "fit: a short pane still paints no glyph across the picture");
    CHECK(ink_rows(&fm, 2, r) == 2, "fit: the metadata rows that fit are drawn, the rest are dropped");

    /* Header height exactly: every metadata line goes, the picture keeps its one row. */
    fm_set_size(&fm, 40, 40, 40, 3);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(rows > 0 && r == 2 && ink_rows(&fm, 2, r) == 0 && ink_in_box(&fm, c, r, cols, rows) == 0,
          "fit: a pane the height of its header drops ALL the metadata rather than covering the picture");

    /* No row left at all: no box, so nothing can overlap. */
    fm_set_size(&fm, 40, 40, 40, 2);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(cols == 0 && rows == 0 && pw == 0 && ph == 0,
          "fit: a pane with no row left for the picture reports no box");
    fm_set_size(&fm, 40, 40, 40, 20);

    /* A fabricated preview: the geometry must hold for any size, and generating a 1000x500 image
       file here would test the encoder, not the fit. It carries no metadata lines, so the picture
       area starts right below the 2-row header and is 18 rows tall. */
    FmPreview *big = calloc(1, sizeof *big);
    big->kind = FMP_IMAGE;
    big->w = 1000;
    big->h = 500;
    big->seq = 2;
    fm.pv_req = 2;
    CHECK(fm_install_preview(&fm, big) == 1, "fit: a wide preview installs");
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    CHECK(cols > 0 && rows > 0, "fit: a big picture gets a box");
    CHECK(cols <= 40 && r + rows <= 20, "fit: the box stays inside the pane");
    /* width-limited: 320 px of width fit, so the scale is 320/1000 and the height follows */
    CHECK(pw == 319 && ph == 159, "fit: the pixel box is the scaled picture, not the cell box");
    CHECK(pw <= cols * 8 && ph <= rows * 16, "fit: the pixel box never exceeds its cell box");
    /* aspect in PIXELS: 1000x500 is 2:1, and the box must be within a cell of it */
    double ar = (double)(cols * 8) / (double)(rows * 16);
    CHECK(ar > 1.6 && ar < 2.4, "fit: the aspect ratio is preserved in pixels, not cells");
    CHECK(c >= 0 && r >= 2, "fit: the box never covers the preview header");

    /* A tall picture in the same pane: height-limited by the 18 rows below the header. */
    FmPreview *tall = calloc(1, sizeof *tall);
    tall->kind = FMP_IMAGE;
    tall->w = 100;
    tall->h = 1000;
    tall->seq = 3;
    fm.pv_req = 3;
    fm_install_preview(&fm, tall);
    fm_image_fit(&fm, 8, 16, &c, &r, &cols, &rows, &pw, &ph);
    double ar2 = (double)(cols * 8) / (double)(rows * 16);
    CHECK(ar2 > 0.05 && ar2 < 0.15, "fit: a tall picture stays tall");
    CHECK(pw == 28 && ph == 287, "fit: a tall picture's pixel box is height-limited");
    CHECK(pw * 10 == ph * 100 / 100 * 10 || ((pw * 1000) / ph > 20 && (pw * 1000) / ph < 110),
          "fit: and keeps its 1:10 aspect in pixels");
    CHECK(cols <= 40 && r + rows <= 20, "fit: and still fits");

    fm_destroy(&fm);
}

/* ---------- the dotfile toggle and the click hit test (fm_set_hidden / fm_row_pos) ----------
   Both exist because main.c must not re-derive them: the toggle must not re-read the disk, and a click
   must land on the line it lands on. So the rules live in fm.c next to the painter, and these checks are
   what pin them. */

static void test_view_toggle(void)
{
    FileMgr fm;
    fm_init(&fm, root);

    unsigned long seq = 0;
    fm_go(&fm, root, &seq);
    FmScan *s = fm_scan_build(root, seq);
    CHECK(fm_install_scan(&fm, s) == 1, "toggle: the listing installs");
    int n = fm.n, nvis = fm.nvis;
    CHECK(nvis < n, "toggle: the view starts smaller than the listing (a dotfile is hidden)");
    CHECK(find_pos(&fm, ".hidden") < 0, "toggle: the dotfile is not in the view at first");

    fm_set_hidden(&fm, 1);
    CHECK(fm.nvis == n, "toggle: showing dotfiles adds every hidden entry back");
    CHECK(fm.n == n, "toggle: showing dotfiles does NOT re-scan (the listing is untouched)");
    CHECK(find_pos(&fm, ".hidden") >= 0, "toggle: the dotfile is in the view now");

    fm_set_hidden(&fm, 1);
    CHECK(fm.nvis == n, "toggle: setting the same value again changes nothing");

    /* The selection is a POSITION in the view, so it has to stay inside the new one. */
    fm_end(&fm);
    CHECK(fm_sel(&fm) == fm.nvis - 1, "toggle: End selects the last entry of the wide view");
    fm_set_hidden(&fm, 0);
    CHECK(fm.sel >= 0 && fm.sel < fm.nvis, "toggle: the selection is clamped into the narrow view");
    int clamped = fm.sel;
    CHECK(fm.top >= 0 && fm.top + 1 <= fm.nvis, "toggle: and the scroll position stays valid");
    fm_set_hidden(&fm, 1);
    /* Toggling back does NOT remember the old position: the selection is a POSITION in the view, so it
       stays where the clamp left it instead of jumping to an entry the user never moved to. */
    CHECK(fm_sel(&fm) == clamped, "toggle: showing them again keeps the clamped position");

    /* fm_row_pos(): the row -> entry map the painter implies. A 10-row pane has a header and a footer,
       so entries live on rows 1..8; a 3-row pane loses both and the entries start at row 0. */
    fm_set_hidden(&fm, 0);
    fm_home(&fm);
    fm_set_size(&fm, 40, 10, 40, 10);
    CHECK(fm_row_pos(&fm, 0) == -1, "row: the header row selects nothing");
    CHECK(fm_row_pos(&fm, 1) == 0, "row: the first entry row is the top of the view");
    CHECK(fm_row_pos(&fm, 8) == 7, "row: the last entry row is top + avail - 1");
    CHECK(fm_row_pos(&fm, 9) == -1, "row: the footer row selects nothing");
    CHECK(fm_row_pos(&fm, 500) == -1, "row: a row past the pane selects nothing");

    /* Scroll the view and the map has to follow it — this is what makes a click select the entry drawn
       under the pointer rather than the one that happens to be first. */
    fm_end(&fm);
    CHECK(fm.top > 0, "row: the view scrolled for End");
    CHECK(fm_row_pos(&fm, 1) == fm.top, "row: after scrolling, row 1 is the new top");
    const FmEntry *e = vis_at(&fm, fm_row_pos(&fm, 3));
    CHECK(e && !strcmp(e->name, vis_at(&fm, fm.top + 2)->name),
          "row: a middle row names the entry the painter put there");

    fm_set_size(&fm, 40, 3, 40, 3);
    fm_home(&fm);
    CHECK(fm_row_pos(&fm, 0) == 0, "row: a pane too short for its chrome starts entries at row 0");
    CHECK(fm_row_pos(&fm, 2) == 2, "row: and still maps the last of them");
    CHECK(fm_row_pos(&fm, 3) == -1, "row: one row past that is nothing");

    fm_destroy(&fm);
}

/* ---------- a text selection inside the preview pane (step 3b) ----------
   The pointer's half of this feature lives in main.c, which is in no test binary; everything about WHAT a
   gesture selects lives in fm.c and vt.c, and that is what these pin. Every number below was printed by
   .scratch/s115/selprobe{,2,3} against this exact pane (30 columns x 12 rows, words.txt) before the CHECK
   was written — the two that the first probe printed wrong (a row span read out of the same printf that
   filled it, and a scroll invalidation tested on a file with nothing to scroll) are why there are three.
   The body of a text preview starts at row 2, column 2: rows 0-1 are the head (name, then path), and the
   two-column gutter is never written, which is what makes the row gesture skip it. */

/* The text a span covers, read back out of the pane: a check that names the WORD a gesture selects says more
   than one that names two column numbers. */
static void span_text(const FmPane *p, int y, int x0, int x1, char *out, size_t cap)
{
    size_t o = 0;
    for (int x = x0; x <= x1 && x < p->cols && o + 8 < cap; x++) {
        uint32_t ch = p->cell[(size_t)y * p->cols + (size_t)x].ch;
        if (ch)
            o += (size_t)utf8_encode(ch, out + o);
    }
    out[o] = 0;
}

/* Does the painted band agree with the model, cell for cell? The rule a session enforces through
   term_cell_highlightable(): a band lands only on a cell that HOLDS something (a glyph, a real space, or the
   continuation half of a wide glyph), never on the never-written space past the end of a line. Counting the
   disagreements covers every cell of the pane in one check. */
static int band_mismatches(const FileMgr *fm, uint32_t sel)
{
    const FmPane *p = &fm->prev;
    int bad = 0;
    for (int y = 0; y < p->rows; y++)
        for (int x = 0; x < p->cols; x++) {
            const Cell *c = &p->cell[(size_t)y * p->cols + (size_t)x];
            int holds = (c->ch != 0) || (c->attr & ATTR_WIDE);
            int want = fm_preview_selected(fm, x, y) && holds;
            if ((c->bg == sel) != want)
                bad++;
        }
    return bad;
}

/* How many cells of the LIST pane carry the selection colour. Its own selected row is the only thing that
   puts it there, so the number must not move when a selection appears in the OTHER pane. */
static int list_band_cells(const FileMgr *fm, uint32_t sel)
{
    const FmPane *p = &fm->list;
    int n = 0;
    for (int y = 0; y < p->rows; y++)
        for (int x = 0; x < p->cols; x++)
            if (p->cell[(size_t)y * p->cols + (size_t)x].bg == sel)
                n++;
    return n;
}

static void test_preview_select(void)
{
    FileMgr fm;
    fm_init(&fm, root);
    fm_set_size(&fm, 40, 12, 30, 12);
    unsigned long seq = 0;
    fm_go(&fm, root, &seq);
    fm_install_scan(&fm, fm_scan_build(root, seq));

    char path[1024];
    int pos = find_pos(&fm, "words.txt");
    CHECK(pos >= 0, "pvsel: the tree has words.txt to preview");
    fm_select(&fm, pos);
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvsel: a preview is asked for");
    CHECK(fm_install_preview(&fm, fm_preview_build(path, seq)) == 1, "pvsel: and it installs");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(pane_shows(&fm, "two-three") == 2, "pvsel: the first body row is row 2");

    /* ---- nothing selected yet ---- */
    char out[4096];
    CHECK(!fm.pv_sel_on, "pvsel: nothing is selected before a gesture");
    CHECK(fm_preview_selection_text(&fm, out, sizeof out) == 0 && out[0] == 0,
          "pvsel: with no selection there is nothing to copy");
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: and no cell carries a band");

    /* ---- one cell, and the predicate's shape ---- */
    fm_preview_select(&fm, 2, 2, 2, 2);
    CHECK(fm.pv_sel_on == 1, "pvsel: a press selects the cell under the pointer");
    CHECK(fm.pv_ax == 2 && fm.pv_ay == 2 && fm.pv_bx == 2 && fm.pv_by == 2,
          "pvsel: a one-cell selection stores one cell");
    CHECK(fm_preview_selected(&fm, 2, 2) == 1, "pvsel: that cell is inside");
    CHECK(fm_preview_selected(&fm, 1, 2) == 0 && fm_preview_selected(&fm, 3, 2) == 0,
          "pvsel: its neighbours are not");
    CHECK(fm_preview_selected(&fm, 2, 3) == 0, "pvsel: and neither is the row below");
    CHECK(fm_preview_selection_text(&fm, out, sizeof out) == 1 && !strcmp(out, "t"),
          "pvsel: one cell copies one character");
    CHECK(band_mismatches(&fm, SEL_BG) == 1, "pvsel: exactly one cell is banded");
    fm_preview_select_clear(&fm);
    CHECK(!fm.pv_sel_on, "pvsel: clear() ends it");
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: and the band is gone on the next paint");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: repainting after a clear leaves no band");

    /* ---- normalisation: the direction of the drag cannot invert the range ---- */
    fm_preview_select(&fm, 10, 2, 2, 2);
    CHECK(fm.pv_ax == 2 && fm.pv_ay == 2 && fm.pv_bx == 10 && fm.pv_by == 2,
          "pvsel: a drag leftward stores the same range as one rightward");
    fm_preview_select(&fm, 10, 4, 2, 2);
    CHECK(fm.pv_ax == 2 && fm.pv_ay == 2 && fm.pv_bx == 10 && fm.pv_by == 4,
          "pvsel: a drag up-and-left anchors on the FIRST row, not the last");
    int n_up = fm_preview_selection_text(&fm, out, sizeof out);
    CHECK(n_up == 40 && !strcmp(out, "two-three, four.\n\xe4\xbd\xa0\xe5\xa5\xbd "
                                     "\xe4\xb8\x96\xe7\x95\x8c\n/etc/pass"),
          "pvsel: and it copies the same bytes whichever way the pointer moved, with no gutter spaces "
          "on the rows after the first");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: a three-row band agrees with the model");
    /* The case the one above cannot tell apart: a drag that goes DOWN a row but LEFT a column. Normalising
       by column alone picks the wrong endpoint there, and the stored pair comes out with pv_ay ABOVE pv_by,
       which makes fm_preview_selected() answer "nothing" for every row — a drag that selected nothing at all.
       Found by mutation (.scratch/s116/mutate.py no_normalise): the first two checks here pass with that
       mutant, so without this one the normalisation pinned nothing. */
    fm_preview_select(&fm, 10, 2, 2, 4);
    CHECK(fm.pv_ax == 10 && fm.pv_ay == 2 && fm.pv_bx == 2 && fm.pv_by == 4,
          "pvsel: a drag down-and-left still anchors on the row the press started on");
    int n_dn = fm_preview_selection_text(&fm, out, sizeof out);
    CHECK(n_dn == 24,
          "pvsel: and it selects the three rows it names - the wrong endpoint leaves every row outside "
          "the range and the drag selects nothing at all");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: and its band is the model's, row for row");

    /* ---- clamping: a press or a drag outside the pane ---- */
    fm_preview_select(&fm, -5, -5, 999, 999);
    CHECK(fm.pv_ax == 0 && fm.pv_ay == 0 && fm.pv_bx == 29 && fm.pv_by == 11,
          "pvsel: a range bigger than the pane is clamped to the pane");
    CHECK(fm_preview_selected(&fm, 0, 0) == 1 && fm_preview_selected(&fm, 29, 11) == 1,
          "pvsel: the clamped corners are inside");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: a whole-pane band still skips the blank cells");
    fm_preview_select_clear(&fm);

    /* ---- the band is a background, not a foreground ---- */
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    uint32_t ink = fm.prev.cell[2 * fm.prev.cols + 2].fg;
    fm_preview_select(&fm, 2, 2, 10, 2);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(fm.prev.cell[2 * fm.prev.cols + 2].fg == ink,
          "pvsel: the band leaves the ink alone, so syntax colour survives the highlight");
    CHECK(fm.prev.cell[2 * fm.prev.cols + 2].bg == SEL_BG,
          "pvsel: and writes itself into the cell's own bg, the way the list's selected row does");

    /* ---- the band never floats: the gutter and the space past a line stay unhighlighted ---- */
    fm_preview_select(&fm, 0, 2, 29, 2);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(fm.prev.cell[2 * fm.prev.cols + 0].bg != SEL_BG,
          "pvsel: the never-written gutter column gets no band");
    CHECK(fm.prev.cell[2 * fm.prev.cols + 1].bg != SEL_BG,
          "pvsel: nor the second gutter column");
    CHECK(fm.prev.cell[2 * fm.prev.cols + 18].bg != SEL_BG,
          "pvsel: nor the space past the end of the line");
    CHECK(fm.prev.cell[2 * fm.prev.cols + 12].bg == SEL_BG,
          "pvsel: but a REAL space inside the line does, exactly as in a session");
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: and every cell of the pane agrees");

    /* ---- the band belongs to the preview pane ---- */
    int lb_before = list_band_cells(&fm, SEL_BG);
    fm_preview_select(&fm, 2, 2, 17, 4);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(list_band_cells(&fm, SEL_BG) == lb_before,
          "pvsel: a preview selection paints nothing in the LIST pane - its selected row is its own");
    fm_preview_select_clear(&fm);

    /* ---- the word gesture: the session's rule, not a copy of it ---- */
    int x0, x1, ok;
    char w[128];
    ok = fm_preview_word_span(&fm, 2, 2, &x0, &x1);
    span_text(&fm.prev, 2, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && x1 == 10 && !strcmp(w, "two-three"),
          "pvsel: a double click takes a hyphenated word whole");
    ok = fm_preview_word_span(&fm, 11, 2, &x0, &x1);
    span_text(&fm.prev, 2, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && x1 == 10 && !strcmp(w, "two-three"),
          "pvsel: a click on the comma after it lands on the word, and the comma is cut");
    ok = fm_preview_word_span(&fm, 13, 2, &x0, &x1);
    span_text(&fm.prev, 2, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 13 && x1 == 16 && !strcmp(w, "four"),
          "pvsel: trailing punctuation is cut (the user's rule: any of it)");
    ok = fm_preview_word_span(&fm, 2, 4, &x0, &x1);
    span_text(&fm.prev, 4, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && x1 == 12 && !strcmp(w, "/etc/passwd"),
          "pvsel: a path stays one word - slashes and dots inside it are not separators");
    ok = fm_preview_word_span(&fm, 14, 4, &x0, &x1);
    span_text(&fm.prev, 4, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 14 && x1 == 15 && !strcmp(w, "(a"),
          "pvsel: LEADING punctuation stays (the user's rule: we don't take the trailing)");
    ok = fm_preview_word_span(&fm, 20, 4, &x0, &x1);
    span_text(&fm.prev, 4, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 17 && x1 == 20 && !strcmp(w, "path"),
          "pvsel: and the closing bracket after a word is cut");
    ok = fm_preview_word_span(&fm, 1, 2, &x0, &x1);
    CHECK(ok == 1 && x0 == 1 && x1 == 1,
          "pvsel: a double click on a blank selects the clicked cell - doing nothing is worse");
    /* The head rows are text too: the file's name is selectable. */
    ok = fm_preview_word_span(&fm, 2, 0, &x0, &x1);
    span_text(&fm.prev, 0, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 1 && x1 == 9 && !strcmp(w, "words.txt"),
          "pvsel: the preview's head row is selectable text as well");
    /* Out of the pane. */
    x0 = x1 = -7;
    ok = fm_preview_word_span(&fm, 99, 2, &x0, &x1);
    CHECK(ok == 0, "pvsel: a column outside the pane has no word");
    ok = fm_preview_word_span(&fm, 2, 99, &x0, &x1);
    CHECK(ok == 0, "pvsel: nor does a row outside it");
    /* One rule, one copy: the preview's word span IS vt.c's row-level function over the pane's own cells.
       If fm.c ever grew its own punctuation table, this is the check that would notice the drift. */
    int sx0, sx1;
    ok = fm_preview_word_span(&fm, 13, 2, &x0, &x1);
    int ok2 = cell_row_word_span(fm.prev.cell + (size_t)2 * fm.prev.cols, fm.prev.cols, 13, &sx0, &sx1);
    CHECK(ok == 1 && ok2 == 1 && x0 == sx0 && x1 == sx1,
          "pvsel: the preview uses the session's word rule, not a second copy of it");

    /* ---- the wide glyph: never split, by the span, by the band or by the copy ---- */
    ok = fm_preview_word_span(&fm, 2, 3, &x0, &x1);
    span_text(&fm.prev, 3, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && x1 == 5 && !strcmp(w, "\xe4\xbd\xa0\xe5\xa5\xbd"),
          "pvsel: a double click covers both cells of each full-width glyph");
    ok = fm_preview_word_span(&fm, 4, 3, &x0, &x1);
    CHECK(ok == 1 && x0 == 2 && x1 == 5,
          "pvsel: clicking the second glyph's anchor gives the same run, never half a pair");
    /* A trailing mark that is NOT ASCII: the explicit list in trailing_punct() (the same table a session
       uses) is what cuts it, and the wide-pair rule is what keeps the cut from splitting it. Measured
       .scratch/s115/selprobe4: row 5 is 元 at cols 2-3 and 。 (U+3002) at cols 4-5. Without that list the
       span would be 2..5 - this is the check the ASCII-only mutant of the shared rule fails. */
    ok = fm_preview_word_span(&fm, 2, 5, &x0, &x1);
    span_text(&fm.prev, 5, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && x1 == 3 && !strcmp(w, "\xe5\x85\x83"),
          "pvsel: a full-width CJK mark is cut as trailing punctuation, glyph and all");
    ok = fm_preview_word_span(&fm, 4, 5, &x0, &x1);
    span_text(&fm.prev, 5, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && x1 == 3 && !strcmp(w, "\xe5\x85\x83"),
          "pvsel: double-clicking the mark itself selects the word before it, not half a mark");
    ok = fm_preview_row_span(&fm, 5, &x0, &x1);
    CHECK(ok == 1 && x0 == 2 && x1 == 5,
          "pvsel: but the ROW keeps the mark - it is part of the line the file holds");
    fm_preview_select(&fm, 2, 3, 5, 3);
    CHECK(fm_preview_selection_text(&fm, out, sizeof out) == 6 &&
          !strcmp(out, "\xe4\xbd\xa0\xe5\xa5\xbd"),
          "pvsel: a copy of the two pairs is the two glyphs, not four cells");
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(fm.prev.cell[3 * fm.prev.cols + 3].bg == SEL_BG && fm.prev.cell[3 * fm.prev.cols + 5].bg == SEL_BG,
          "pvsel: a wide glyph's continuation half is banded too, or the glyph is half lit");
    fm_preview_select(&fm, 2, 3, 3, 3);
    CHECK(fm_preview_selection_text(&fm, out, sizeof out) == 3 && !strcmp(out, "\xe4\xbd\xa0"),
          "pvsel: a range ending inside a pair still copies the whole glyph once");
    fm_preview_select_clear(&fm);

    /* ---- the row gesture ---- */
    ok = fm_preview_row_span(&fm, 2, &x0, &x1);
    CHECK(ok == 1 && x0 == 2 && x1 == 17,
          "pvsel: a triple click spans the row's OWN cells - not the pane's gutter, not past the text");
    fm_preview_select(&fm, x0, 2, x1, 2);
    CHECK(fm_preview_selection_text(&fm, out, sizeof out) == 16 && !strcmp(out, "two-three, four."),
          "pvsel: and copies the file's line exactly, with no gutter spaces added");
    fm_preview_select_clear(&fm);
    ok = fm_preview_row_span(&fm, 11, &x0, &x1);
    CHECK(ok == 1 && x0 == 0 && x1 == 29,
          "pvsel: a row holding nothing spans the whole row - the gesture means something, it copies nothing");
    fm_preview_select(&fm, x0, 11, x1, 11);
    CHECK(fm_preview_selection_text(&fm, out, sizeof out) == 0,
          "pvsel: and an empty row copies the empty string");
    fm_preview_select_clear(&fm);
    x0 = x1 = -7;
    ok = fm_preview_row_span(&fm, 12, &x0, &x1);      /* one row past the pane */
    CHECK(ok == 0, "pvsel: a row outside the pane has no span");
    ok = fm_preview_row_span(&fm, -1, &x0, &x1);
    CHECK(ok == 0, "pvsel: nor does a negative one");

    /* ---- the copy is bounded by the buffer, and always terminated ---- */
    fm_preview_select(&fm, 2, 2, 17, 2);
    char small[9];
    memset(small, 'Z', sizeof small);
    int n = fm_preview_selection_text(&fm, small, sizeof small);
    CHECK(n == 8 && !strcmp(small, "two-thre"),
          "pvsel: a small buffer gets a prefix, cut to leave room for the NUL");
    char one[1];
    one[0] = 'Z';
    n = fm_preview_selection_text(&fm, one, 1);
    CHECK(n == 0 && one[0] == 0, "pvsel: a one-byte buffer copies nothing and stays terminated");
    /* cap == 0 has no byte to terminate. The function first guarded only its opening store (`if (cap)
       out[0] = 0;`) and left the terminating one as `out[n < cap ? n : cap - 1]` — for cap 0 that is
       `out[-1]`, and .scratch/s116/capzero.c under ASan showed the WRITE one byte before the caller's array.
       The buffer is deliberately the SECOND byte of an array: `pad + 1 - 1` is `pad[0]` by pointer
       arithmetic, so this check sees the underflow without needing a sanitizer build. */
    char pad[2];
    pad[0] = 'Z';
    pad[1] = 'Y';
    n = fm_preview_selection_text(&fm, pad + 1, 0);
    CHECK(n == 0 && pad[0] == 'Z' && pad[1] == 'Y',
          "pvsel: a zero-capacity buffer is written nowhere at all, not one byte before it either");
    fm_preview_select_clear(&fm);

    /* ---- invalidation: the range names CELLS, so anything that changes them ends it ---- */
    fm_select(&fm, find_pos(&fm, "many.txt"));       /* 40 lines: this pane has room to scroll */
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvsel: many.txt is previewed");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    CHECK(fm_preview_scroll_max(&fm) == 30, "pvsel: and this pane has 30 rows to scroll through");
    fm_preview_select(&fm, 2, 2, 6, 2);
    CHECK(fm.pv_sel_on == 1, "pvsel: selected in the scrollable preview");
    fm_preview_scroll(&fm, 0);
    CHECK(fm.pv_sel_on == 1,
          "pvsel: a wheel gesture that moved nothing keeps the selection - it is not a scroll");
    fm_preview_scroll(&fm, 1);
    CHECK(fm.pv_sel_on == 0,
          "pvsel: a scroll that DID move clears it - the cells the endpoints name hold other text now");
    fm_preview_select(&fm, 2, 2, 6, 2);
    fm_set_size(&fm, 40, 12, 30, 12);
    CHECK(fm.pv_sel_on == 1, "pvsel: a resize that changed nothing keeps it");
    fm_set_size(&fm, 40, 12, 40, 12);
    CHECK(fm.pv_sel_on == 0, "pvsel: a wider pane clears it - the columns name different characters now");
    fm_set_size(&fm, 40, 12, 30, 12);
    fm_preview_select(&fm, 2, 2, 6, 2);
    fm_select(&fm, find_pos(&fm, "words.txt"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvsel: another file is selected");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    CHECK(fm.pv_sel_on == 0,
          "pvsel: a new preview clears it - the range named the PREVIOUS file's cells");

    /* ---- a directory preview is selectable too: its body is the entry list ---- */
    fm_select(&fm, find_pos(&fm, "many"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvsel: a directory preview is asked for");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    int row = pane_shows(&fm, "f0");
    CHECK(row >= 0, "pvsel: the directory preview paints its entries");
    ok = fm_preview_word_span(&fm, 2, row, &x0, &x1);
    span_text(&fm.prev, row, x0, x1, w, sizeof w);
    CHECK(ok == 1 && x0 == 2 && !strcmp(w, "f0"), "pvsel: an entry name is a word there too");
    fm_preview_select(&fm, 2, row, 6, row);
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    CHECK(band_mismatches(&fm, SEL_BG) == 0, "pvsel: and its band follows the same rule");

    /* ---- a line WIDER than the pane: the copy is what the row shows, not the file's whole line ----
       The preview cuts a long line at the pane's right edge and the pane never scrolls sideways, so the
       tail of a line wider than the pane is not in any cell to select. It sits last in the group because
       installing a preview replaces the one the checks above read. */
    fm_select(&fm, find_pos(&fm, "long.txt"));
    CHECK(fm_preview_request(&fm, &seq, path, sizeof path) == 1, "pvsel: the long-line file is previewed");
    fm_install_preview(&fm, fm_preview_build(path, seq));
    fm_paint(&fm, BASE_FG, BASE_BG, SEL_BG);
    ok = fm_preview_row_span(&fm, 2, &x0, &x1);
    CHECK(ok == 1 && x0 == 2 && x1 == 29,
          "pvsel: a row wider than the pane spans to the pane's LAST column");
    fm_preview_select(&fm, x0, 2, x1, 2);
    int n_long = fm_preview_selection_text(&fm, out, sizeof out);
    CHECK(n_long == 28 && !strcmp(out, "abcdefghijklmnopqrstuvwxyzab"),
          "pvsel: and the copy is the 28 cells that row SHOWS, not the 130-character line the file holds");

    fm_destroy(&fm);
}

/* ---------- the wheel over the band and the keyboard (fm_wheel_focus) ----------
   The user's report: hovering the pointer over the band (or the preview pane) took the keyboard. The
   mechanism was NOT a hover handler — there is none — it was fm_wheel() in main.c calling fm_focus_set()
   on its fifth line, BEFORE it knew whether the gesture had scrolled anything, and returning early when
   it had not. A wl_pointer.frame with a sub-cell delta therefore moved the keyboard and did nothing
   else. These checks pin the rule the decision now follows; the first three are the bug itself and fail
   against the old unconditional rule even with fm_focus_on_hover = 1. */
static void test_wheel_focus(void)
{
    /* A gesture that scrolled nothing never moves the keyboard — in EITHER mode. This is the fix, not a
       preference: the old code focused the pane and then returned because steps was 0. */
    CHECK(fm_wheel_focus(FM_HIT_LIST, 0, 0) == -1,
          "wheel focus: a gesture that scrolled nothing leaves the keyboard alone (default mode)");
    CHECK(fm_wheel_focus(FM_HIT_LIST, 0, 1) == -1,
          "wheel focus: not even with fm_focus_on_hover = 1 — a scroll of zero is not a focus request");
    CHECK(fm_wheel_focus(FM_HIT_PREVIEW, 0, 1) == -1,
          "wheel focus: the preview pane is not focused by a zero scroll either");

    /* The default the user asked for: a pane is focused by CLICKING it, so a real scroll over the band
       scrolls and leaves the keyboard where it was. */
    CHECK(fm_wheel_focus(FM_HIT_LIST, 3, 0) == -1,
          "wheel focus: a real scroll does NOT take the keyboard by default");
    CHECK(fm_wheel_focus(FM_HIT_PREVIEW, 3, 0) == -1,
          "wheel focus: nor does it focus the preview pane by default");

    /* fm_focus_on_hover = 1 keeps the older behaviour, for anyone who wants the pane under the pointer to
       own the keyboard. A gesture in either direction counts as a scroll. */
    CHECK(fm_wheel_focus(FM_HIT_LIST, 3, 1) == FM_FOCUS_LIST,
          "wheel focus: fm_focus_on_hover = 1 gives the keyboard to the browser pane");
    CHECK(fm_wheel_focus(FM_HIT_PREVIEW, -2, 1) == FM_FOCUS_PREVIEW,
          "wheel focus: and to the preview pane; a wheel-up (negative steps) is a scroll too");

    /* Not over the band at all: never the keyboard, whatever the other arguments say. Checked here rather
       than left to the caller's FM_HIT_NONE filter, so a future caller cannot get it wrong. */
    CHECK(fm_wheel_focus(FM_HIT_NONE, 3, 1) == -1,
          "wheel focus: a point outside the band never moves the keyboard");
    CHECK(fm_wheel_focus(FM_HIT_NONE, 0, 0) == -1, "wheel focus: and not with no scroll either");
}

int main(void)
{
    kai_term_loglevel = L_ERROR;      /* the failure paths log; the tests must not drown in them */
    make_tree();

    test_formatters();
    test_lang();
    test_scan();
    test_nav();
    test_view_toggle();
    test_preview();
    test_paint();
    test_preview_scroll();
    test_preview_keys();
    test_fit();
    test_preview_select();
    test_wheel_focus();

    /* Undo the chmod before removing the tree, or rmdir fails. */
    if (readable_test) {
        char p[1024];
        p_join(p, sizeof p, "noread");
        chmod(p, 0755);
    }
    rm_tree(root);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
