/* Kai-Term — shell integration.
   The file manager can move the shell into the directory the user is browsing. The way that worked before
   this module was to type `cd '<dir>'` at the prompt, which is visible, needs the user's half-typed command
   captured and re-typed around it, and can only run where the model can PROVE the line is empty (see the
   prompt section of src/vt.c). This module does it without touching the line at all: the shell's own line
   editor runs a handler bound to a control sequence, and the directory arrives out of band in a file.

   Why a file and not the sequence: the terminal has one channel to the shell (the pty input stream), and a
   key binding can only ever be a fixed byte sequence — anything after it would be inserted into the user's
   line. So the sequence carries no data, which also means a stray sequence can never run a path.

   Everything here is deliberately dumb and testable: three scripts as string constants, one private
   directory, one request file per session, one command line per shell. The interesting part — which
   sequence is safe — is measured, not assumed, and the measurements are recorded next to the table below
   and in AGENT.md (s90). */

#include "kai-term.h"

#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------- the scripts ----------
   Each one announces itself over Kai-Term's APC channel (parsed by kai_apc in src/vt.c) and binds one
   sequence to a handler that reads the request file and cds. None of them replaces the user's own
   configuration: fish evaluates `-C` after the user's config, bash's --rcfile file sources ~/.bashrc
   first, and zsh's ZDOTDIR directory holds a .zshrc that sources the user's real one first.

   Each script does two more things, both asked for by the user on 2026-10-20:
   - it repaints the prompt after the cd, where the shell can be asked to (fish: `commandline -f repaint`,
     measured; zsh: `zle reset-prompt`; bash: it cannot — see the note in SCRIPT_BASH);
   - it wraps a bare `fish`/`bash`/`zsh` so a shell the user starts at the prompt loads the script too and
     announces itself, instead of the session dropping to the typed path. A name the user defined as a
     function or alias is never replaced, and a command given ANY argument runs exactly as typed. */

