#pragma once

// The window a person runs: a terminal, plus the three things about a terminal that
// have nothing to do with bytes — keeping a shell, finding the ones that were kept,
// and not throwing work away when it is closed with something running in it.
//
// All three are decided here rather than in the terminal widget, because none of them
// is about the screen. The widget is told which keybinds mean something to its owner
// and which session it is showing; it does not know a daemon exists.

#include "closeprompt.hpp"
#include "../config/config.hpp"
#include "../config/themepicker.hpp"
#include "../session/cli.hpp"
#include "../session/sessionbackend.hpp"
#include "sessionmenu.hpp"
#include "../terminal/terminalwidget.hpp"

#include <QCloseEvent>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace kai {

// What a window starts out showing, and what to say about how it got that way. The
// size travels with it, because the shell was already started at that size: a window
// built at a different one would spend its first moment resizing the shell that came
// with it, and the two numbers would disagree about what the terminal is.
struct WindowStart {
    std::unique_ptr<TerminalBackend> backend;
    std::string session; // empty when there is no daemon to hold the shell
    std::string note;    // said once, in the corner, when the plan changed
    int rows = 24;
    int cols = 80;
};

// Get a shell for a new window, held by the daemon if there is one.
//
// Starting the daemon is not a step anybody should have to take before their terminal
// works, so it is started here, quietly, and a window that cannot start one carries on
// with a shell of its own. That fallback is said out loud once rather than kept quiet,
// because the difference only shows up later — when a keybind that keeps a shell keeps
// nothing — and a person who was never told will not think to look for it here.
inline WindowStart startWindowBackend(int rows, int cols) {
    std::string error;
    const std::string name = startWindowSession(rows, cols, error);
    if (!name.empty()) {
        try {
            // A daemon from another build is not on our socket, so it cannot answer a
            // command we have and an older one does not — which is the point. It is
            // still holding the shells it started, and nothing we can list will show
            // them, so the window says so once instead of leaving them to be found in
            // `ps` next week.
            return {std::make_unique<SessionBackend>(name, rows, cols), name,
                    foreignDaemonNotice(), rows, cols};
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    return {std::make_unique<PtyBackend>(rows, cols, std::string(), homeDirectory()),
            std::string(),
            "no session daemon: " + (error.empty() ? std::string("it would not start") : error),
            rows, cols};
}

class AppWindow : public kai::TerminalWidget {
public:
    // A window with a shell of its own.
    explicit AppWindow(WindowStart start)
        : TerminalWidget(start.rows, start.cols, std::move(start.backend)),
          session_(std::move(start.session)) {
        bindKeybinds();
        adoptConfig(true);
        if (!start.note.empty())
            setStatusText(start.note, 8000);
    }

    // A window onto a session that is already running. Throws when the session is not
    // there, because an empty window looks like a working terminal and is not one.
    explicit AppWindow(const std::string& name)
        : TerminalWidget(24, 80, std::make_unique<SessionBackend>(name, 24, 80)),
          session_(name),
          // Showing a session is not keeping it. It arrived kept or unkept; either way
          // this window is now responsible for it, and only a save from here keeps it.
          session_kept_(false) {
        bindKeybinds();
        // The theme's chrome applies; its screen colours do not. The session's
        // own colours are part of the state this window came to show.
        adoptConfig(false);
    }

    // Read the config file once, take what it says, and say out loud — on
    // stderr, at startup, before the first frame — anything it could not use.
    // A typo'd setting that is silently skipped is an afternoon of wondering;
    // one that names its own line is a fifteen-second fix.
    void adoptConfig(bool owns_screen) {
        config_ = loadConfig();
        for (const std::string& problem : config_.problems)
            std::fprintf(stderr, "kai-term: %s\n", problem.c_str());
        Theme theme; // the built-in colours first, then whatever the file overrode
        const auto lift = [](Color& into, const std::optional<Rgb>& from) {
            if (from)
                into = Color{static_cast<std::uint8_t>(from->r), static_cast<std::uint8_t>(from->g),
                             static_cast<std::uint8_t>(from->b)};
        };
        lift(theme.foreground, config_.foreground);
        lift(theme.background, config_.background);
        lift(theme.cursor, config_.cursor);
        lift(theme.selection, config_.selection);
        lift(theme.accent, config_.accent);
        for (std::size_t i = 0; i < theme.palette.size(); ++i)
            lift(theme.palette[i], config_.palette[i]);
        applyTheme(theme, owns_screen);
        // The window's body, not just its colours: a config that says
        // `font_size = 14` means the cells are 14 pixels tall now, and a
        // config that says `cols = 100` means the opening window is as wide
        // as a hundred of them at that font. Font first, size second — the
        // other order would measure the hundred columns with the old ruler.
        if (config_.font_size)
            setFontPixelSize(*config_.font_size);
        if (config_.cols || config_.rows)
            resize(sizeForGrid(config_.rows.value_or(rows()), config_.cols.value_or(cols())));
        // A window you can half-see through is a window with the desktop
        // behind it; Qt asks the compositor, and without one the number is
        // simply ignored, which is the honest answer rather than a fake.
        if (config_.opacity)
            setWindowOpacity(*config_.opacity);
    }

    const std::string& sessionName() const { return session_; }
    // Whether the shell being shown is one the person said to keep. A restored session
    // is one by definition, and a fresh window is not one until the keybind says so.
    bool sessionKept() const { return session_kept_; }

    // What the close prompt would be about: the program running out front, or empty
    // when the shell is sitting at a prompt. A shell running in this process can be
    // asked directly; a session lives in the daemon, which is where the question has to
    // be asked.
    std::string runningProgram() const {
        if (session_.empty())
            return foregroundProgram();
        std::string error;
        for (const SessionInfo& info : listSessions(error)) {
            if (info.name == session_)
                return info.foreground;
        }
        return {};
    }
    // Called after the window is showing a different session, so whoever set the title
    // can set it again.
    std::function<void()> on_session_changed;

    // Ctrl+Shift+S: keep this shell, and start a fresh one in the same window.
    //
    // The window does not close. The thing a person is doing when they press this is
    // not "I have finished with this window" but "I have finished with this shell", and
    // the second one does not need a new window to carry on in.
    //
    // The kept session is renamed into the range a typed command uses, because the
    // `w` names are for shells nobody decided to keep and this one is now a thing they
    // did decide about. Renaming is not fatal if it fails: the session is kept under
    // the name it has, since keeping it is the part that matters.
    void saveCurrentSession() {
        if (session_.empty()) {
            setStatusText("no session daemon, so nothing was kept", 6000);
            return;
        }
        std::string error;
        const std::string kept = nextKeptName();
        std::string saved_as = session_;
        if (!kept.empty() && saveSession(session_, kept, error))
            saved_as = kept;
        else if (!saveSession(session_, std::string(), error)) {
            setStatusText("could not keep this session: " + error, 6000);
            return;
        }
        session_kept_ = true;

        std::unique_ptr<TerminalBackend> fresh;
        const std::string next = startWindowSession(rows(), cols(), error);
        if (!next.empty()) {
            try {
                fresh = std::make_unique<SessionBackend>(next, rows(), cols());
            } catch (const std::exception& e) {
                setStatusText(std::string("kept, but no new shell: ") + e.what(), 8000);
                return;
            }
        } else {
            // The session is safe either way; a window that could not get a second
            // shell carries on showing the one it just kept rather than going blank.
            setStatusText("kept as '" + saved_as + "', but no new shell would start", 8000);
            return;
        }

        // `false`, because there is nothing to discard: the shell behind this one was
        // kept a moment ago, and a window moving on does not end a kept session.
        showSession(std::move(fresh), false);
        session_ = next;
        session_kept_ = false;
        setStatusText("session '" + saved_as + "' kept");
        if (on_session_changed)
            on_session_changed();
    }

    // Where this window could go instead of staying here. That is everything the daemon
    // would put in a listing — kept, or left behind when a window went away without being
    // asked to — minus the one being shown, which is not somewhere else.
    //
    // The left-behind ones belong here. A shell with no window and a program in it is
    // precisely what the menu is for, and `session list` had been saying so all along;
    // offering only the saved ones was half a rule, the same half as a keep that never
    // expires. Separate from the menu itself so what it would say can be checked without
    // a person clicking it.
    std::vector<SessionInfo> restorableSessions() const {
        std::vector<SessionInfo> out;
        std::string error;
        for (const SessionInfo& info : worthListing(listSessions(error))) {
            if (info.name != session_)
                out.push_back(info);
        }
        return out;
    }

    // Ctrl+Shift+A: the sessions you could go back to, listed inside the terminal
    // itself and walked like any TUI picker — Up/Down or j/k to choose, Enter to go,
    // Esc to put it away, and the number keys to jump straight to an entry. It is
    // drawn by the widget in the terminal's own font, so it looks like a part of the
    // terminal, because it is one.
    //
    // The listing is taken when the key is pressed rather than kept up to date, because
    // the alternative is a menu that is wrong about what is running the moment a window
    // opens or a session is killed somewhere else.
    void showSessionMenu() {
        const std::vector<SessionInfo> choices = restorableSessions();
        if (choices.empty()) {
            askInTerminal("Nothing to go back to",
                          "Ctrl+Shift+S keeps this shell, and it will be here when you "
                          "come back for it.",
                          {"OK"}, 0, [](int) {});
            return;
        }
        std::vector<std::string> labels;
        std::vector<std::string> names;
        for (const SessionInfo& info : choices) {
            labels.push_back(sessionMenuLabel(info));
            names.push_back(info.name);
        }
        chooseInTerminal("Go to another session", labels, [this, names](int which) {
            if (which >= 0 && which < static_cast<int>(names.size()))
                confirmSwitchTo(names[which]);
        });
    }

    // Switching sessions when something is running in this one is the same
    // decision the close button asks: the switch ends this shell, and whatever
    // it is doing goes with it. So it gets the same question, in the same
    // shape — one line, Yes / No / Save Session, No focused, letters
    // answering — with the load in place of the quit. Two things make it
    // silent: nothing running out here (there is nothing to lose), and a
    // session that is already kept (its work goes on whether or not this
    // window is looking at it, which is the entire arrangement).
    void confirmSwitchTo(const std::string& name) {
        const std::string running = runningProgram();
        if (session_kept_ || running.empty()) {
            restoreSession(name);
            return;
        }
        std::vector<std::string> choices{"Yes", "No", "Save Session"};
        askInTerminal("Load session " + name + " while " + running + " is running?",
                      std::string(), choices, 1,
                      [this, name](int which) {
                          if (which == 0) {
                              restoreSession(name);
                              return;
                          }
                          if (which != 2)
                              return; // No, or the question put away: stay here
                          // The same keeping Ctrl+Shift+S does, aimed at the
                          // switch rather than the window: the name a person
                          // would type, and if renaming fails the session is
                          // kept under the name it has, since keeping it is
                          // the part that matters.
                          std::string error;
                          const std::string kept = nextKeptName();
                          bool saved = !kept.empty() && saveSession(session_, kept, error);
                          if (!saved)
                              saved = saveSession(session_, std::string(), error);
                          if (!saved) {
                              // The answer was "save it first", so a save that
                              // failed must not quietly become a switch that
                              // loses the work the button was pressed to keep.
                              setStatusText("could not save the session: " + error, 8000);
                              return;
                          }
                          // `false`, because there is nothing to discard: the
                          // shell behind this one was kept a moment ago.
                          restoreSession(name, false);
                      });
    }

    // Bring a kept session into this window. `discard_current` is the word for
    // the shell being left: true ends it when nobody kept it, false is for a
    // caller that just kept it and means it.
    void restoreSession(const std::string& name, bool discard_current = true) {
        std::unique_ptr<SessionBackend> restored;
        try {
            restored = std::make_unique<SessionBackend>(name, rows(), cols());
        } catch (const std::exception& e) {
            setStatusText(std::string("could not restore '") + name + "': " + e.what(), 8000);
            return;
        }
        // Leaving this shell on purpose: if it was never kept, that is the word that
        // ends it, and if it was kept the daemon keeps it whatever this says.
        showSession(std::move(restored), discard_current);
        session_ = name;
        // Not kept. The daemon says the same thing the moment this view arrives, and
        // saying it here too is what makes the window's own question honest: closing
        // this window will end that shell unless the person says otherwise, so the
        // status line points at the key that would stop it.
        session_kept_ = false;
        setStatusText("session '" + name + "' restored, and no longer kept"
                      " — Ctrl+Shift+S keeps it");
        if (on_session_changed)
            on_session_changed();
    }

protected:
    // Closing the window. Asked about whenever something is running, kept session or
    // not — see closeprompt.hpp for what each answer costs. The question is asked
    // inside the terminal, in its own font and colours, and answered from the
    // keyboard or the pointer; a native dialog would arrive wearing the desktop's
    // theme and remind everybody that the terminal is a rectangle.
    void closeEvent(QCloseEvent* ev) override {
        // The question is already up. This is the same request arriving a second
        // time — another press of the window's close button, a desktop repeating
        // itself — and the dialog on screen is what it should look at, not a twin
        // stacked behind it.
        if (overlayActive()) {
            ev->ignore();
            return;
        }
        if (close_approved_) {
            // The second pass of an answered question: the overlay said what to do,
            // asked for the close again, and this pass lets it through without
            // asking twice. The answer decides whether the shell goes with the
            // window: Yes ends it, Save Session has already kept it.
            const bool keep_session = close_keep_session_;
            close_approved_ = false;
            close_keep_session_ = false;
            if (!keep_session)
                endUnkeptShell();
            TerminalWidget::closeEvent(ev);
            return;
        }
        const ConfirmPolicy policy = closePolicy();
        // Never means never, so it does not go and ask the daemon what is running just to
        // conclude that it will not say so.
        const std::string running = policy == ConfirmPolicy::Never ? std::string() : runningProgram();
        const std::string question = closeQuestion(running, policy);
        if (tracingClose()) {
            traceClose("session '" + session_ + "' kept=" + (session_kept_ ? "yes" : "no") +
                       ", policy=" + policyWord(policy) + ", running='" + running + "', " +
                       (question.empty() ? "nothing to ask" : "would ask: " + question));
            // Only asked for when it is being traced: this is another round trip to the
            // daemon, in the middle of a close, for the sake of a line nobody normally reads.
            traceClose("the daemon has: " + closeListing());
        }
        if (question.empty()) {
            // Nothing to ask about is not the same as nothing happening. A shell nobody
            // kept is ended by a window being closed on purpose — which is the whole
            // difference between closing a terminal and losing one — and saying so only
            // in the branch where a dialog appeared meant a quiet close left the shell
            // running with no window on it, so every window you closed politely turned
            // into a session you never asked to keep.
            endUnkeptShell();
            TerminalWidget::closeEvent(ev);
            return;
        }

        // This close stops here. The answer, when it comes, asks for the close a
        // second time through approveClose().
        ev->ignore();
        // Yes / No / Save Session, in that order, with the focus starting on No:
        // Enter fires the focused answer, and the focused answer at a question asked
        // in haste should be the one that cannot lose a build. "Save Session" is
        // only there when there is a session that is not already kept — see
        // closeOfferSave for why a kept session does not get a second button for
        // the thing it already is. Nothing else is said: the question is the whole
        // question, and a paragraph underneath it is a wall between a person and
        // the three buttons that answer it.
        std::vector<std::string> choices{"Yes", "No"};
        const bool offer_save = closeOfferSave(session_, session_kept_);
        if (offer_save)
            choices.push_back("Save Session");
        askInTerminal(question, std::string(), choices, 1,
                      [this, offer_save](int which) {
                          if (which == 0) {
                              approveClose(false);
                              return;
                          }
                          if (which >= 2 && offer_save) {
                              // Say it failed, but let the window go either way: a
                              // session that could not be saved is still left behind
                              // rather than ended, which is the gentler half of what
                              // the button was asked to do.
                              std::string error;
                              if (!saveSession(session_, std::string(), error))
                                  setStatusText("could not save the session: " + error, 8000);
                              approveClose(true);
                          }
                          // No, or the question put away with Esc or a click outside:
                          // the window stays exactly where it was.
                      });
    }

private:
    // One line on stderr about what a close decided, when KAI_DEBUG_CLOSE is set.
    //
    // It exists because "the window closed without asking" has three answers from here —
    // nothing was running, the session was kept so nothing was being lost, or the
    // desktop's close never reached this window at all — and a person describing the
    // window cannot tell them apart either. What the line says, or the line not appearing
    // at all, is the difference. Quiet by default: a terminal writing over its own shell
    // while a person is typing into it would be a worse complaint than the one being
    // chased.
    static bool tracingClose() { return std::getenv("KAI_DEBUG_CLOSE") != nullptr; }

    // How often closing interrupts. The words are the ones Old-Term's
    // `confirm_quit` takes, so a person who knows one knows the other. The
    // environment beats the file on purpose: `KAI_CONFIRM_QUIT=never` is the
    // thing you type once to try something, and a setting you are holding in
    // your hand right now should not be outranked by one you decided on weeks
    // ago. With neither, closing asks only when something is running.
    ConfirmPolicy closePolicy() const {
        if (const char* setting = std::getenv("KAI_CONFIRM_QUIT"))
            return confirmPolicy(setting);
        return confirmPolicy(config_.confirm_quit);
    }

    static std::string policyWord(ConfirmPolicy policy) {
        switch (policy) {
            case ConfirmPolicy::Never:
                return "never";
            case ConfirmPolicy::Always:
                return "always";
            case ConfirmPolicy::Running:
                break;
        }
        return "running";
    }

    void traceClose(const std::string& what) const {
        if (!tracingClose())
            return;
        std::fprintf(stderr, "kai-term: close: %s\n", what.c_str());
        std::fflush(stderr);
        // And into a file, because the window being asked about is usually the one that
        // was started from the terminal the line would have gone to: asked to look at
        // what a closed window printed is a request that cannot be carried out.
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        const std::string path =
            std::string(runtime != nullptr && runtime[0] != '\0' ? runtime : "/tmp") +
            "/kai-term-close.log";
        if (FILE* log = std::fopen(path.c_str(), "a")) {
            std::fprintf(log, "close: %s\n", what.c_str());
            std::fclose(log);
        }
    }

    // What the daemon had to say about the shells it holds, at the moment of the close.
    // Whether the shell being shown was in that answer at all is the difference between
    // "nothing is running out front" and "the window was not looking at the same shell
    // the daemon was describing", and those two look identical from a window that says
    // nothing either way.
    std::string closeListing() const {
        std::string error;
        std::string out;
        for (const SessionInfo& info : listSessions(error)) {
            out += info.name + "(fg=" + (info.foreground.empty() ? "-" : info.foreground) +
                   ",clients=" + std::to_string(info.clients) +
                   ",saved=" + (info.saved ? "y" : "n") + ",alive=" + (info.alive ? "y" : "n") + ") ";
        }
        if (!error.empty())
            return "the daemon said: " + error;
        if (out.empty())
            return "nothing at all";
        return out;
    }

    // Say that this window is going away on purpose, so that a shell nobody kept goes
    // with it. A kept session ignores the word, and a window that vanishes without it —
    // a crash, a kill -9 — never says it, which is what keeps that shell reachable.
    void endUnkeptShell() {
        if (!sessionKept())
            discardOnClose();
    }

    // An answer from the question inside the terminal: remember what it said, and
    // ask the host to close the window a second time — a close already underway
    // cannot be resumed from a callback, only started again and let through.
    void approveClose(bool keep_session) {
        close_approved_ = true;
        close_keep_session_ = keep_session;
        close();
    }

    // A menu entry says the name, and what is happening in it: a list of names is a
    // list of guesses about which one had the build running.


    std::string nextKeptName() const {
        std::string error;
        return freeName(listSessions(error), "s");
    }

    void bindKeybinds() {
        on_save_session = [this] { saveCurrentSession(); };
        on_session_menu = [this] { showSessionMenu(); };
        on_theme_picker = [this] { showThemePicker(); };
    }

    // Ctrl+Shift+P: choose the colours inside the terminal, with the terminal
    // itself as the preview, and write what you chose back to the config file.
    //
    // The picker opens on the colours this window wears now, so what you see
    // is a starting point and not a lie. The widget hands back only the
    // colours that actually moved, so a save rewrites those lines and leaves
    // the rest of somebody's file — comments, alignment, settings the picker
    // has never heard of — exactly as they were. On a window attached to a
    // running session the file still gets the new colours, because they are
    // the person's own settings for the next window; the live preview there
    // moves only the window's chrome, since a session's screen colours are the
    // session's business and not this picker's to correct.
    void showThemePicker() {
        const Theme current = theme();
        const std::vector<ThemeField> fields = fieldsForTheme(current);
        pickThemeInTerminal(fields, [this](std::vector<ThemeField> changed, bool saved) {
            if (!saved || changed.empty())
                return; // cancel already put the old colours back; nothing changed
            std::vector<std::pair<std::string, std::string>> updates;
            for (const ThemeField& field : changed)
                updates.emplace_back(field.key, colourText(field.value));
            std::string error;
            if (saveConfigValues(config_.theme_path, updates, error,
                                 "# kai-term colours — edit here or with Ctrl+Shift+P in the window\n"))
                setStatusText("colours saved to " + config_.theme_path, 6000);
            else
                setStatusText("could not save colours: " + error, 8000);
        });
    }

    // The picker's rows, in the order it shows them: the five window colours
    // first because they are what you look at most, then the sixteen ANSI
    // palette entries in index order, which is the order every terminal
    // program that lists colours uses. The palette rows carry a plain-English
    // label (`bright magenta`) while their key stays the file's `color13` —
    // the person reads names, the file keeps the convention.
    static std::vector<ThemeField> fieldsForTheme(const Theme& theme) {
        std::vector<ThemeField> fields = {
            {"foreground", rgbOf(theme.foreground), "foreground"},
            {"background", rgbOf(theme.background), "background"},
            {"cursor", rgbOf(theme.cursor), "cursor"},
            {"selection", rgbOf(theme.selection), "selection"},
            {"accent", rgbOf(theme.accent), "accent"},
        };
        static const char* const kPaletteNames[16] = {
            "black",  "red",    "green",  "yellow", "blue",   "magenta", "cyan",  "white",
            "bright black", "bright red", "bright green", "bright yellow",
            "bright blue",  "bright magenta", "bright cyan", "bright white"};
        for (std::size_t i = 0; i < theme.palette.size(); ++i)
            fields.push_back({"color" + std::to_string(i), rgbOf(theme.palette[i]),
                              kPaletteNames[i]});
        return fields;
    }

    static Rgb rgbOf(const Color& colour) {
        return Rgb{colour.r, colour.g, colour.b};
    }

    std::string session_;
    // What the config file said, read once at construction. The colours went
    // into the theme; this stays because `confirm_quit` is asked at close, not
    // at startup, and re-reading the file every time a window closes would let
    // an edit mid-session change the answer under someone deciding in the
    // moment — the colours would not, and neither should this.
    Config config_;
    bool session_kept_ = false;
    // The answer to a close that was asked inside the terminal, and which of the
    // three answers it was: approved says the next close may through, keep_session
    // says the shell does not go with it. Consumed by that one close.
    bool close_approved_ = false;
    bool close_keep_session_ = false;
};

} // namespace kai
