/* Kai-Term — PTY handling: fork a shell attached to a pseudo-terminal. */
#include "kai-term.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <unistd.h>

int pty_spawn(Pty *p, const char *cmd, int rows, int cols, const char *dir)
{
    p->mfd = posix_openpt(O_RDWR | O_NOCTTY);
    if (p->mfd < 0) {
        klog(L_ERROR, "posix_openpt: %s", strerror(errno));
        return -1;
    }
    if (grantpt(p->mfd) < 0 || unlockpt(p->mfd) < 0) {
        klog(L_ERROR, "grantpt/unlockpt: %s", strerror(errno));
        close(p->mfd);
        p->mfd = -1;
        return -1;
    }
    char *slave_name = ptsname(p->mfd);
    if (!slave_name) {
        klog(L_ERROR, "ptsname: %s", strerror(errno));
        close(p->mfd);
        p->mfd = -1;
        return -1;
    }

    /* Leave the pty's termios at the kernel's defaults. On Linux a pty master and its slave share ONE
       termios, so anything set here is also the CHILD's terminal. An earlier version called
       cfmakeraw() on the master, which gave every program in the terminal a raw terminal: ISIG off made
       Ctrl+C an ordinary byte instead of SIGINT, ICANON off made Ctrl+D an ordinary byte instead of EOF,
       ECHO off meant a program that relies on the tty echoing showed nothing it was typed, and OPOST off
       made every `printf("\n")` arrive as a bare LF, which the VT parser correctly does not treat as a
       newline — the output staircases. Measured 2026-10-19 (`.scratch/s78/`, A/B against a copy of this
       file with the cfmakeraw block deleted): `ls` emits `<LF>` with the raw pty and `<CR><LF>` without
       it; `sleep 30` + Ctrl+C survives with it and is killed by SIGINT without it (in fish too); Ctrl+D
       is not EOF with it. Programs that want raw mode set it themselves — readline, vim, fish and less
       all do — so leaving the defaults alone is what a terminal emulator must do. */
    pty_setsize(p, rows, cols);
    p->rows = rows;
    p->cols = cols;

    p->child = fork();
    if (p->child < 0) {
        klog(L_ERROR, "fork: %s", strerror(errno));
        close(p->mfd);
        p->mfd = -1;
        return -1;
    }
    if (p->child == 0) {
        /* child: open slave, make it our controlling terminal */
        int sfd = open(slave_name, O_RDWR);
        if (sfd < 0)
            _exit(127);
        close(p->mfd);

        if (setsid() < 0)
            klog(L_WARN, "setsid: %s", strerror(errno));
        if (ioctl(sfd, TIOCSCTTY, 0) < 0)
            klog(L_WARN, "TIOCSCTTY: %s", strerror(errno));

        dup2(sfd, STDIN_FILENO);
        dup2(sfd, STDOUT_FILENO);
        dup2(sfd, STDERR_FILENO);
        if (sfd > STDERR_FILENO)
            close(sfd);

        unsetenv("COLUMNS");
        unsetenv("LINES");
        unsetenv("TERMCAP");
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "kai-term", 1);

        /* Start the shell in a known directory instead of inheriting wherever kai-term itself was
           launched from (a file manager starting a terminal leaves it in its own cwd). main.c has
           already resolved the default to $HOME and validated the path. */
        if (dir && dir[0]) {
            if (chdir(dir) != 0)
                klog(L_WARN, "chdir %s: %s (starting in %s)", dir, strerror(errno),
                     getenv("PWD") ? getenv("PWD") : ".");
        }

        if (cmd && cmd[0]) {
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        const char *sh = getenv("SHELL");
        if (!sh || !*sh)
            sh = "/bin/bash";
        execl(sh, sh, (char *)NULL);
        execl("/bin/sh", "/bin/sh", (char *)NULL);
        _exit(127);
    }

    return 0;
}

void pty_setsize(Pty *p, int rows, int cols)
{
    if (p->mfd < 0)
        return;
    struct winsize ws = {
        .ws_row = (unsigned short)rows,
        .ws_col = (unsigned short)cols,
    };
    /* The cell size in px, when it is known. Applications that draw images need it: timg 1.6.3
       only auto-selects its kitty graphics mode when the PTY reports a non-zero pixel geometry
       (measured: with ws_xpixel/ws_ypixel == 0 it prints "cell-pixels: -1x-2" and falls back to
       quarter blocks), and it scales the image with it, so reporting nothing is what makes an
       image come out at the wrong aspect. */
    if (p->cell_w > 0 && p->cell_h > 0) {
        ws.ws_xpixel = (unsigned short)(cols * p->cell_w);
        ws.ws_ypixel = (unsigned short)(rows * p->cell_h);
    }
    if (ioctl(p->mfd, TIOCSWINSZ, &ws) == 0 &&
        (p->rows != rows || p->cols != cols)) {
        if (p->child > 0)
            kill(p->child, SIGWINCH);
    }
    p->rows = rows;
    p->cols = cols;
}