static const char SCRIPT_FISH[] =
    "# Kai-Term shell integration for fish. Written by Kai-Term at startup and loaded with\n"
    "# `fish -C \"source <this file>\"`, which fish evaluates AFTER the user's own configuration, so\n"
    "# nothing of theirs is replaced or reordered.\n"
    "#\n"
    "# It gives Kai-Term exactly one thing: a way to ask the shell to change its OWN directory, so the\n"
    "# file manager never types `cd` at the prompt. This function runs inside fish's line editor, so the\n"
    "# text the user is typing is never erased, never re-typed, and never sees the command. The directory\n"
    "# arrives in a file (KAI_TERM_CD_REQUEST), never as typed characters.\n"
    "function kai_term_cd --description \"change to the directory Kai-Term asked for, leaving the line alone\"\n"
    "    set -l d \"\"\n"
    "    if test -n \"$KAI_TERM_CD_REQUEST\"; and test -f \"$KAI_TERM_CD_REQUEST\"\n"
    "        set d (cat \"$KAI_TERM_CD_REQUEST\")\n"
    "    end\n"
    "    if test -n \"$d\"\n"
    "        cd -- \"$d\"\n"
    "        printf '\\033_K;kai-term;cd;%s\\033\\\\' \"$d\"\n"
    "    else\n"
    "        printf '\\033_K;kai-term;cd;\\033\\\\'\n"
    "    end\n"
    "    # Repaint the prompt so it names the directory the shell is IN now. Measured (fish 4.6,\n"
    "    # `.scratch/s92/repaint.c`): without this the prompt keeps showing the directory the shell left\n"
    "    # until the line is submitted — the user's report — and with it the prompt row reads the new\n"
    "    # directory while the typed text stays byte-identical. A repaint re-runs fish_prompt, so a prompt\n"
    "    # function (starship, or the user's own) is re-rendered too.\n"
    "    commandline -f repaint\n"
    "end\n"
    "# C-x C-] (0x18 0x1d). Measured with fish 4.6 (`.scratch/s90b/`): the reader matches these bytes as ONE\n"
    "# key (a literal sequence fish does not parse as a key is never matched); with this binding ABSENT fish\n"
    "# inserts NOTHING when they arrive, and fish has no default binding for them (`bind` reports none), so\n"
    "# taking the pair costs the user nothing. The same two bytes are inert-and-unbound in bash 5.3 too,\n"
    "# which is why there is ONE sequence for every shell — see shellint_seq().\n"
    "bind (printf '\\030\\035') kai_term_cd\n"
    "\n"
    "# A shell the user starts at this prompt gets the same integration, so browsing keeps working inside\n"
    "# it. Only a BARE `fish`/`bash`/`zsh` is wrapped: with any argument at all the command runs exactly as\n"
    "# typed (`bash script.sh`, `fish -c ...` are untouched), and a name the user already defined as a\n"
    "# function or an alias is never replaced. Measured (`.scratch/s92/nested.c`): fish -> `bash` announces\n"
    "# bash, the ack arrives, `pwd` says the new directory and the user's line is untouched.\n"
    "if not set -q KAI_TERM_INT_DIR\n"
    "    # main.c exports this; the fallback is only for a script sourced by hand. `Path` is NOT a builtin in\n"
    "    # fish 4.6 (measured: \"Unknown command\"), so the directory is cut out of the file name by hand.\n"
    "    set -gx KAI_TERM_INT_DIR (string replace -r '/[^/]*$' '' (status current-filename))\n"
    "end\n"
    "function kai_term_nested --description \"start a nested shell with its Kai-Term integration loaded\"\n"
    "    set -l name $argv[1]\n"
    "    set -e argv[1]\n"
    "    if test (count $argv) -eq 0\n"
    "        switch $name\n"
    "            case fish\n"
    "                command fish -C \"source $KAI_TERM_INT_DIR/fish.fish\"\n"
    "            case bash\n"
    "                command bash --rcfile \"$KAI_TERM_INT_DIR/bash.bash\"\n"
    "            case zsh\n"
    "                if not set -q KAI_TERM_REAL_ZDOTDIR\n"
    "                    if set -q ZDOTDIR\n"
    "                        set -gx KAI_TERM_REAL_ZDOTDIR $ZDOTDIR\n"
    "                    else\n"
    "                        set -gx KAI_TERM_REAL_ZDOTDIR $HOME\n"
    "                    end\n"
    "                end\n"
    "                env ZDOTDIR=\"$KAI_TERM_INT_DIR/zsh\" zsh\n"
    "            case '*'\n"
    "                command $name\n"
    "        end\n"
    "        # The nested shell has exited: say who WE are again, so the model names the shell that is\n"
    "        # reading the line now rather than the one that just closed.\n"
    "        printf '\\033_K;kai-term;integration;fish\\033\\\\'\n"
    "    else\n"
    "        command $name $argv\n"
    "    end\n"
    "end\n"
    "if not functions -q fish\n"
    "    function fish --description \"Kai-Term: start a nested fish with its integration loaded\"\n"
    "        kai_term_nested fish $argv\n"
    "    end\n"
    "end\n"
    "if not functions -q bash\n"
    "    function bash --description \"Kai-Term: start a nested bash with its integration loaded\"\n"
    "        kai_term_nested bash $argv\n"
    "    end\n"
    "end\n"
    "if not functions -q zsh\n"
    "    function zsh --description \"Kai-Term: start a nested zsh with its integration loaded\"\n"
    "        kai_term_nested zsh $argv\n"
    "    end\n"
    "end\n"
    "# A heartbeat: after every command, say again which shell is at the prompt. Kai-Term believes an\n"
    "# integration only after it announces, and a shell it started for the user (or the user started in a\n"
    "# way the wrapper above cannot see, like `env bash`) changes who is reading the line. Without this a\n"
    "# session that once got no answer would stay on the typed path for the rest of the run.\n"
    "function kai_term_announce --on-event fish_postexec --description \"tell Kai-Term which shell is at the prompt now\"\n"
    "    printf '\\033_K;kai-term;integration;fish\\033\\\\'\n"
    "end\n"
    "printf '\\033_K;kai-term;integration;fish\\033\\\\'\n";

