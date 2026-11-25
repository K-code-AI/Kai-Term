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
inline WindowStart startWindowBackend(int rows, int cols,
                                      const std::string& view_mode = "side-by-side") {
    std::string error;
    const std::string name = startWindowSession(rows, cols, error);
    if (!name.empty()) {
        try {
            // A daemon from another build is not on our socket, so it cannot answer a
            // command we have and an older one does not — which is the point. It is
            // still holding the shells it started, and nothing we can list will show
            // them, so the window says so once instead of leaving them to be found in
            // `ps` next week.
            auto backend = std::make_unique<SessionBackend>(name, rows, cols);
            // A session this window has just started wears the view its person asked for.
            // Only a new one: the session read back from a save keeps the view it was saved
            // with, and that is decided by what is on disk rather than by this setting.
            if (view_mode == "tabs")
                backend->setViewMode(true);
            return {std::move(backend), name, foreignDaemonNotice(), rows, cols};
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
        installPaneConnector();
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
        installPaneConnector();
        // The person's own colours win here too. A window that attached to a
        // running session still reads the config and locks whatever colours it
        // names, so a previously-themed session comes back wearing the theme
        // rather than the colours it happened to save (Kaida, 2026-11-19: the
        // theme in the config "should supersede everything when existing").
        adoptConfig(false);
    }

    // Read the config file once, take what it says, and say out loud — on
    // stderr, at startup, before the first frame — anything it could not use.
    // A typo'd setting that is silently skipped is an afternoon of wondering;
    // one that names its own line is a fifteen-second fix.
    // `apply_body` is true for a window being built, where the config's font and
    // grid decide the window. It is false when re-reading after the picker saves
    // colours: the colours and their locks are refreshed from the file, but the
    // window's size is whatever the person has it at now — saving a colour must
    // not resize the terminal out from under them.
    // The colours of a config, worn now: the built-in theme, whatever that file
    // overrode, and the whole result locked so a program painting with OSC 10/11
    // cannot talk the window out of the person’s colours. Split out of
    // `adoptConfig` because a theme chosen in the settings menu has to be worn
    // before it is written, and a preview painted by different code than the real
    // thing is a preview that lies.
    void applyConfigColours(const Config& source, bool owns_screen) {
        Theme theme; // the built-in colours first, then whatever the file overrode
        const auto lift = [](Color& into, const std::optional<Rgb>& from) {
            if (from)
                into = Color{static_cast<std::uint8_t>(from->r), static_cast<std::uint8_t>(from->g),
                             static_cast<std::uint8_t>(from->b)};
        };
        lift(theme.foreground, source.foreground);
        lift(theme.background, source.background);
        lift(theme.cursor, source.cursor);
        lift(theme.selection, source.selection_background);
        lift(theme.selection_foreground, source.selection_foreground);
        lift(theme.accent, source.accent);
        for (std::size_t i = 0; i < theme.palette.size(); ++i)
            lift(theme.palette[i], source.palette[i]);
        // The two alphas travel with the colours rather than being applied to the
        // window as a whole, because they are not one thing: the ground the cells
        // sit on and the ink drawn over it get separate numbers, so a background you
        // can see the desktop through leaves the text as readable as it ever was.
        // `opacity` is the older name for the ground and still reads.
        theme.background_opacity = source.background_opacity.value_or(1.0);
        theme.foreground_opacity = source.foreground_opacity.value_or(1.0);
        applyTheme(theme, owns_screen);
        // A colour the person put in their config is now a LOCK in the screen: it
        // outranks whatever the session paints (OSC 4/10/11) and survives a
        // program's reset. This is the whole reason a saved theme used to wash out
        // — fish and prompt tools write OSC 10/11 at startup, and a snapshot of a
        // session carries its own colours — and it is why an attached window now
        // wears the person's colours instead of the session's. Colours the config
        // left unnamed are not locked and stay the session's to recolour.
        const auto liftOpt = [](const std::optional<Rgb>& from) -> std::optional<Color> {
            if (!from)
                return std::nullopt;
            return Color{static_cast<std::uint8_t>(from->r), static_cast<std::uint8_t>(from->g),
                         static_cast<std::uint8_t>(from->b)};
        };
        std::array<std::optional<Color>, 16> palette_locks;
        for (std::size_t i = 0; i < palette_locks.size(); ++i)
            palette_locks[i] = liftOpt(source.palette[i]);
        // The whole theme is the lock, named or not. A colour the file did not name
        // is still this window's colour — the theme's own default beneath the names —
        // and a program painting over it with OSC 10/11 is stating an opinion about
        // the person's desktop. Kaida's rule is flat: "a name in my config should
        // supersede everything … anyway windows hold the theme, not sessions". What
        // a program may still do is colour the cells it prints, which happens
        // inside the theme rather than in place of it. The same rule is what makes
        // the picker's preview honest for a palette entry: the row you are moving is
        // the colour the window actually uses.
        for (std::size_t i = 0; i < palette_locks.size(); ++i)
            palette_locks[i] = Color{theme.palette[i].r, theme.palette[i].g, theme.palette[i].b};
        setColourLocks(Color{theme.foreground.r, theme.foreground.g, theme.foreground.b},
                       Color{theme.background.r, theme.background.g, theme.background.b},
                       palette_locks);
        // The cursor's shape belongs with the colours, because it is the same kind of
        // decision: what the window looks like, held by the window rather than by the
        // bytes running through it. Named in the file, it outranks the session's
        // DECSCUSR — including the shape a snapshot replays on attach, which is how a
        // style chosen in the settings was lost as soon as a session was opened.
        // (Kaida, 2026-11-21: "Cursor style is lost upon a fresh instance of kai-term /
        // when loading a session … just like the theme, the Windows hold the cursor
        // style".)
        lockCursorStyle(source.cursor_style);
    }

    // Wear one theme's colours now, without writing anything: the settings file first,
    // then the named theme over it, which is how a startup would read the pair. The
    // window's own opacities travel with it, because they are a separate decision and
    // this menu may be holding an unsaved change to them.
    // (Kaida, 2026-11-20: "An option in our interactive settings (Ctrl + Shift + K) to
    // select from the themes inside ~/.config/kai-term/themes".)
    void previewTheme(const std::string& name) {
        Config themed;
        parseConfigFile(config_.settings_path, themed);
        parseConfigFile(resolveThemePath(name), themed);
        themed.background_opacity = theme().background_opacity;
        themed.foreground_opacity = theme().foreground_opacity;
        applyConfigColours(themed, ownsScreen());
    }

    // How many rows the settings menu has. A test that counts them by hand has to be
    // edited every time a row is added, and a test edited to match the code is a test
    // that has stopped saying anything about it.
    int settingsRowCount() const { return static_cast<int>(settingsRows().size()); }

    void adoptConfig(bool owns_screen, bool apply_body = true) {
        config_ = loadConfig();
        for (const std::string& problem : config_.problems)
            std::fprintf(stderr, "kai-term: %s\n", problem.c_str());
        applyConfigColours(config_, owns_screen);
        // The window's body, not just its colours: a config that says
        // `font_size = 14` means the cells are 14 pixels tall now, and a
        // config that says `cols = 100` means the opening window is as wide
        // as a hundred of them at that font. Font first, size second — the
        // other order would measure the hundred columns with the old ruler.
        // Skipped when re-reading after a colour save, so picking a colour
        // never snaps the window back to the configured grid.
        if (apply_body) {
            if (config_.font_size)
                setFontPixelSize(*config_.font_size);
            // Padding before the grid: it changes how many cells the window holds,
            // so sizing to `cols`/`rows` after it is the order that lands on the
            // grid the file asked for rather than one column away from it.
            if (config_.padding)
                setPadding(*config_.padding);
            // The divider, on the same terms as the padding: the file's number is the line's
            // thickness, and a file that says nothing leaves the built-in single pixel.
            if (config_.divider_width)
                setDividerWidth(*config_.divider_width);
            if (config_.scrollback)
                setHistoryLimit(*config_.scrollback);
            if (config_.cursor_style)
                setCursorStyle(*config_.cursor_style);
            // The blink, on the same terms as the shape: what the file says is what the
            // window does. A file that says nothing says blink, because that is what a
            // terminal does until its person says otherwise; a file that says 0 means it.
            setCursorBlink(config_.cursor_blink.value_or(kDefaultCursorBlinkMs));
            // Whether the control lists its keys while it waits. Off only when the file
            // says so; no line means the hints show, as they do for a person learning.
            setShowKeyHints(config_.keybind_hints != "off");
            if (config_.cols || config_.rows)
                resize(sizeForGrid(config_.rows.value_or(rows()), config_.cols.value_or(cols())));
        }
        // No `setWindowOpacity()` here, deliberately: it asks the X11 window manager
        // for `_NET_WM_WINDOW_OPACITY`, which a Wayland compositor has no equivalent
        // of — so on a Wayland desktop the setting was accepted, remembered, and
        // quietly done nothing with. And where it does work it dims the whole window,
        // glyphs included, which is a watermark rather than a transparent terminal.
        // The real thing is per-pixel and happens in `TerminalWidget::setTransparency`.
    }

    const std::string& sessionName() const { return session_; }

    // Show the places of a session side by side without being told to. The window
    // cannot open a connection of its own — it does not know a session or a daemon
    // exists — so it is handed a way to ask for one, and uses it the moment the tab
    // bar says the places are laid out beside each other.
    //
    // The neighbour attaches at the window's whole size, never its cell: the session
    // takes the size of the smallest window looking at it, so a view that asked for
    // forty columns would shrink the place being looked at to forty as well. Its own
    // screen is then the cell's size, which the window knows from the bar. A session
    // that has gone while a neighbour was being opened is met with a null connection,
    // which the window treats as "no neighbour" rather than as a crash.
    void installPaneConnector() {
        setPaneConnector([this](int pane) -> std::unique_ptr<kai::TerminalBackend> {
            if (session_.empty())
                return nullptr;
            try {
                auto neighbour = std::make_unique<SessionBackend>(session_, rows(), cols());
                neighbour->showPane(pane);
                return neighbour;
            } catch (const std::exception&) {
                return nullptr;
            }
        });
    }
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

    // Ctrl+K then S: keep this shell, and start a fresh one in the same window.
    //
    // The window does not close. The thing a person is doing when they press this is
    // not "I have finished with this window" but "I have finished with this shell", and
    // the second one does not need a new window to carry on in.
    //
    // The kept session is renamed into the range a typed command uses, because the
    // `w` names are for shells nobody decided to keep and this one is now a thing they
    // did decide about. Renaming is not fatal if it fails: the session is kept under
    // the name it has, since keeping it is the part that matters.
    // Whether saving a session asks what to call it. On unless the settings file says off:
    // a key whose whole job is "keep this" should not quietly decide the name it will be
    // found by, and the person who has already decided says so once in the file.
    bool asksForSessionName() const { return config_.save_name_prompt != "off"; }

    void saveCurrentSession() {
        if (session_.empty()) {
            setStatusText("no session daemon, so nothing was kept", 6000);
            return;
        }
        // Ask what to call it. A session that comes back as `w14` is a session that came
        // back unnamed, and the reason to keep one is to find it again later. The question
        // can be turned off in the settings for somebody who wants the keypress to keep the
        // shell and get on with the next one; the answer is then the name the window would
        // have picked, exactly as it always was.
        // (Kaida, 2026-11-24: "saving a session should now prompt for a name (can be
        // disabled in setting)".)
        if (asksForSessionName()) {
            const std::string automatic = nextKeptName();
            askNameInTerminal("What should this session be called?",
                              "This window moves on to a new shell; the one you are in keeps running.",
                              automatic,
                              [this, automatic](std::string name) {
                                  // Enter with nothing typed is an answer rather than a
                                  // refusal: it means "the name you were going to pick".
                                  keepSessionUnder(name.empty() ? automatic : name);
                              });
            return;
        }
        keepSessionUnder(nextKeptName());
    }

    // Keep the shell this window is showing, under the name it is to be found by, and
    // start a fresh one in the window. Split out of `saveCurrentSession` so the name can
    // arrive from the question above or from the setting that skips it.
    void keepSessionUnder(const std::string& wanted) {
        if (session_.empty())
            return; // the question outlived the session, which is the one race worth closing
        std::string error;
        std::string saved_as = session_;
        if (!wanted.empty() && saveSession(session_, wanted, error))
            saved_as = wanted;
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

    // Ctrl+K then A: the sessions you could go back to, listed inside the terminal
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
                          "Ctrl+K then S keeps this shell, and it will be here when you "
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
                          // The same keeping Ctrl+K then S does, aimed at the
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
                      " — Ctrl+K then S keeps it");
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
        on_settings_menu = [this] { showSettingsMenu(); };
    }

    // Choose the colours inside the terminal, with the terminal itself as the preview, and
    // write what you chose back to the config file.
    //
    // It has no key of its own: it is what Enter on the settings menu's `theme` row opens.
    // One key fewer to remember, and the colours are found where the other decisions are.
    // (Kaida, 2026-11-20: "Instead of having a keybind for the Theme Editor Ctrl+shift+P,
    // the Theme editor should be an item in the setting menu under Theme. It would make
    // things more coherent"; "ctrl+shift+p, does indeed go away".)
    //
    // `opening` is the theme the editor is being opened on, and its name goes in the
    // field at the bottom. That is a parameter rather than `config_.theme` because the
    // settings menu can be standing on a theme it has not saved yet: walk the row from
    // `theme1` to `theme2` and the window already wears theme2, while the file still says
    // theme1. Opening the editor on the file's answer put theme1 in the name field, and a
    // person saving the edit they came to make to theme2 wrote it over theme1 instead.
    // (Kaida, 2026-11-21: "the name field at the bottom is still theme1 so an unaware user
    // will overwrite theme1 with the edit intended for theme2".)
    //
    // The picker opens on the colours this window wears now, so what you see is
    // a starting point and not a lie. The widget hands back only the colours
    // that actually moved, so a save rewrites those lines and leaves the rest of
    // somebody's file — comments, alignment, settings the picker has never heard
    // of — exactly as they were. The preview is authoritative while you edit: it
    // moves the same colour LOCK a config file sets, so a colour you are choosing
    // is shown over the session rather than washed out by it, whether this window
    // owns its screen or attached to one. After a save the config is re-read, so
    // the locked set becomes exactly the colours the file now names.
    // `on_done` runs whether the editor saved or was cancelled, after its result has been
    // written — which is what lets the settings menu that opened it come back to the row
    // it came from, saying whatever the editor just made true.
    void showThemePicker(const std::string& opening,
                         std::function<void(bool saved)> on_done = {}) {
        const Theme current = theme();
        const std::vector<ThemeField> fields = fieldsForTheme(current);
        const std::string loaded = themeName(opening.empty() ? config_.theme : opening);
        pickThemeInTerminal(
            fields, loaded,
            [](const std::string& name) {
                std::ifstream probe(resolveThemePath(name));
                return probe.good();
            },
            [this, fields, loaded, on_done](std::vector<ThemeField> now, bool saved,
                                            std::string name) {
                const auto done = [&on_done, saved] {
                    if (on_done)
                        on_done(saved);
                };
                if (!saved) {
                    done(); // cancel already put the old colours back
                    return;
                }
                // A different name is a different file, and a new file wants every
                // colour the window is wearing rather than only the ones that moved.
                // The same name keeps the older, quieter rule: rewrite the lines that
                // changed and leave the rest of somebody's file — comments, alignment,
                // settings the picker has never heard of — exactly as they were.
                const bool renamed = name != loaded;
                std::vector<std::pair<std::string, std::string>> colours;
                for (std::size_t i = 0; i < now.size() && i < fields.size(); ++i) {
                    const Rgb& was = fields[i].value;
                    const Rgb& is = now[i].value;
                    if (renamed || was.r != is.r || was.g != is.g || was.b != is.b)
                        colours.emplace_back(now[i].key, colourText(now[i].value));
                }
                const std::string path = resolveThemePath(name);
                if (colours.empty() && !renamed) {
                    done(); // nothing moved; the file stays exactly as it was
                    return;
                }
                std::string error;
                // A theme is written without the `=`: that is the layout theme files use
                // now, and writing a second copy of every colour in the old shape is how a
                // save came to leave a theme holding each of its colours twice.
                // (Kaida, 2026-11-21.)
                bool wrote = saveConfigValues(
                    path, colours, error,
                    "# kai-term colours — edit here, or in the window: settings (Ctrl+K then K), then Enter on the theme row\n",
                    /*equals_sign=*/false);
                // A new theme is only worn once the settings file says to wear it. The
                // colours went in above; this is the line that points at them, and it
                // goes into `kai-term.conf`, where the decisions live and not into the
                // colours themselves.
                if (wrote && renamed)
                    wrote = saveConfigValues(config_.settings_path, {{"theme", name}}, error);
                if (!wrote) {
                    setStatusText("could not save colours: " + error, 8000);
                    done();
                    return;
                }
                config_.theme = name;
                setStatusText(renamed ? "theme saved to " + path
                                      : "colours saved to " + path,
                              6000);
                // Re-read so the locks match the file that is now on disk: the
                // colours just written become locked, and any colour the preview
                // had locked but not saved goes back to the session's authority.
                //
                // `false` for the body, and that is the whole of the fix for the
                // window jumping size on a save: re-reading is about colours, and
                // the second argument is what keeps the config's `cols`/`rows` and
                // font from being applied a second time and snapping the window
                // back to the configured grid while the person is looking at it
                // (Kaida, 2026-11-19: "it resize my terminal when a new theme is
                // saved"). The size a window is at now is the size it keeps — and
                // the transparency the preview was already showing is re-applied,
                // because that is a colour decision, not a body one.
                adoptConfig(ownsScreen(), false);
                done();
            });
    }

    // The picker's rows, in the order it shows them: the five window colours
    // first because they are what you look at most, then the sixteen ANSI
    // palette entries in index order, which is the order every terminal
    // program that lists colours uses. The palette rows carry a plain-English
    // label (`bright magenta`) while their key stays the file's `color13` —
    // the person reads names, the file keeps the convention.
    static std::vector<ThemeField> fieldsForTheme(const Theme& theme) {
        // The window's own colours first, in the order they are noticed: the ground, the
        // ink on it, the colour that means `this one`, the cursor, and the two halves of a
        // selection. (Kaida, 2026-11-21: "Foreground background cursor ... can stay at the
        // top but in this order: Background, Foreground, Accent, Cursor, Selection
        // Background, Selection Foreground.")
        std::vector<ThemeField> fields = {
            {"background", rgbOf(theme.background), "background"},
            {"foreground", rgbOf(theme.foreground), "foreground"},
            {"accent", rgbOf(theme.accent), "accent"},
            {"cursor", rgbOf(theme.cursor), "cursor"},
            // The file says `selection_background`, the screen says `selection`: the names
            // on the screen are the ones a person reads, and the ones in the file are the
            // ones kitty already uses. "selection background" is the name in the file and
            // the name on the screen now, because a row called `selection` next to one
            // called `selection foreground` leaves you guessing which half is which.
            {"selection_background", rgbOf(theme.selection), "selection background"},
            {"selection_foreground", rgbOf(theme.selection_foreground), "selection foreground"},
        };
        static const char* const kPaletteNames[16] = {
            "black",  "red",    "green",  "yellow", "blue",   "magenta", "cyan",  "white",
            "bright black", "bright red", "bright green", "bright yellow",
            "bright blue",  "bright magenta", "bright cyan", "bright white"};
        // A colour and its bright version next to each other. They are one colour at two
        // strengths, and sixteen rows in file order meant scrolling the length of the
        // panel to compare a pair.
        // (Kaida, 2026-11-21: "The color should be grouped togheter by "color" ...
        // Black, Bright Black, Red, Bright Red, Green, Bright Green ...".)
        const std::size_t palette_size = theme.palette.size();
        for (std::size_t i = 0; i < 8 && i < palette_size; ++i) {
            fields.push_back({"color" + std::to_string(i), rgbOf(theme.palette[i]),
                              kPaletteNames[i]});
            if (i + 8 < palette_size)
                fields.push_back({"color" + std::to_string(i + 8), rgbOf(theme.palette[i + 8]),
                                  kPaletteNames[i + 8]});
        }
        return fields;
    }

    // Ctrl+K then K: the settings the file holds, walked inside the window.
    //
    // The same shape as the colour picker, for the same reason: the decisions are
    // in a Qt-free model, the pixels are the widget's, and the preview is the
    // window, so changing the font shows you the font. What a menu has over an
    // editor is that a row cannot hold a value its own setting would refuse. Each
    // row knows the range it accepts, or the short list of answers it has, and the
    // keys, the row's `-`/`+`, and a click all go through the same clamps — so
    // there is no way to leave this menu with a font size of 4000 or a
    // `confirm_quit` that is not one of the three words the file takes.
    // What leaving the settings menu without saving puts back. It is a member and not a
    // local because the colour editor can be opened from inside the menu and can *save*
    // while the menu waits: a decision somebody made and wrote to a file is not one to
    // undo on the way out, so the editor moves the baseline it was opened from.
    // (Kaida, 2026-11-20: "the setting keeps your change until you save or cancel them,
    // even when in a sub-menu".)
    struct MenuBaseline {
        int font_size = 0;
        double padding = 0.0;
        int scrollback = 0;
        CursorStyle cursor_style = CursorStyle::Block;
        int cursor_blink = 0;
        double ground = 1.0;
        double ink = 1.0;
        std::string confirm_quit;
        std::string theme;
        bool open = false;
    };

    void showSettingsMenu() {
        const std::vector<SettingRow> rows = settingsRows();
        // What the window is doing before anybody touches it, so that leaving
        // without saving puts every live change back. A preview you cannot undo is
        // not a preview, it is a commit.
        menu_baseline_ = MenuBaseline{fontPixelSize(),
                                      padding(),
                                      historyLimit(),
                                      cursorStyle(),
                                      cursorBlink(),
                                      theme().background_opacity,
                                      theme().foreground_opacity,
                                      config_.confirm_quit,
                                      config_.theme,
                                      true};

        pickSettingsInTerminal(
            rows,
            [this](std::vector<SettingRow> changed, bool saved) {
                const MenuBaseline was = menu_baseline_;
                menu_baseline_ = MenuBaseline{};
                if (!saved) {
                    setFontPixelSize(was.font_size);
                    setPadding(was.padding);
                    setHistoryLimit(was.scrollback);
                    setCursorStyle(was.cursor_style);
                    setCursorBlink(was.cursor_blink);
                    // …and the lock back to whatever the file says, so a shape the menu
                    // previewed does not outlive the menu: without this a cancelled shape
                    // would keep out every DECSCUSR the session writes afterwards.
                    lockCursorStyle(config_.cursor_style);
                    config_.confirm_quit = was.confirm_quit;
                    // The colours the window was wearing when the menu opened, put back
                    // from the file they came from. Leaving without saving has to
                    // un-preview the theme as much as the font — unless the colour editor
                    // saved some of them in the meantime, which is why this is a member
                    // that the editor is allowed to move and not a captured local.
                    previewTheme(was.theme);
                    applyOpacity(was.ground, was.ink);
                    return;
                }
                if (changed.empty())
                    return; // nothing moved; the file stays exactly as it was
                std::vector<std::pair<std::string, std::string>> updates;
                for (const SettingRow& row : changed)
                    updates.emplace_back(row.key, settingFileText(row));
                std::string error;
                if (!saveConfigValues(config_.settings_path, updates, error)) {
                    setStatusText("could not save settings: " + error, 8000);
                    return;
                }
                setStatusText("settings saved to " + config_.settings_path, 6000);
                // Re-read so the window's idea of the settings matches the file
                // that is now on disk — the close prompt takes `confirm_quit` from
                // there, for one. The body is deliberately not re-applied: the
                // values you just chose are the ones the window is already showing,
                // and saving must not move the window a second time.
                adoptConfig(ownsScreen(), false);
            },
            [this](const SettingRow& row) { applySetting(row); },
            [this](const SettingRow& row) {
                if (row.key != "theme")
                    return;
                // Enter on the theme row: the colour editor comes up over the menu, and the
                // menu waits with everything you had changed still changed. Coming back out
                // you are on the theme row again, and it says what the editor left true.
                // (Kaida, 2026-11-20: "the Theme editor should be an item in the setting
                // menu under Theme"; "the setting keeps your change until you save or cancel
                // them, even when in a sub-menu".)
                const std::string worn_before = config_.theme;
                // The row's own answer, which is the theme the window was made to wear a
                // moment ago and the file a save here should be writing to.
                const std::string chosen = row.value >= 0 &&
                                                   static_cast<std::size_t>(row.value) <
                                                       row.choices.size()
                                               ? row.choices[static_cast<std::size_t>(row.value)]
                                               : config_.theme;
                showThemePicker(chosen, [this, worn_before](bool saved) {
                    resumeSettings();
                    if (!saved || !menu_baseline_.open)
                        return; // a cancel changed nothing to keep
                    // The editor wrote colours to a file, and the file is what the window
                    // now wears. Leaving the menu after that must not roll them back: the
                    // baseline the menu would put back moves to what was just committed.
                    menu_baseline_.theme = config_.theme;
                    menu_baseline_.ground = theme().background_opacity;
                    menu_baseline_.ink = theme().foreground_opacity;
                    if (config_.theme == worn_before)
                        return; // the editor wrote no name, so the row still has ours to say
                    // A new theme is a new name in the settings file, and the editor has
                    // already written it. The row takes the file's answer so that pressing
                    // `save` here cannot put the older name back over the top of it.
                    for (const SettingRow& fresh : settingsRows()) {
                        if (fresh.key == "theme") {
                            settingsRefreshRow(fresh.key, fresh.choices, fresh.value);
                            break;
                        }
                    }
                });
            });
    }

    // The menu's rows, read from the window rather than from the file: what the
    // person is looking at is the thing they are editing. Ranges are the file's
    // own — the same numbers the config parser refuses outside of — so a value you
    // can reach in the menu is a value the reader will accept tomorrow morning.
    std::vector<SettingRow> settingsRows() const {
        std::vector<SettingRow> rows;
        rows.push_back({"font_size", "Font size", SettingKind::Integer, fontPixelSize(), 6, 72,
                        2, {}, false, 1, "px"});
        rows.push_back({"background_opacity", "Background opacity", SettingKind::Percent,
                        percentFromFraction(theme().background_opacity), 1, 100});
        rows.push_back({"foreground_opacity", "Text opacity", SettingKind::Percent,
                        percentFromFraction(theme().foreground_opacity), 1, 100});
        rows.push_back({"padding", "Padding", SettingKind::Integer,
                        static_cast<int>(padding() + 0.5), 0, 64, 2, {}, false, 1, "px"});
        // The line between two places. Both directions answer to this one number, which is
        // the point of the row: a vertical divider and a horizontal one are the same
        // thickness here and on the screen, and 0 leaves the gap empty.
        rows.push_back({"divider_width", "Divider thickness", SettingKind::Integer,
                        static_cast<int>(dividerWidth()), 0, 16, 2, {}, false, 1, "px"});
        rows.push_back({"scrollback", "Scrollback history length", SettingKind::Integer,
                        historyLimit(), 0, 20000, 5, {}, false, 1, "Lines"});
        const int shape = cursorStyle() == CursorStyle::Block
                              ? 0
                              : (cursorStyle() == CursorStyle::Bar ? 1 : 2);
        rows.push_back({"cursor_style", "Cursor style", SettingKind::Choice, shape, 0, 0, 0,
                        {"block", "bar", "underline"}});
        // Milliseconds per half-blink, walked fifty at a time: the range is wide enough
        // that one millisecond a press is a walk of five thousand, and the difference
        // between two rates a hundred apart is not worth a press each.
        // (Kaida, 2026-11-21: "customizable blink speed ( 0 being no blinking)".)
        rows.push_back({"cursor_blink", "Cursor blink speed", SettingKind::Integer, cursorBlink(),
                        0, 5000, 4, {}, false, 50});
        const std::string policy = policyWord(closePolicy());
        int quit = 1; // the answer with no file and no environment saying otherwise
        for (const char* word : {"never", "running", "always"}) {
            if (policy == word)
                break;
            ++quit;
        }
        rows.push_back({"confirm_quit", "Ask before closing", SettingKind::Choice, quit, 0, 0, 0,
                        {"never", "running", "always"}});
        // Whether the control, while it waits for its key, shows the row of keys it
        // takes. On is the default — a thing you have not learned yet should say what it
        // wants — and off is for the person who has learned it and wants the screen back.
        int hints = config_.keybind_hints == "off" ? 0 : 1;
        rows.push_back({"keybind_hints", "Show Keybind Tooltips", SettingKind::Choice, hints, 0, 0, 0,
                        {"off", "on"}});
        // Whether keeping a session asks what to call it. On is the default, because the
        // name is what you type to find it again; off is the one-press keep the key used to
        // be, which takes the name the window would have picked.
        int name_prompt = config_.save_name_prompt == "off" ? 0 : 1;
        rows.push_back({"save_name_prompt", "Name sessions on save", SettingKind::Choice,
                        name_prompt, 0, 0, 0, {"off", "on"}});
        // Which view a session opened by a new window starts in. `Ctrl+K T` changes it for
        // the session in hand either way; this spares a person who lives in one view the
        // keystroke every single time they open a window.
        // (Kaida, 2026-11-25: "a setting that lets the user chose their default view (side
        // by side or Tabs)".)
        rows.push_back({"default_view_mode", "Default view", SettingKind::Choice,
                        config_.default_view_mode == "tabs" ? 1 : 0, 0, 0, 0,
                        {"side-by-side", "tabs"}});
        // Which colours the window is wearing. The answers are the names on disk, with
        // the default offered first even though it is not a file in `themes` — it is the
        // file the colours have always lived in, and a menu that hides the answer you are
        // already on is a menu that lies about the present.
        std::vector<std::string> themes = listThemes();
        const std::string loaded = themeName(config_.theme);
        if (std::find(themes.begin(), themes.end(), loaded) == themes.end())
            themes.insert(themes.begin() + 1, loaded); // a full path, or a deleted theme
        int theme_row = 0;
        for (std::size_t i = 0; i < themes.size(); ++i) {
            if (themes[i] == loaded)
                theme_row = static_cast<int>(i);
        }
        // The row both is the theme and opens the thing that makes one: stepping it
        // with the arrows previews a theme that already exists, Enter opens the editor
        // where you can make a new one.
        // (Kaida, 2026-11-20: "Instead of having a keybind for the Theme Editor
        // Ctrl+shift+P, the Theme editor should be an item in the setting menu under
        // Theme. It would make things more coherent".)
        rows.push_back({"theme", "Themes & Customization", SettingKind::Choice, theme_row, 0, 0, 0, themes, true});
        return rows;
    }

    // One row, applied to the live window. This is what makes the preview real:
    // the same setters the config file goes through at startup, called on every
    // keystroke and every click.
    void applySetting(const SettingRow& row) {
        if (row.key == "font_size")
            setFontPixelSize(row.value);
        else if (row.key == "padding")
            setPadding(row.value);
        else if (row.key == "divider_width")
            setDividerWidth(row.value);
        else if (row.key == "scrollback")
            setHistoryLimit(row.value);
        else if (row.key == "cursor_blink")
            setCursorBlink(row.value);
        else if (row.key == "cursor_style")
            // A shape chosen here is a decision about the window, so the window holds it
            // against the session for the preview as much as after the save.
            lockCursorStyle(std::optional<CursorStyle>(
                row.value == 0   ? CursorStyle::Block
                : row.value == 1 ? CursorStyle::Bar
                                 : CursorStyle::Underline));
        else if (row.key == "confirm_quit")
            config_.confirm_quit = settingFileText(row);
        else if (row.key == "keybind_hints")
            setShowKeyHints(row.value == 1);
        else if (row.key == "save_name_prompt")
            // Nothing to preview: the answer is about the next time the key is pressed.
            config_.save_name_prompt = settingFileText(row);
        else if (row.key == "default_view_mode")
            // Likewise about the next window, not this one: the view of the session in hand
            // is `Ctrl+K T`'s to change.
            config_.default_view_mode = settingFileText(row);
        else if (row.key == "theme")
            previewTheme(settingFileText(row));
        else if (row.key == "background_opacity")
            applyOpacity(fractionFromPercent(row.value), theme().foreground_opacity);
        else if (row.key == "foreground_opacity")
            applyOpacity(theme().background_opacity, fractionFromPercent(row.value));
    }

    void applyOpacity(double ground, double ink) {
        Theme next = theme();
        next.background_opacity = ground;
        next.foreground_opacity = ink;
        applyTheme(next, ownsScreen());
    }

    // The file's spelling of a row: a number for the rows that are numbers, a
    // fraction for the two percentages, and the word itself for the rows that are
    // one of a few answers. These are the words `loadConfig` reads back, which is
    // the test that matters — the menu cannot write a line its own reader would
    // spend startup complaining about.
    static std::string settingFileText(const SettingRow& row) {
        if (row.kind == SettingKind::Percent)
            return fractionText(row.value);
        if (row.kind == SettingKind::Choice) {
            if (row.choices.empty())
                return std::string();
            const std::size_t index = static_cast<std::size_t>(
                ((row.value % static_cast<int>(row.choices.size())) +
                 static_cast<int>(row.choices.size())) %
                static_cast<int>(row.choices.size()));
            return row.choices[index];
        }
        return std::to_string(row.value);
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
    // The settings menu's undo baseline, live only while that menu is up (or waiting
    // behind the colour editor it stepped aside for).
    MenuBaseline menu_baseline_;
    Config config_;
    bool session_kept_ = false;
    // The answer to a close that was asked inside the terminal, and which of the
    // three answers it was: approved says the next close may through, keep_session
    // says the shell does not go with it. Consumed by that one close.
    bool close_approved_ = false;
    bool close_keep_session_ = false;
};

} // namespace kai
