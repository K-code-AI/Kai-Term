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