static const char SCRIPT_BASH[] =
    "# Kai-Term shell integration for bash. Written by Kai-Term at startup and loaded with\n"
    "# `bash --rcfile <this file>`; it sources the user's own ~/.bashrc FIRST, so their prompt, aliases,\n"
    "# options and functions are exactly as they left them.\n"
    "if [ -f \"$HOME/.bashrc\" ]; then . \"$HOME/.bashrc\"; fi\n"
    "if [ -z \"$KAI_TERM_INT_DIR\" ] && [ -n \"${BASH_SOURCE[0]}\" ]; then\n"
    "  KAI_TERM_INT_DIR=\"${BASH_SOURCE[0]%/*}\"\n"
    "  export KAI_TERM_INT_DIR\n"
    "fi\n"
    "kai_term_cd() {\n"
    "  local d=\"\"\n"
    "  if [ -n \"$KAI_TERM_CD_REQUEST\" ] && [ -f \"$KAI_TERM_CD_REQUEST\" ]; then\n"
    "    d=$(cat \"$KAI_TERM_CD_REQUEST\")\n"
    "  fi\n"
    "  if [ -n \"$d\" ]; then\n"
    "    cd -- \"$d\"\n"
    "    printf '\\033_K;kai-term;cd;%s\\033\\\\' \"$d\"\n"
    "  else\n"
    "    printf '\\033_K;kai-term;cd;\\033\\\\'\n"
    "  fi\n"
    "}\n"
    "# C-x C-] (0x18 0x1d). Measured with bash 5.3: readline runs the function without inserting anything,\n"
    "# and with the binding ABSENT readline swallows the unbound C-x pair — nothing lands on the user's\n"
    "# line. It is unbound by default (`bind -p` has no \\C-x\\C-]), so nothing of the user's is overridden.\n"
    "# (An escape-based sequence would NOT be safe here: bash inserts its trailing bytes, measured.)\n"
    "bind -x '\"\\C-x\\C-]\": kai_term_cd'\n"
    "# Why this script does NOT try to repaint the prompt (fish's script does): readline expands the prompt\n"
    "# once per read, so no readline command refreshes it mid-line. Measured (bash 5.3, .scratch/s92/):\n"
    "# C-l leaves the old directory, and printing ${PS1@P} over the line GARBLES it — readline's cached\n"
    "# cursor position goes wrong and the old prompt text survives. The only thing that refreshes it is\n"
    "# submitting a line, which is not Kai-Term's to press. The prompt is correct again after the next\n"
    "# Enter; the shell's own directory is correct immediately (the ack proves it, and `pwd` says so).\n"
    "\n"
    "# A shell the user starts at this prompt gets the same integration (see SCRIPT_FISH for the rule:\n"
    "# bare name only, never over a function or alias the user already defined).\n"
    "kai_term_nested() {\n"
    "  local name=\"$1\"\n"
    "  shift\n"
    "  if [ \"$#\" -eq 0 ]; then\n"
    "    case \"$name\" in\n"
    "      fish) command fish -C \"source $KAI_TERM_INT_DIR/fish.fish\" ;;\n"
    "      bash) command bash --rcfile \"$KAI_TERM_INT_DIR/bash.bash\" ;;\n"
    "      zsh)\n"
    "        if [ -z \"$KAI_TERM_REAL_ZDOTDIR\" ]; then\n"
    "          KAI_TERM_REAL_ZDOTDIR=\"${ZDOTDIR:-$HOME}\"\n"
    "          export KAI_TERM_REAL_ZDOTDIR\n"
    "        fi\n"
    "        env ZDOTDIR=\"$KAI_TERM_INT_DIR/zsh\" zsh ;;\n"
    "      *) command \"$name\" ;;\n"
    "    esac\n"
    "    printf '\\033_K;kai-term;integration;bash\\033\\\\'\n"
    "  else\n"
    "    command \"$name\" \"$@\"\n"
    "  fi\n"
    "}\n"
    "if [ \"$(type -t fish)\" != function ] && [ \"$(type -t fish)\" != alias ]; then\n"
    "  fish() { kai_term_nested fish \"$@\"; }\n"
    "fi\n"
    "if [ \"$(type -t bash)\" != function ] && [ \"$(type -t bash)\" != alias ]; then\n"
    "  bash() { kai_term_nested bash \"$@\"; }\n"
    "fi\n"
    "if [ \"$(type -t zsh)\" != function ] && [ \"$(type -t zsh)\" != alias ]; then\n"
    "  zsh() { kai_term_nested zsh \"$@\"; }\n"
    "fi\n"
    "# A heartbeat: before every prompt, say again which shell is at the prompt (see SCRIPT_FISH). The hook\n"
    "# is PREPENDED so the user's own PROMPT_COMMAND still runs, in either form bash allows (a string or an\n"
    "# array, 5.0+), and the guard is a plain shell variable — an exported one would be inherited by a\n"
    "# nested bash and stop IT from installing its own hook.\n"
    "kai_term_announce() { printf '\\033_K;kai-term;integration;bash\\033\\\\'; }\n"
    "if [ -z \"$KAI_TERM_PROMPT_HOOK\" ]; then\n"
    "  case \"$(declare -p PROMPT_COMMAND 2>/dev/null)\" in\n"
    "    \\(declare\\ -a\\ *) PROMPT_COMMAND=(kai_term_announce \"${PROMPT_COMMAND[@]}\") ;;\n"
    "    *) PROMPT_COMMAND=\"kai_term_announce${PROMPT_COMMAND:+; $PROMPT_COMMAND}\" ;;\n"
    "  esac\n"
    "  KAI_TERM_PROMPT_HOOK=1\n"
    "fi\n"
    "printf '\\033_K;kai-term;integration;bash\\033\\\\'\n";