void pty_set_cells(Pty *p, int cell_w, int cell_h)
{
    if (p->mfd < 0)
        return;
    if (cell_w < 0) cell_w = 0;
    if (cell_h < 0) cell_h = 0;
    if (p->cell_w == cell_w && p->cell_h == cell_h)
        return;
    p->cell_w = cell_w;
    p->cell_h = cell_h;
    /* Re-issue the geometry. Zooming changes the cell without changing cols x rows, and an
       application that already read the winsize must see the new pixel numbers. No SIGWINCH:
       the character size the application draws in did not change, only the reported px. */
    pty_setsize(p, p->rows, p->cols);
}

void pty_write(Pty *p, const void *buf, size_t len)
{
    const char *b = buf;
    while (len > 0) {
        ssize_t n = write(p->mfd, b, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        b += n;
        len -= (size_t)n;
    }
}

/* Is something still running here? The DECISION is kept apart from the syscall so a test can put every case
   through without owning a process — the same split as render_cursor(). Measured against bash, zsh and fish
   (.scratch/s117/fgprobe2, which answers each shell's startup queries and proves the command ran): idle at
   the prompt the pty's foreground process group IS the shell's pid, and a foreground command or a pipeline
   moves it elsewhere. Kai-Term never calls tcsetpgrp, so the shell owns that value.
   The two guards are measurements, not paranoia. After the shell exits, bash's pty answers tcgetpgrp with 0,
   and a naive `fg != child` calls a DEAD session "running" — the window would then ask whether to kill
   nothing (zsh and fish left the old value there, which reads idle only by luck). And a session whose pty is
   already closed has nothing to ask about. A job the user backgrounded with `&` keeps the shell in the
   foreground group, so it reads idle: the gate protects a foreground command, not a daemon (measured, and
   documented as a limitation in README/ISSUES). */
int fg_busy(pid_t fg, pid_t child)
{
    if (fg <= 0 || child <= 0)
        return 0;
    return fg != child;
}

int pty_fg_busy(const Pty *p)
{
    if (!p || p->mfd < 0)
        return 0;
    return fg_busy(tcgetpgrp(p->mfd), p->child);
}

/* What is running here, BY NAME. The box the user asked for says `Warning ! "nano" is still running.`, not
   "a command" — a user who typed `nano` recognises their own word and does not have to wonder which session
   is meant. The DECISION about the text is kept apart from the file read for the same reason fg_busy() is:
   a test can put every case through `proc_comm_name` without owning a process.
   /proc/<pid>/comm is the kernel's own name for the process: TASK_COMM_LEN-1 = 15 bytes plus a newline, so
   it is always short enough for one row of a window, and it is the name of the FOREGROUND group's leader,
   which is exactly the process fg_busy() says is at risk. It is read with open/read rather than fopen
   because this runs inside a Wayland event handler on the way to closing the window: no malloc, no stdio
   buffer, nothing that can fail in a way the close path has to unwind. */
int proc_comm_name(const char *raw, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;
    out[0] = 0;
    if (!raw)
        return 0;

    size_t n = strlen(raw);
    while (n && (raw[n - 1] == '\n' || raw[n - 1] == '\r'))
        n--;                    /* comm is newline-terminated; the sentence must not be */

    /* The name goes inside a quoted sentence and is drawn one cell per codepoint, so a control character
       would draw a control code or move the cursor of the window that is asking. Nothing real puts one in a
       comm, and refusing is cheaper than escaping. Bytes >= 0x80 are allowed: a name may be UTF-8. */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)raw[i];
        if (c < 0x20 || c == 0x7f)
            return 0;
    }
    if (!n)
        return 0;

    size_t keep = n < cap - 1 ? n : cap - 1;
    memcpy(out, raw, keep);
    out[keep] = 0;
    utf8_trim_partial(out);     /* the byte cap can cut a multi-byte name in half */
    return out[0] ? 1 : 0;
}

int pty_fg_name(const Pty *p, char *out, size_t cap)
{
    if (!out || cap == 0)
        return 0;
    out[0] = 0;
    if (!p || p->mfd < 0)
        return 0;

    pid_t fg = tcgetpgrp(p->mfd);
    if (fg <= 0)
        return 0;               /* the same dead-session guard fg_busy() refuses on */

    char path[64];
    snprintf(path, sizeof path, "/proc/%lld/comm", (long long)fg);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;               /* the process is gone, or /proc is not mounted */
    char raw[PROC_NAME_MAX + 8];
    ssize_t n = read(fd, raw, sizeof raw - 1);
    close(fd);
    if (n <= 0)
        return 0;
    raw[n] = 0;
    return proc_comm_name(raw, out, cap);
}