/* VERIFIED 2026-10-20 (s94): the user confirmed zsh in their own window, and the integration gate now runs zsh
   5.9 headless — the script announces, `C-x C-]` fires the ZLE widget, the ack comes back in 54 ms, the typed
   line is byte-identical and `zle reset-prompt` repaints the prompt so it names the new directory. What is still
   NOT measured for zsh is what those two bytes do in a zsh that has NO binding (the inertness that protects an
   unwrapped nested shell) — it was measured for fish and bash only. Kai-Term only ever uses the integration path
   after the script announces itself, so if a future zsh stops taking the binding the ack never arrives and the
   typed-`cd` path runs: the safe direction to be wrong in. */
static const char SCRIPT_ZSH[] =
    "# Kai-Term shell integration for zsh. Written by Kai-Term at startup; zsh is started with\n"
    "# ZDOTDIR pointing at a directory holding this file as .zshrc, and it sources the user's real .zshrc\n"
    "# first (KAI_TERM_REAL_ZDOTDIR names where that was).\n"
    "if [ -n \"$KAI_TERM_REAL_ZDOTDIR\" ] && [ -f \"$KAI_TERM_REAL_ZDOTDIR/.zshrc\" ]; then\n"
    "  . \"$KAI_TERM_REAL_ZDOTDIR/.zshrc\"\n"
    "elif [ -f \"$HOME/.zshrc\" ]; then\n"
    "  . \"$HOME/.zshrc\"\n"
    "fi\n"
    "if [ -z \"$KAI_TERM_INT_DIR\" ]; then\n"
    "  KAI_TERM_INT_DIR=\"${(%):-%x}\"\n"
    "  KAI_TERM_INT_DIR=\"${KAI_TERM_INT_DIR%/*}\"\n"
    "  export KAI_TERM_INT_DIR\n"
    "fi\n"
    "kai_term_cd() {\n"
    "  local d=\"\"\n"
    "  if [ -n \"$KAI_TERM_CD_REQUEST\" ] && [ -f \"$KAI_TERM_CD_REQUEST\" ]; then\n"
    "    d=$(cat \"$KAI_TERM_CD_REQUEST\")\n"
    "  fi\n"
    "  if [ -n \"$d\" ]; then\n"
    "    builtin cd -- \"$d\"\n"
    "    printf '\\033_K;kai-term;cd;%s\\033\\\\' \"$d\"\n"
    "  else\n"
    "    printf '\\033_K;kai-term;cd;\\033\\\\'\n"
    "  fi\n"
    "  zle reset-prompt        # the prompt must name the directory the shell is IN now\n"
    "}\n"
    "zle -N kai_term_cd\n"
    "bindkey '^X^]' kai_term_cd\n"
    "\n"
    "# A shell the user starts at this prompt gets the same integration (see SCRIPT_FISH for the rule:\n"
    "# bare name only, never over a function the user already defined). UNVERIFIED, like the rest here.\n"
    "kai_term_nested() {\n"
    "  local name=\"$1\"\n"
    "  shift\n"
    "  if [ \"$#\" -eq 0 ]; then\n"
    "    case \"$name\" in\n"
    "      fish) command fish -C \"source $KAI_TERM_INT_DIR/fish.fish\" ;;\n"
    "      bash) command bash --rcfile \"$KAI_TERM_INT_DIR/bash.bash\" ;;\n"
    "      zsh)\n"
    "        if [ -z \"$KAI_TERM_REAL_ZDOTDIR\" ]; then\n"
    "          KAI_TERM_REAL_ZDOTDIR=\"${ZDOTDIR:-$HOME}\"\n"
    "          export KAI_TERM_REAL_ZDOTDIR\n"
    "        fi\n"
    "        env ZDOTDIR=\"$KAI_TERM_INT_DIR/zsh\" zsh ;;\n"
    "      *) command \"$name\" ;;\n"
    "    esac\n"
    "    printf '\\033_K;kai-term;integration;zsh\\033\\\\'\n"
    "  else\n"
    "    command \"$name\" \"$@\"\n"
    "  fi\n"
    "}\n"
    "if (( ! $+functions[fish] )); then\n"
    "  fish() { kai_term_nested fish \"$@\"; }\n"
    "fi\n"
    "if (( ! $+functions[bash] )); then\n"
    "  bash() { kai_term_nested bash \"$@\"; }\n"
    "fi\n"
    "if (( ! $+functions[zsh] )); then\n"
    "  zsh() { kai_term_nested zsh \"$@\"; }\n"
    "fi\n"
    "# A heartbeat: before every prompt, say again which shell is at the prompt (see SCRIPT_FISH).\n"
    "kai_term_announce() { printf '\\033_K;kai-term;integration;zsh\\033\\\\'; }\n"
    "precmd_functions+=(kai_term_announce)\n"
    "printf '\\033_K;kai-term;integration;zsh\\033\\\\'\n";

/* ---------- the table ---------- */

const char *shellint_name(int kind)
{
    switch (kind) {
    case SHELLINT_FISH: return "fish";
    case SHELLINT_BASH: return "bash";
    case SHELLINT_ZSH:  return "zsh";
    default:            return "none";
    }
}

int shellint_kind_for(const char *shell_path)
{
    if (!shell_path || !shell_path[0])
        return SHELLINT_NONE;
    const char *base = strrchr(shell_path, '/');
    base = base ? base + 1 : shell_path;
    if (base[0] == '-')                       /* a login shell is exec'd as "-bash" */
        base++;
    if (!strcmp(base, "fish")) return SHELLINT_FISH;
    if (!strcmp(base, "bash")) return SHELLINT_BASH;
    if (!strcmp(base, "zsh"))  return SHELLINT_ZSH;
    return SHELLINT_NONE;                     /* sh, dash, csh, a -e command line, anything unknown */
}

const char *shellint_seq(int kind, size_t *len)
{
    /* ONE sequence for every supported shell, measured on this box (s91, `.scratch/s90b/inert.c` +
       `fire.c`: real fish 4.6 and real bash 5.3 in a real Kai-Term pty, no window):
       C-x C-] = 0x18 0x1d.
         - FIRES when bound: fish's line editor and bash's readline both run the handler, the user's half
           typed line is byte-identical before and after, and the shell's own cwd changes (the ack proves
           it, and `pwd` after the line is cleared says the new directory).
         - INERT when NOT bound: nothing is inserted, in either shell, with or without the user's own
           rcfile. That is what makes a nested shell (a plain bash started inside an integrated fish) safe:
           the bytes reach a reader with no handler and the worst case is silence plus a timed-out ack, not
           a stray character on the user's line. The per-shell table this replaces (F14 for fish, C-x q for
           bash) was NOT safe that way — F14 inserts `~` in bash, C-x q inserts `q` in fish.
         - UNBOUND by default in both, so Kai-Term takes a key from nobody.
       Rejected, each measured: C-x q (damages fish), ESC [ 2 6 ~ = F14 (damages bash), C-] alone (fires and
       is inert, but bash binds it to character-search, so it would steal a working key), and C-\ (0x1c:
       inert and unbound in both shells, but `stty -a` on a pty reports `quit = ^\` — VQUIT would kill
       whatever owns the tty whenever the shell is not reading it, which is far worse than a stray byte).
       zsh gets the same bytes, and since s94 they are MEASURED there too: the gate's zsh run gets the handler
       running and the ack back in 54 ms. (What has never been measured in zsh is the inert-when-unbound case —
       that was tested in fish and bash only. If a zsh ever does not take these bytes, the ack never arrives and
       the typed path runs, which is the safe direction to be wrong in.) */
    static const char SEQ[] = "\x18\x1d";      /* C-x C-] */
    if (kind == SHELLINT_NONE) {
        if (len)
            *len = 0;
        return NULL;
    }
    if (len)
        *len = sizeof(SEQ) - 1;         /* sizeof, not strlen: both are 2 here, but sizeof cannot drift if a
                                           byte is ever added to the literal */
    return SEQ;
}

/* ---------- the private directory ---------- */

static int write_script(const char *dir, const char *rel, const char *text)
{
    char p[FM_PATH_MAX];
    if (snprintf(p, sizeof(p), "%s/%s", dir, rel) >= (int)sizeof(p))
        return -1;
    FILE *f = fopen(p, "w");
    if (!f) {
        klog(L_WARN, "shell integration: cannot write %s: %s", p, strerror(errno));
        return -1;
    }
    if (fputs(text, f) == EOF || fclose(f) != 0) {
        klog(L_WARN, "shell integration: writing %s failed: %s", p, strerror(errno));
        unlink(p);
        return -1;
    }
    return 0;
}

int shellint_prepare(char *dir, size_t cap)
{
    const char *base = getenv("XDG_RUNTIME_DIR");
    char tmpl[FM_PATH_MAX];
    if (!base || base[0] != '/' || strlen(base) + 64 >= sizeof(tmpl))
        base = "/tmp";                        /* unset, relative, or nearly full: the usual fallback */
    if (snprintf(tmpl, sizeof(tmpl), "%s/kai-term-int-XXXXXX", base) >= (int)sizeof(tmpl))
        return -1;
    if (!mkdtemp(tmpl)) {
        klog(L_WARN, "shell integration: cannot create a directory in %s: %s", base, strerror(errno));
        return -1;
    }
    /* The command line built by shellint_argv() quotes paths with single quotes and is run through
       /bin/sh -c, so a path holding a quote would break it. mkdtemp gives us the name, so this can only
       happen if the runtime directory itself is oddly named — refuse rather than build a broken line. */
    if (strchr(tmpl, '\'') || strlen(tmpl) + 64 >= cap) {
        rmdir(tmpl);
        klog(L_WARN, "shell integration: the runtime directory name is unusable (%s)", tmpl);
        return -1;
    }
    if (chmod(tmpl, 0700) != 0)
        klog(L_WARN, "shell integration: chmod %s: %s", tmpl, strerror(errno));
    snprintf(dir, cap, "%s", tmpl);
    char zd[FM_PATH_MAX];
    if (snprintf(zd, sizeof(zd), "%s/zsh", dir) >= (int)sizeof(zd) ||
        mkdir(zd, 0700) != 0) {               /* zsh needs a directory to point ZDOTDIR at */
        klog(L_WARN, "shell integration: cannot create %s: %s", zd, strerror(errno));
        rmdir(dir);
        return -1;
    }
    if (write_script(dir, "fish.fish", SCRIPT_FISH) != 0 ||
        write_script(dir, "bash.bash", SCRIPT_BASH) != 0 ||
        write_script(dir, "zsh/.zshrc", SCRIPT_ZSH) != 0) {
        shellint_cleanup(dir);
        return -1;
    }
    return 0;
}

void shellint_cleanup(const char *dir)
{
    if (!dir || !dir[0])
        return;
    char p[FM_PATH_MAX + 64];
    snprintf(p, sizeof(p), "%s/fish.fish", dir);   unlink(p);
    snprintf(p, sizeof(p), "%s/bash.bash", dir);   unlink(p);
    snprintf(p, sizeof(p), "%s/zsh/.zshrc", dir);  unlink(p);
    /* The request files are named per SESSION, and a leftover one would outlive the session that asked for
       it: a later shell reading it would cd somewhere the user never chose. main.c numbers sessions with a
       counter that never repeats within one run (a tab index does repeat — closing a tab shifts the later ones
       down and a new tab reuses the freed index, and two sessions sharing a request file is how one shell
       ends up in another session's directory), so the names cannot be enumerated here: the directory is
       scanned and everything matching is removed, including the temporary name write_request() renames from. */
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strncmp(de->d_name, "cd-request", 10) != 0)
                continue;
            char rp[FM_PATH_MAX + 64];
            snprintf(rp, sizeof(rp), "%s/%s", dir, de->d_name);
            unlink(rp);
        }
        closedir(d);
    }
    snprintf(p, sizeof(p), "%s/zsh", dir);         rmdir(p);
    if (rmdir(dir) != 0 && errno != ENOENT)
        klog(L_DEBUG, "shell integration: rmdir %s: %s", dir, strerror(errno));
}

void shellint_request_path(const char *dir, int session, char *out, size_t cap)
{
    if (!dir || !dir[0]) {
        if (cap)
            out[0] = 0;
        return;
    }
    snprintf(out, cap, "%s/cd-request.%d", dir, session);
}

int shellint_write_request(const char *req_path, const char *path)
{
    if (!req_path || !req_path[0] || !path || !path[0])
        return -1;
    char tmp[FM_PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", req_path) >= (int)sizeof(tmp))
        return -1;
    FILE *f = fopen(tmp, "w");
    if (!f) {
        klog(L_WARN, "shell integration: cannot write %s: %s", tmp, strerror(errno));
        return -1;
    }
    if (fputs(path, f) == EOF || ferror(f) || fclose(f) != 0) {
        unlink(tmp);
        return -1;
    }
    /* rename(), not a rewrite in place: the shell reads this file the instant it handles the request, and
       it must never see a half-written path. */
    if (rename(tmp, req_path) != 0) {
        klog(L_WARN, "shell integration: rename %s: %s", req_path, strerror(errno));
        unlink(tmp);
        return -1;
    }
    return 0;
}

int shellint_argv(int kind, const char *dir, const char *req_path, char *cmd, size_t cap)
{
    if (kind == SHELLINT_NONE || !dir || !dir[0] || !req_path || !req_path[0])
        return -1;
    const char *name = shellint_name(kind);
    const char *real_zd = getenv("ZDOTDIR");
    if (!real_zd || !real_zd[0])
        real_zd = getenv("HOME");
    /* KAI_TERM_INT_DIR is exported to every integrated shell because the script's nested-shell wrappers
       need it: a bare `bash` typed at an integrated fish prompt is run as
       `command bash --rcfile $KAI_TERM_INT_DIR/bash.bash`, and the shell running that wrapper only knows
       the directory because we put it in the environment. Each script has a fallback that derives it from
       its own file name, but the wrappers must not depend on that. */
    int n;
    if (kind == SHELLINT_FISH)
        n = snprintf(cmd, cap, "KAI_TERM_CD_REQUEST='%s' KAI_TERM_INT_DIR='%s' "
                               "%s -C 'source %s/fish.fish'", req_path, dir, name, dir);
    else if (kind == SHELLINT_BASH)
        n = snprintf(cmd, cap, "KAI_TERM_CD_REQUEST='%s' KAI_TERM_INT_DIR='%s' "
                               "%s --rcfile '%s/bash.bash'", req_path, dir, name, dir);
    else if (real_zd && real_zd[0] && !strchr(real_zd, '\''))
        n = snprintf(cmd, cap, "KAI_TERM_CD_REQUEST='%s' KAI_TERM_INT_DIR='%s' KAI_TERM_REAL_ZDOTDIR='%s' "
                               "ZDOTDIR='%s/zsh' %s", req_path, dir, real_zd, dir, name);
    else
        n = snprintf(cmd, cap, "KAI_TERM_CD_REQUEST='%s' KAI_TERM_INT_DIR='%s' ZDOTDIR='%s/zsh' %s",
                     req_path, dir, dir, name);
    if (n <= 0 || (size_t)n >= cap)
        return -1;
    return 0;
}
