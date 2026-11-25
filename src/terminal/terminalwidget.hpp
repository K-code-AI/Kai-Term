#pragma once

#include "screen.hpp"
#include "keyencode.hpp"
#include "mouseencode.hpp"
#include "scrollbar.hpp"
#include "selection.hpp"
#include "text.hpp"
#include "backend.hpp"
#include "../config/colormixer.hpp"
#include "../config/settingspicker.hpp"
#include "../config/themepicker.hpp"

#include <poll.h>

#include <QWidget>
#include <QClipboard>
#include <QFont>
#include <QFontMetricsF>
#include <QChar>
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimerEvent>
#include <QColor>
#include <QRectF>
#include <QPen>
#include <QByteArray>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <QString>
#include <QStringList>
#include <QWindow>
#include <QSurfaceFormat>
#include <functional>
#include <QSizeF>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <utility>

namespace kai {

// Both carry the number of the shell that produced them. A window can be pointed at a
// different shell at runtime, and bytes from the one it left behind must not be
// painted onto the screen of the one it is looking at now — an event queue is not a
// pipe, and it does not empty itself just because the writer stopped.
struct TerminalOutputEvent : QEvent {
    explicit TerminalOutputEvent(std::string data, int generation)
        : QEvent(QEvent::None), data(std::move(data)), generation(generation) {}
    std::string data;
    int generation;
};

// The session's tab bar, as the daemon last described it. Not output: a session gaining a
// pane prints nothing on the pane being looked at, so nothing else would ever say so.
struct TerminalTabsEvent : QEvent {
    TerminalTabsEvent(int panes, int focused, bool tabs, std::vector<PaneRect> cells,
                      std::vector<int> numbers, int mine, int generation)
        : QEvent(QEvent::None),
          panes(panes),
          focused(focused),
          tabs(tabs),
          cells(std::move(cells)),
          numbers(std::move(numbers)),
          mine(mine),
          generation(generation) {}
    int panes;
    int focused;
    bool tabs;
    std::vector<PaneRect> cells;
    // Which number each place is called by, in the order the places are listed.
    std::vector<int> numbers;
    // Which place this window is being shown, as the session sees it; -1 = nobody said.
    int mine = -1;
    int generation;
};

struct TerminalExitEvent : QEvent {
    explicit TerminalExitEvent(int generation)
        : QEvent(QEvent::None), generation(generation) {}
    int generation;
};

// Output from a second place shown beside the one this window is looking at. It is
// a separate event because it feeds a separate screen: the bytes for the neighbour
// must never be painted onto the screen the window was built around. It carries the
// neighbour's own number for the same reason the others carry the shell's — a place
// taken away must not keep painting into a screen that has already been freed.
struct ExtraOutputEvent : QEvent {
    explicit ExtraOutputEvent(std::string data, int generation)
        : QEvent(QEvent::None), data(std::move(data)), generation(generation) {}
    std::string data;
    int generation;
};

// A place of the same session, drawn beside the one this window is looking at. It is
// deliberately small: a connection, a screen of its own, and a reader to keep that
// screen filled. Everything the window does for the place it was built around — the
// tab bar, the refusal, the clipboard, the queries — stays on the first connection,
// because those describe the session, not one half of it. A neighbour is a second pair
// of eyes on the same session, not a second window, and it is read-only: typing goes to
// the place being looked at, never to a neighbour. It scrolls on its own, because a
// window that scrolled one place by scrolling another would be showing the wrong history.
struct ExtraPane {
    std::unique_ptr<TerminalBackend> backend;
    Screen screen;
    std::thread reader;
    std::atomic<bool> running{false};
    std::atomic<bool> alive{false};
    int generation = 0;
    PaneRect cell;
    int pane = -1;
    int scroll = 0; // lines scrolled back from this place's own live bottom
    // The scroll this place was left at, waiting to be applied once its snapshot has
    // arrived — a view created before any history has fed in cannot be scrolled yet, and
    // applying it then would only be clamped back to the bottom.
    int pending_scroll = 0;
};

class TerminalWidget : public QWidget {
public:
    explicit TerminalWidget(int rows = 24, int cols = 80, std::string shell = {})
        : TerminalWidget(rows, cols, std::make_unique<PtyBackend>(rows, cols, std::move(shell))) {
    }

    // A window showing a session that lives somewhere else. Everything past this
    // point is the same: bytes in, screen out, keys back — what differs is on the far
    // side of the backend.
    TerminalWidget(int rows, int cols, std::unique_ptr<TerminalBackend> backend)
        : backend_(std::move(backend)), screen_(rows, cols),
          rows_(rows), cols_(cols), running_(true), shell_alive_(true)
    {
        // The blink turns the cursor off and on again in the one cell it lives in. It
        // is a wall-clock thing rather than a byte from the session, so it belongs to
        // the window and not to the parser.
        connect(&cursor_blink_timer_, &QTimer::timeout, this, [this] {
            cursor_lit_ = !cursor_lit_;
            repaintCursorCell();
        });

        // A state diary, for a broken screen that only happens in front of someone
        // else. With KAI_TERM_STATE_FILE set, this widget writes what it currently
        // believes — geometry, grid, cursor, scroll, and the rows that have text on
        // them — every second and a half, and whenever the window is resized or a
        // program takes or gives up the screen. Unset, it writes nothing and costs
        // nothing. It exists because a picture of a bug and a description of one are
        // both worse than the numbers behind it.
        if (const char* diary = std::getenv("KAI_TERM_STATE_FILE"); diary && *diary) {
            diary_alt_ = screen_.inAlternateScreen();
            connect(&state_diary_timer_, &QTimer::timeout, this, [this] {
                const bool alt = screen_.inAlternateScreen();
                writeStateDiary(alt == diary_alt_ ? "tick"
                                                  : (alt ? "screen taken" : "screen given back"));
                diary_alt_ = alt;
            });
            state_diary_timer_.start(1500);
            writeStateDiary("start");
        }
        font_ = QFont("Monospace");
        font_.setPixelSize(14);
        updateCellPixels();
        setFocusPolicy(Qt::StrongFocus);
        // Any-event mouse reporting (?1003) needs motion with no button held.
        setMouseTracking(true);

        // The screen the backend is already holding, before its reader is let loose: a
        // window built around a session should have that session's screen the moment it
        // exists, not whenever an event finally reaches a widget that may not be shown yet.
        feedWhatIsHeld();
        startReader();
    }

    // The colours the window wears when nobody has said otherwise, and the
    // ones a config file chose. These are not the screen's colours — an
    // application repaints those with escape sequences whenever it likes —
    // they are what a blank screen and unstyled text wear, plus the two the
    // window itself owns: the highlight under a dragged selection and the
    // accent a dialog puts on the answer its focus is on.
    struct Theme {
        Color foreground{220, 220, 220};
        Color background{30, 30, 30};
        Color cursor{220, 220, 220};
        Color selection{62, 96, 168};
        // The ink on a selected cell. It was a constant in the painting for as long as
        // there was a painting — a light grey that stayed light grey on a theme whose
        // selection colour was already pale — and a theme can say better now.
        Color selection_foreground{240, 240, 240};
        Color accent{110, 160, 255};
        // The sixteen ANSI colours as this window reads them. The built-in
        // answer is the built-in palette — a theme that says nothing about
        // `red` leaves red the colour every program expects.
        std::array<Color, 16> palette = Screen::basePalette();
        // How solid the two halves of the window are against whatever is behind
        // it: the ground the cells sit on, and the ink drawn over it. They are two
        // settings rather than one because the point of a see-through terminal is
        // the desktop behind the background, and the point of the text is that you
        // can read it. 1 is solid, which is what a window that says nothing is.
        double background_opacity = 1.0;
        double foreground_opacity = 1.0;
    };

    // Applying a theme is a decision about the next screen, not a rewrite of
    // the last one: the defaults move, cells already on screen keep what they
    // were written with, and the window repaints what it has. That is the
    // same rule an escape-sequence recolour follows, which is why a theme
    // that painted existing cells would be two authorities over one grid.
    // The theme travels whether or not this window started the shell. A window
    // holds the theme; a session does not (Kaida, 2026-11-20: "Theme is lost when
    // attaching a session, which should not happen, a session doesn't hold theme
    // value a windows should… anyway windows hold the theme, not sessions"). It
    // used to be that a window which attached left the screen's own defaults
    // alone, on the grounds that the session had carried them across a process
    // boundary on purpose. But a snapshot's OSC 10/11 is a stored opinion from
    // some other moment, and the colours were chosen by whoever launched this
    // window. What the session painted into the cells still stands: a theme is a
    // decision about the next screen, not a rewrite of the last one. The palette
    // follows the same rule, and the locks a window puts on its own colours (see
    // `setColourLocks`) are what make it hold against live output as well as
    // against a snapshot.
    void applyTheme(const Theme& theme, bool owns_screen) {
        theme_ = theme;
        owns_screen_ = owns_screen;
        screen_.setDefaults(theme.foreground, theme.background);
        screen_.setThemePalette(theme.palette);
        setTransparency(theme.background_opacity, theme.foreground_opacity);
        update();
    }
    const Theme& theme() const { return theme_; }

    // What the screen underneath the theme is actually wearing. A window's theme
    // and a screen's defaults are two different things, and "the theme is lost" is
    // a complaint about the second one, so a test has to be able to ask it.
    Color screenDefaultForeground() const { return screen_.defaultForeground(); }
    Color screenDefaultBackground() const { return screen_.defaultBackground(); }
    Color screenPaletteColour(int index) const {
        return screen_.paletteColor(index < 0 ? 0 : (index > 15 ? 15 : index));
    }

    // ---------------------------------------------------------------------
    // Transparency, the real kind.
    //
    // `QWidget::setWindowOpacity()` is the obvious call and it is the wrong one
    // twice over. It is X11-only — it asks the window manager for
    // `_NET_WM_WINDOW_OPACITY`, and on a Wayland compositor (here: niri) there is
    // no such request, so the number is accepted and nothing happens, which is
    // exactly how "no matter the setting it's fully opaque" reads from the inside.
    // And when it does work it multiplies the whole window, so the text goes
    // translucent along with the background and the terminal becomes a watermark.
    //
    // The real kind is per-pixel: the surface carries an alpha channel, the
    // background is painted at the background's alpha, and the ink is painted at
    // the ink's. The compositor then blends that with whatever is behind, so the
    // desktop comes through the ground while the glyphs stay as crisp as the day
    // they were drawn. Two numbers because that is two decisions.
    // ---------------------------------------------------------------------
    void setTransparency(double background, double foreground) {
        background_opacity_ = std::clamp(background, 0.0, 1.0);
        foreground_opacity_ = std::clamp(foreground, 0.0, 1.0);
        const bool translucent = background_opacity_ < 1.0 || foreground_opacity_ < 1.0;
        if (translucent != translucent_) {
            // The attribute is what gives the surface an alpha channel and stops
            // Qt filling an opaque ground under us. Asking the window format for
            // alpha as well covers a window that is already on screen when the
            // setting moves, which is what the picker does while previewing.
            setAttribute(Qt::WA_TranslucentBackground, translucent);
            if (QWindow* window = windowHandle()) {
                QSurfaceFormat format = window->format();
                format.setAlphaBufferSize(translucent ? 8 : 0);
                window->setFormat(format);
            }
            translucent_ = translucent;
        }
        update();
    }
    double backgroundOpacity() const { return background_opacity_; }
    double foregroundOpacity() const { return foreground_opacity_; }

    // The two halves of every colour this window paints. `ground` is what cells
    // and panels sit on and is the half that lets the desktop through; `ink` is
    // the glyphs, the cursor, the rules and the words, and stays solid unless the
    // person asked otherwise. With both at 1 these hand back exactly the colour
    // they were given, so an opaque window paints the way it always did.
    QColor ground(const Color& c, int alpha = 255) const {
        return QColor(c.r, c.g, c.b, scaleAlpha(alpha, background_opacity_));
    }
    QColor ink(const Color& c, int alpha = 255) const {
        return QColor(c.r, c.g, c.b, scaleAlpha(alpha, foreground_opacity_));
    }
    // The same two, for the handful of colours the window invents out of numbers
    // rather than out of the theme — the scrollbar's thumb, the status line's box.
    QColor ground(int r, int g, int b, int alpha = 255) const {
        return QColor(r, g, b, scaleAlpha(alpha, background_opacity_));
    }
    QColor ink(int r, int g, int b, int alpha = 255) const {
        return QColor(r, g, b, scaleAlpha(alpha, foreground_opacity_));
    }

    // The overlay panels paint everything at full strength: their box, their text,
    // their buttons. Transparency is a decision about the terminal — how much of
    // the desktop shows through the ground the cells sit on — and it was never a
    // decision about the menus. A panel that fades with the window is a panel you
    // end up reading through your wallpaper, which is the one thing a panel is not
    // allowed to be. (Kaida, 2026-11-20: "oppacity shouldn't affect the menu for
    // better readability".)
    static QColor panel_ground(const Color& c, int alpha = 255) {
        return QColor(c.r, c.g, c.b, alpha);
    }
    static QColor panel_ground(int r, int g, int b, int alpha = 255) {
        return QColor(r, g, b, alpha);
    }
    static QColor panel_ink(const Color& c, int alpha = 255) {
        return QColor(c.r, c.g, c.b, alpha);
    }
    static QColor panel_ink(int r, int g, int b, int alpha = 255) {
        return QColor(r, g, b, alpha);
    }

  private:
    static int scaleAlpha(int alpha, double opacity) {
        if (opacity >= 1.0)
            return alpha;
        return static_cast<int>(alpha * opacity + 0.5);
    }

  public:

    // Name the colours the config file supplied, so they outrank the session.
    // A field left as nullopt stays unlocked and the session may still set it.
    // This is what makes a saved theme survive a shell that writes OSC 10/11 at
    // startup, and what makes an attached window wear the person's colours
    // rather than the session's.
    void setColourLocks(const std::optional<Color>& foreground,
                        const std::optional<Color>& background,
                        const std::array<std::optional<Color>, 16>& palette) {
        screen_.lockDefaults(foreground, background);
        screen_.lockPalette(palette);
        // Held here as well as in the screen, because the screen is not permanent:
        // starting a fresh shell in this window builds a new one, and locks that
        // lived only in the old screen went away with it. That is how a session
        // spawn was able to get rid of the theming — the window still had its
        // theme, and the colours that outrank the session were not in the screen
        // the session had just arrived in.
        colour_locks_ = ColourLocks{foreground, background, palette, true};
        update();
    }
    // Whether this window may set the screen's own colours (it started the
    // shell) or only its chrome (it attached). Read by the picker after a save.
    bool ownsScreen() const { return owns_screen_; }

    // The font the config asked for, taken before the first frame. Cells are
    // squares of pixels, not wishes: the grid is worked out again from the
    // window's pixels afterwards, because a bigger font genuinely means
    // fewer columns in the same window, which is what a bigger font means
    // in every terminal that ever was.
    void setFontPixelSize(int pixels) {
        font_.setPixelSize(pixels);
        applyGridGeometry();
        update();
    }
    int fontPixelSize() const { return font_.pixelSize(); }

    // The window size that shows exactly `want_rows` by `want_cols` cells at
    // the current font — the size to ask for when the config says the window
    // opens at 100 columns by 30 rows and means it. The extra pixel is floor
    // division insurance: a column lost to rounding is a column the config
    // promised and the window did not deliver.
    QSize sizeForGrid(int want_rows, int want_cols) const {
        const double pad = activePadding();
        return QSize(static_cast<int>(want_cols * cellWidth() + 2 * pad) + 1,
                     static_cast<int>(want_rows * cellHeight() + 2 * pad) + 1);
    }

    // The third shape the terminal draws over its own grid: the theme
    // picker. Every decision in it lives in `ThemePicker`, which has no Qt in
    // it and is tested headless; the widget only paints what the model says
    // and hands it the keys. The preview is not a picture of the terminal —
    // it is the terminal: each keystroke reapplies the theme through the same
    // `applyTheme` a config file goes through, with the same ownership rule,
    // so what you watch choosing is exactly what you would get.
    // The panel now holds a decision it did not used to: the NAME the colours go into.
    // `loaded` is the theme being worn, which the field opens on so the panel says what
    // is true; `name_taken` answers whether a name the person typed is already a file,
    // which is the panel's business because it is the panel that has to ask before it
    // writes over one. What comes back through `finish` is every colour as the panel
    // leaves it and the name to put them in — the caller decides which lines that is
    // worth rewriting.
    // (Kaida, 2026-11-20: themes as files, "the picker's name field is always visible —
    // saving under the loaded name asks to overwrite, a new name writes a new theme".)
    void pickThemeInTerminal(
        std::vector<ThemeField> fields, std::string loaded,
        std::function<bool(const std::string& name)> name_taken,
        std::function<void(std::vector<ThemeField> now, bool saved, std::string name)> finish) {
        if (overlay_.active || picker_)
            return; // a dialog and a picker do not stack; one is always the wrong one
        picker_ = std::make_unique<ThemePicker>(std::move(fields));
        picker_before_ = theme_;
        picker_saved_locks_ = screen_.colourLocks(); // handed back untouched on a cancel
        picker_loaded_name_ = std::move(loaded);
        picker_name_ = pickerDisplayName(picker_loaded_name_);
        picker_name_taken_ = std::move(name_taken);
        picker_overwrite_ = false;
        picker_editing_ = false;
        picker_caret_ = 0;
        // The keys start on the first row. Where they were last time belongs to that
        // panel, which is gone: an overlay that opens with the keys already on `save`
        // has hidden the fact that the last visit ended there.
        picker_focus_ = 0;
        picker_finish_ = std::move(finish);
        update();
    }
    bool pickerActive() const { return picker_ != nullptr; }
    // The colour list's own rows: how many, and what each is called on the screen. The
    // screen's names and the file's names are not the same list — the row that says
    // `black` writes `color0` — so a check has to be able to ask which of the two it is
    // looking at.
    int pickerRowCount() const {
        return picker_ ? static_cast<int>(picker_->fields().size()) : 0;
    }
    QString pickerRowLabel(int model_row) const {
        if (!picker_ || model_row < 0 || model_row >= pickerRowCount())
            return QString();
        const ThemeField& field = picker_->fields()[static_cast<std::size_t>(model_row)];
        return QString::fromStdString(field.label.empty() ? field.key : field.label);
    }
    QString pickerRowKey(int model_row) const {
        if (!picker_ || model_row < 0 || model_row >= pickerRowCount())
            return QString();
        return QString::fromStdString(picker_->fields()[static_cast<std::size_t>(model_row)].key);
    }

    // The fourth shape over the grid: the settings menu. Same shape as the colour
    // picker and the same rule — the decisions live in `SettingsPicker`, which has
    // no Qt in it, the pixels live here, and the preview is the window. `preview`
    // is handed each row the moment it moves, so the window changes while you are
    // looking at it; `finish` says whether any of it should be written down.
    // `open` is for a row that opens a panel of its own rather than only holding a
    // value: Enter on it steps the menu aside and hands the row over. The menu is not
    // finished, so nothing it holds unsaved is lost while the other panel is up.
    void pickSettingsInTerminal(
        std::vector<SettingRow> rows,
        std::function<void(std::vector<SettingRow>, bool saved)> finish,
        std::function<void(const SettingRow&)> preview,
        std::function<void(const SettingRow&)> open = {}) {
        if (overlay_.active || picker_ || settings_)
            return; // one thing at a time over the grid; a stack is always the wrong one
        settings_suspended_ = false;
        settings_ = std::make_unique<SettingsPicker>(std::move(rows));
        // As in the picker: a panel opens with the keys on its first row. Which word the
        // last visit happened to end on is not this visit's business.
        settings_focus_ = 0;
        settings_finish_ = std::move(finish);
        settings_preview_ = std::move(preview);
        settings_open_ = std::move(open);
        update();
    }
    bool settingsActive() const { return settings_ != nullptr && !settings_suspended_; }

    // The colour mixer, opened over one colour of the picker with Space or a click
    // on its swatch. It is a detour, not a new screen: the list stays underneath,
    // and the colour comes back into the row it came from.
    bool mixerActive() const { return mixer_ != nullptr; }
    void mixColour(Rgb start, std::function<void(Rgb, bool)> finish,
                   std::function<void(Rgb)> preview) {
        mixer_ = std::make_unique<ColorMixer>(start);
        mixer_focus_ = 0;
        mixer_drag_ = -1;
        mixer_finish_ = std::move(finish);
        mixer_preview_ = std::move(preview);
        update();
    }

    // Which line of an open overlay the keys are on, as a pair of (row, stop) where
    // stop 0 is the rows, 1 is `save` and 2 is `cancel`. The panel shows this; a test
    // should read it rather than infer it from which setting happened to move.
    std::pair<int, int> settingsCursor() const {
        return settings_ ? std::pair<int, int>{settings_->row(), settings_focus_}
                         : std::pair<int, int>{-1, -1};
    }
    std::pair<int, int> mixerCursor() const {
        return mixer_ ? std::pair<int, int>{mixer_->row(), mixer_focus_}
                      : std::pair<int, int>{-1, -1};
    }
    // Where a channel's bar was last painted, for a click that means "put the
    // value where the pointer was".
    QRectF mixerBarRect(int channel_row) const {
        if (!mixer_ || channel_row < 0 ||
            channel_row >= static_cast<int>(mixer_hit_.bars.size()))
            return QRectF();
        return mixer_hit_.bars[static_cast<std::size_t>(channel_row)];
    }
    // Which digit of the mixer's row the next keystroke lands on — the one
    // thing about a row the keys can be on without the colour moving.
    int mixerDigit() const { return mixer_ ? mixer_->digit() : -1; }

    // Where the keys are: the row, and which of the panel's stops — 0 the list, 1
    // `save`, 2 `cancel`, 3 the name the colours go into.
    std::pair<int, int> pickerCursor() const {
        return picker_ ? std::pair<int, int>{picker_->row(), picker_focus_}
                       : std::pair<int, int>{-1, -1};
    }
    // Where a row of the colour list was painted, and where its swatch was, so a
    // check can ask whether the highlight stayed on the value and off the colour.
    QRectF pickerRowRect(int model_row) const {
        for (std::size_t i = 0; i < picker_hit_.row_index.size(); ++i) {
            if (picker_hit_.row_index[i] == model_row && i < picker_hit_.rows.size())
                return picker_hit_.rows[i];
        }
        return QRectF();
    }
    // The settings menu stepped aside for another panel. Its rows, its values, the row
    // the keys are on and everything unsaved in it stay exactly as they were; only the
    // painting and the keys hand over, and both come back. A menu that closed when you
    // opened the colour editor would throw away the font size you had just dialled in.
    // (Kaida, 2026-11-20: "the setting keeps your change until you save or cancel them,
    // even when in a sub-menu".)
    bool settingsSteppedAside() const { return settings_suspended_; }

    // What the menu's rows say, in the two vocabularies: the label a person reads and the
    // key the file takes, plus the value the row is holding — which after a sub-menu is
    // the point of asking, because it is what the menu kept for you.
    int settingsOpenRowCount() const {
        return settings_ ? static_cast<int>(settings_->rows().size()) : 0;
    }
    QString settingsRowLabel(int model_row) const {
        if (!settings_ || model_row < 0 || model_row >= settingsOpenRowCount())
            return QString();
        const SettingRow& row = settings_->rows()[static_cast<std::size_t>(model_row)];
        return QString::fromStdString(row.label.empty() ? row.key : row.label);
    }
    QString settingsRowKey(int model_row) const {
        if (!settings_ || model_row < 0 || model_row >= settingsOpenRowCount())
            return QString();
        return QString::fromStdString(settings_->rows()[static_cast<std::size_t>(model_row)].key);
    }
    // The answer a choice row is holding, as the person sees it.
    QString settingsRowChoice(int model_row) const {
        if (!settings_ || model_row < 0 || model_row >= settingsOpenRowCount())
            return QString();
        const SettingRow& row = settings_->rows()[static_cast<std::size_t>(model_row)];
        if (row.choices.empty() || row.value < 0 ||
            row.value >= static_cast<int>(row.choices.size()))
            return QString();
        return QString::fromStdString(row.choices[static_cast<std::size_t>(row.value)]);
    }
    int settingsRowValue(int model_row) const {
        if (!settings_ || model_row < 0 || model_row >= settingsOpenRowCount())
            return -1;
        return settings_->rows()[static_cast<std::size_t>(model_row)].value;
    }
    // Where the keys are: 0 a row, 1 `save`, 2 `cancel` — and which row when it is a row.
    int settingsFocus() const { return settings_focus_; }
    int settingsCursorRow() const { return settings_ ? settings_->row() : -1; }
    // Put the menu back after the panel it stepped aside for is gone. The same object,
    // the same row, the same unsaved values: coming back to a menu that had forgotten
    // what you were doing is the reason sub-menus get a bad name.
    void resumeSettings() {
        if (!settings_)
            return;
        settings_suspended_ = false;
        update();
    }

    void settingsRefreshRow(const std::string& key, const std::vector<std::string>& choices,
                            int value) {
        if (settings_)
            static_cast<void>(settings_->setRowAnswers(key, choices, value));
    }

    QRectF pickerSwatchRect(int model_row) const {
        for (std::size_t i = 0; i < picker_hit_.row_index.size(); ++i) {
            if (picker_hit_.row_index[i] == model_row && i < picker_hit_.swatches.size())
                return picker_hit_.swatches[i];
        }
        return QRectF();
    }

    // Where the settings menu put its rows and arrows, as they were last painted.
    // A test that wants to click a line asks here rather than redoing the layout
    // arithmetic, because the only honest answer is the geometry the pixels used —
    // a second calculation would be a second opinion, and the two would drift.
    QRectF settingsRowRect(int model_row) const {
        for (std::size_t i = 0; i < settings_hit_.row_index.size(); ++i) {
            if (settings_hit_.row_index[i] == model_row && i < settings_hit_.rows.size())
                return settings_hit_.rows[i];
        }
        return QRectF();
    }
    QPointF settingsStepPoint(int model_row, bool up) const {
        for (std::size_t i = 0; i < settings_hit_.row_index.size(); ++i) {
            if (settings_hit_.row_index[i] != model_row)
                continue;
            const std::vector<QRectF>& boxes = up ? settings_hit_.plus : settings_hit_.minus;
            if (i >= boxes.size())
                break;
            return boxes[i].center();
        }
        return QPointF();
    }
    QPointF settingsSavePoint() const { return settings_hit_.save.center(); }
    QPointF pickerSavePoint() const { return picker_hit_.save.center(); }
    // The whole word, not just somewhere inside it: how far the bottom of the list is
    // from the first thing below it is a question about edges.
    QRectF pickerSaveRect() const { return picker_hit_.save; }
    QPointF pickerCancelPoint() const { return picker_hit_.cancel.center(); }
    QPointF mixerSavePoint() const { return mixer_hit_.save.center(); }

    // Where a panel was last painted, so a check can ask whether it fits the
    // window it is painted in. The answer is the geometry the pixels used, which
    // is the only one worth asking for: a second layout worked out beside the
    // first is a second opinion, and the two drift.
    QRectF mixerPanelRect() const { return mixer_hit_.panel; }
    QRectF pickerPanelRect() const { return picker_hit_.panel; }
    QRectF settingsPanelRect() const { return settings_hit_.panel; }
    QRectF mixerPreviewRect() const { return mixer_hit_.preview; }
    // Where the colour list is scrolled to, and whether it said so. Recorded while
    // painting, as the hit rects are: the list's window is a consequence of the panel's
    // size, and the only honest place to learn it is where it was worked out.
    struct PickerScroll {
        int first = 0;
        int visible = 0;
        int total = 0;
        QRectF up;
        QRectF down;
    };
    int pickerFirstRow() const { return picker_scroll_.first; }
    int pickerVisibleRows() const { return picker_scroll_.visible; }
    bool pickerMoreAbove() const { return !picker_scroll_.up.isEmpty(); }
    bool pickerMoreBelow() const { return !picker_scroll_.down.isEmpty(); }
    QRectF pickerScrollMarkRect(bool upwards) const {
        return upwards ? picker_scroll_.up : picker_scroll_.down;
    }

    // What the strip says, and whether it all fit. The second one is the difference
    // between a short sentence and a long one being clipped at both ends by a centred
    // draw, which is how the pangram read.
    static QString mixerSampleText() { return kMixerSample; }
    bool mixerSampleFits() const { return mixer_sample_fits_; }
    // Which ground the sentence is sitting on, and where the word that changes it is.
    int mixerGround() const { return mixer_ground_; }
    QRectF mixerGroundSwitchRect() const { return mixer_hit_.ground_switch; }
    // What the mixer calls the colour it is editing, and the one thing each panel
    // has lit. A panel with two things lit has two answers to the same question, so
    // "where is the highlight" is worth being able to ask.
    QString mixerTitleText() const { return mixerTitle(); }
    QRectF mixerHighlightRect() const { return mixer_hit_.highlight; }
    QRectF pickerHighlightRect() const { return picker_hit_.highlight; }
    QRectF settingsHighlightRect() const { return settings_hit_.highlight; }
    // The picker's name field: what it holds, where it was painted, and what the panel
    // is asking when it is asking. A field a test cannot read is a field that can be
    // missing and still pass.
    QString pickerNameText() const { return picker_name_; }
    bool pickerEditing() const { return picker_editing_; }
    int pickerCaret() const { return picker_caret_; }
    QString pickerTargetNameText() const { return pickerTargetName(); }
    QString pickerTitleText() const { return pickerTitle(); }
    QRectF pickerNameRect() const { return picker_hit_.name; }
    bool pickerAskingOverwrite() const { return picker_overwrite_; }

    // The line of keys at the bottom of whichever panel was painted last: where it landed,
    // how many lines it came to, and the size it is set in beside the size the rows above
    // it are set in. That line used to be the widest and the largest thing in every panel,
    // which is how a menu about a font size came to be as wide as a sentence about arrow
    // keys. (Kaida, 2026-11-20: "the tooltips are the culprit, they take too much space and
    // make the windows larger, tooltips should have a reduced size".)
    QRectF panelHintRect() const { return hint_hit_; }
    int panelHintLines() const { return panel_hint_lines_; }
    int panelHintPixels() const { return panel_hint_px_; }
    int panelRowsPixels() const { return panel_rows_px_; }
    // Which of the panel's lines of keys it was that got painted. The colour list says
    // something different when the keys are standing on the theme name, and a check
    // cannot ask about words it cannot read back.
    QString panelHintText() const { return panel_hint_text_; }
    // A panel's title as it was painted, so the air under it is a distance a check
    // can measure rather than an impression a photograph has to argue about.
    QRectF panelTitleRect() const { return title_hit_; }
    // The two words as one box, so "clear of the hint" is a question about two rectangles
    // rather than about where a pixel happened to be painted.
    QRectF pickerWordsRect() const { return picker_hit_.save.united(picker_hit_.cancel); }
    QRectF settingsWordsRect() const { return settings_hit_.save.united(settings_hit_.cancel); }
    QRectF mixerWordsRect() const { return mixer_hit_.save.united(mixer_hit_.cancel); }
    // The picker's theme name as it was painted. It is the last line of the list, and the
    // two words are the panel's, and they read as one block of three when they are hung
    // together. (Kaida, 2026-11-20: "The theme name is too close to the save and cancel
    // buttons it should be a bit higher".)

    // The lowest row each panel painted. A panel that is too tall for its window
    // does not say so in its own rectangle — it says so by painting its last rows
    // underneath its two words, which is the mangling worth asking about.
    QRectF mixerBottomRowRect() const {
        return mixer_hit_.rows.empty() ? QRectF() : mixer_hit_.rows.back();
    }
    QRectF pickerBottomRowRect() const {
        return picker_hit_.rows.empty() ? QRectF() : picker_hit_.rows.back();
    }
    QRectF settingsBottomRowRect() const {
        return settings_hit_.rows.empty() ? QRectF() : settings_hit_.rows.back();
    }
    // The window's body, as the config or the settings menu asked for it. Padding
    // re-derives the grid because it changes how many cells the same window holds;
    // the scrollback depth and the cursor shape are the model's business and are
    // handed straight on to it.
    void setPadding(double pixels) {
        padding_ = std::clamp(pixels, 0.0, 64.0);
        applyGridGeometry();
        update();
    }
    double padding() const { return padding_; }
    // How thick the line between two places is drawn, in pixels. The gap itself belongs to
    // the arrangement — a divider costs a row or a column of cells whichever way it runs —
    // so this is only about what is painted inside it. Filling the whole gap used to mean a
    // vertical divider was a column wide and a horizontal one a row tall, so the two were
    // never the same thickness and neither was a line. The same number is used for both, and
    // 0 draws nothing.
    // (Kaida, 2026-11-24: "the separator should be thinner and equal in size when comparing
    // the horizontal and vertical one (maybe even an option in the settings to change it
    // size) and use the Theme Accent color".)
    void setDividerWidth(double pixels) {
        divider_width_ = std::clamp(pixels, 0.0, 16.0);
        update();
    }
    double dividerWidth() const { return divider_width_; }
    void setHistoryLimit(int lines) {
        if (lines < 0)
            lines = 0;
        history_limit_for_new_screen_ = static_cast<std::size_t>(lines);
        screen_.setHistoryLimit(history_limit_for_new_screen_);
        update();
    }
    int historyLimit() const { return static_cast<int>(screen_.historyLimit()); }
    static Screen::CursorShape shapeFor(CursorStyle style) {
        return style == CursorStyle::Block    ? Screen::CursorShape::Block
               : style == CursorStyle::Bar    ? Screen::CursorShape::Bar
                                              : Screen::CursorShape::Underline;
    }
    void setCursorStyle(CursorStyle style) {
        default_cursor_shape_for_new_screen_ = shapeFor(style);
        screen_.setDefaultCursorShape(shapeFor(style));
        update();
    }
    // What a window does about the blink when nobody has said: it blinks, like the
    // terminals everybody arrives from. `cursor_blink = 0` in the file is the word for
    // steady, and it is a real answer rather than an off switch.
    // (Kaida, 2026-11-21: asked whether the default should blink, and said yes.)
    static constexpr int kDefaultCursorBlinkMs = 500;

    // The cursor's blink: how long each half lasts in milliseconds, 0 meaning it stays
    // lit. Nothing animated before this — the DECSCUSR blinking shapes chose a shape and
    // left the timing alone — so the rate belongs to the window, in the file, where a
    // person can say how much movement they want behind the text.
    // (Kaida, 2026-11-21: "can we add cursor blink, with customizable blink speed
    // ( 0 being no blinking)".)
    void setCursorBlink(int ms) {
        const int was = cursor_blink_ms_;
        cursor_blink_ms_ = std::max(0, ms);
        if (cursor_blink_ms_ == 0) {
            cursor_blink_timer_.stop();
            cursor_lit_ = true; // steady means lit, not steady wherever it happened to stop
        } else if (cursor_blink_timer_.isActive()) {
            cursor_blink_timer_.setInterval(cursor_blink_ms_);
        } else {
            cursor_lit_ = true;
            cursor_blink_timer_.start(cursor_blink_ms_);
        }
        if (was != cursor_blink_ms_)
            repaintCursorCell();
    }
    int cursorBlink() const { return cursor_blink_ms_; }
    bool cursorBlinkRunning() const { return cursor_blink_timer_.isActive(); }
    // The phase itself, whether or not the window is showing it: a test wants to know
    // the timer is turning, and a window in the background hides the blink without
    // stopping it.
    bool cursorBlinkPhase() const { return cursor_lit_; }
    bool cursorLit() const {
        if (cursor_blink_ms_ <= 0 || !isActiveWindow())
            return true;
        return cursor_lit_;
    }
    // The one cell a blink changes. Repainting the window every half second to turn a
    // cursor off and on again is a fan speeding up for nothing.
    QRect cursorBlinkRect() const {
        if (scroll_offset_ != 0 || !screen_.cursorVisible())
            return QRect();
        const int cx = std::clamp(screen_.cursorX(), 0, cols_ - 1);
        const int cy = std::clamp(screen_.cursorY(), 0, rows_ - 1);
        const int span = std::max(1, screen_.at(cy, cx).width);
        const QPointF top = cellTopLeft(cy, cx);
        const QSizeF cell = cellSize();
        return QRect(QPoint(int(top.x()) - 1, int(top.y()) - 1),
                     QSize(int(cell.width() * span) + 2, int(cell.height()) + 2));
    }
    void repaintCursorCell() {
        // Split, the cursor lives in whichever half is being typed into and may be a
        // neighbour's, so the one-cell rectangle the single screen uses is the wrong
        // box to dirty; the whole window is cheap to repaint and always right.
        if (splitActive()) {
            update();
            return;
        }
        const QRect cell = cursorBlinkRect();
        if (!cell.isNull())
            update(cell);
    }

    // The cursor's shape, held for the config: named there, it outranks every DECSCUSR
    // the session writes — including the one a snapshot replays when a window attaches
    // to a running session. Nothing named, nothing locked, and a program is free to pick
    // its own shape.
    // (Kaida, 2026-11-21: "the Windows hold the cursor style".)
    void lockCursorStyle(std::optional<CursorStyle> style) {
        cursor_style_lock_ = style; // kept for the next screen, which is coming
        screen_.lockCursorShape(style ? std::optional<Screen::CursorShape>(shapeFor(*style))
                                      : std::nullopt);
        update();
    }
    CursorStyle cursorStyle() const {
        switch (screen_.defaultCursorShape()) {
            case Screen::CursorShape::Block: return CursorStyle::Block;
            case Screen::CursorShape::Bar: return CursorStyle::Bar;
            case Screen::CursorShape::Underline: return CursorStyle::Underline;
        }
        return CursorStyle::Underline;
    }

    // The thread that turns bytes from the far end into events for this window. Its own
    // method because a window can be shown a different shell, and the new one needs a
    // reader of its own. The generation number and the backend are both taken here, at
    // the start of a shell's life, so everything this reader reports afterwards belongs
    // to that shell — including anything it is holding when the window moves on.
    void startReader() {
        const int generation = generation_;
        TerminalBackend* const backend = backend_.get();
        running_ = true;
        shell_alive_ = true;
        reader_ = std::thread([this, backend, generation] {
            char buf[4096];
            while (running_ && shell_alive_) {
                // The tab bar is not output, so it does not arrive through read(): a
                // session gaining a pane prints nothing at all on the pane being looked
                // at. Asking on every turn of the loop is what notices, and the wait below
                // already wakes five times a second by itself.
                std::string refusal;
                if (backend->takeRefusal(refusal)) {
                    const std::string said = refusal;
                    QMetaObject::invokeMethod(this, [this, said] { showNotice(said); },
                                              Qt::QueuedConnection);
                }
                TerminalBackend::TabBar bar;
                if (backend->takeTabBar(bar)) {
                    QCoreApplication::postEvent(this,
                                                new TerminalTabsEvent(bar.panes, bar.focused,
                                                                      bar.tabs, bar.cells,
                                                                      bar.numbers, bar.mine,
                                                                      generation));
                }
                // Bytes the backend is already holding are not waiting on the
                // descriptor to become readable, so they are taken without asking.
                // A session restored into a window arrives in the handshake that
                // opens the connection and then sits in the backend's own buffer:
                // waiting on the socket anyway left that window blank until something
                // made the shell talk, and the whole screen appeared on the first
                // keypress. It looked like a painting problem and was a reading one.
                if (!backend->hasBuffered()) {
                    // Wait on the descriptor rather than blocking in read(): a stray
                    // job that inherited the terminal can keep the stream open after
                    // the shell itself is gone, and the window has to close on the
                    // shell leaving, not on whoever else lets go.
                    struct pollfd watched {backend->descriptor(), POLLIN, 0};
                    const int ready = ::poll(&watched, 1, 200);
                    if (ready < 0 && errno == EINTR)
                        continue;
                    if (ready == 0)
                        continue;
                }
                if (backend->collectsChild() && backend->collect()) {
                    // The shell is gone. Anything it printed on the way out has
                    // already been read and queued as an event, so there is
                    // nothing left to wait for.
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent(generation));
                    break;
                }
                ssize_t n = backend->read(buf, sizeof(buf));
                if (n == TerminalBackend::kNothingRightNow)
                    continue; // quiet rather than gone: the turn round asks about the bar again
                if (n <= 0) {
                    // The shell closed the terminal. Collect it here so the exit
                    // status means something; the master stays open until
                    // shutdown has joined this thread. (A successful write is no
                    // sign of life — see Pty::write.)
                    if (backend->collectsChild())
                        backend->collect();
                    shell_alive_ = false;
                    QCoreApplication::postEvent(this, new TerminalExitEvent(generation));
                    break;
                }
                // Take the whole run the backend is already holding, not just one
                // bufferful. A restored screen arrives as a single snapshot that the
                // backend holds end to end, and that snapshot has to reach the model in
                // one piece: it begins by leaving whatever alternate screen the view was
                // on and re-enters it at the end, so a window fed only the first half
                // sees, between the halves, a screen that has dropped to the primary
                // buffer. A full-screen application wants the whole window, so the
                // geometry follows that transient down — the window resizes, the session
                // resizes to match, the session answers with another snapshot, and the
                // two chase each other through an endless resize loop that reflows the
                // entire scrollback on every turn. That is why a full-screen program
                // hung once there was anything to scroll back to and ran fine when there
                // was not: a small snapshot lands in one read, a large one does not.
                // Feeding the whole buffered run at once hides the transient, and the
                // loop never starts. A local shell holds nothing between reads, so it is
                // unaffected.
                std::string run(buf, static_cast<size_t>(n));
                while (backend->hasBuffered()) {
                    const ssize_t more = backend->read(buf, sizeof(buf));
                    if (more <= 0)
                        break;
                    run.append(buf, static_cast<size_t>(more));
                }
                QCoreApplication::postEvent(this,
                                            new TerminalOutputEvent(std::move(run), generation));
            }
        });
    }

    // The neighbour's reader. It is the window's own reader with the parts a second
    // pair of eyes does not need taken out: it does not ask about the tab bar or the
    // refusal, because those belong to the connection the window was built around,
    // and it does not collect a child, because a session's program is the daemon's
    // to reap, not this window's. What it must keep is the two things that make the
    // first reader safe — minding kNothingRightNow, so a view that is merely quiet is
    // not mistaken for a view that is gone, and taking the whole buffered run at
    // once, so a place's first screen reaches its model in one piece.
    void startNeighbourReader(ExtraPane* view) {
        if (!view)
            return;
        TerminalBackend* const backend = view->backend.get();
        const int generation = view->generation;
        view->running = true;
        view->reader = std::thread([this, view, backend, generation] {
            char buf[4096];
            while (view->running && view->alive) {
                // A backend answers read() with kNothingRightNow while it is holding a
                // tab bar or a refusal the window has not taken, and it does not pop
                // that frame until it is taken. The window's own reader asks for the
                // tab bar on every turn; a neighbour has no tab bar of its own, but it
                // still has to take the one the session broadcasts to it, or the flag
                // stays set, read() keeps saying "nothing right now", and the real
                // output sits unread behind it forever. Taking and discarding is what
                // keeps a quiet-but-alive view from being mistaken for a stalled one.
                TerminalBackend::TabBar bar;
                static_cast<void>(backend->takeTabBar(bar));
                std::string refusal;
                static_cast<void>(backend->takeRefusal(refusal));
                if (!backend->hasBuffered()) {
                    struct pollfd watched {backend->descriptor(), POLLIN, 0};
                    const int ready = ::poll(&watched, 1, 200);
                    if (ready < 0 && errno == EINTR)
                        continue;
                    if (ready == 0)
                        continue;
                }
                ssize_t n = backend->read(buf, sizeof(buf));
                if (n == TerminalBackend::kNothingRightNow)
                    continue; // quiet, not gone
                if (n <= 0) {
                    view->alive = false;
                    break;
                }
                std::string run(buf, static_cast<size_t>(n));
                while (backend->hasBuffered()) {
                    const ssize_t more = backend->read(buf, sizeof(buf));
                    if (more <= 0)
                        break;
                    run.append(buf, static_cast<size_t>(more));
                }
                QCoreApplication::postEvent(this, new ExtraOutputEvent(std::move(run), generation));
            }
        });
    }

    // Let go of one neighbour. The reader is joined before its connection is
    // destroyed, exactly as for the window's own reader; the socket is hung up first
    // so a reader waiting on output wakes and sees the end. Detaching is not closing:
    // the program on the far end belongs to the session, and this window is only
    // stopping looking at it.
    void stopNeighbourReader(std::unique_ptr<ExtraPane> view) {
        if (!view)
            return;
        view->running = false;
        if (view->backend)
            view->backend->stopProcess(false); // also wakes the read the reader is in
        if (view->reader.joinable())
            view->reader.join();
        if (view->backend)
            view->backend->release();
        ++extra_generation_; // anything still in flight about that place is dropped
    }

    // Let go of every neighbour — leaving side-by-side mode, closing the last extra
    // place, moving to another session, or this window itself going away.
    void stopAllNeighbours() {
        for (auto& view : neighbours_)
            stopNeighbourReader(std::move(view));
        neighbours_.clear();
    }

    // Say that this window is being closed and what it is showing was not kept, so
    // that the teardown in the destructor ends the session rather than only leaving it.
    // Nothing else calls this. A window merely going away is not somebody throwing
    // work away, and a terminal that assumed it was would be losing builds.
    void discardOnClose() { closing_on_close_ = true; }

    // What is running out front in this window, when it is not just the shell. Empty
    // for a session owned by the daemon, which is the one place that can answer that
    // about a shell it owns; whoever is asking asks the daemon instead.
    std::string foregroundProgram() const { return backend_->foregroundJobName(); }

    void shutdown() {
        running_ = false;
        stopAllNeighbours(); // neighbours go first, and their readers are joined like the rest
        // The child goes first, and the reader is joined before the master is
        // closed. Closing a descriptor under a thread blocked reading it leaves
        // that number free to be handed to somebody else's open() while the read
        // is still in flight, so teardown waits for the reader to notice the end
        // of the stream on its own.
        backend_->stopProcess(closing_on_close_);
        if (reader_.joinable())
            reader_.join();
        backend_->release();
        shell_alive_ = false;
    }

    ~TerminalWidget() override {
        shutdown();
    }

    // Point this window at a different shell, and stop showing the one it had.
    //
    // The order is the whole of this. The reader has to be joined before the backend it
    // reads from is destroyed; the shell on the far end has to be told whether this
    // window is closing or merely looking elsewhere, because that is what decides
    // whether an unsaved session outlives the window; and the screen has to be emptied,
    // because a window still showing the previous shell would be showing a session that
    // is not there. Anything the old shell sends after this is tagged with a generation
    // this window has stopped listening for.
    void showSession(std::unique_ptr<TerminalBackend> next, bool discard_current) {
        if (!next)
            return;
        stopAllNeighbours(); // a neighbour is a view of the old session, not the new one
        running_ = false;
        backend_->stopProcess(discard_current); // also wakes the read the reader is in
        if (reader_.joinable())
            reader_.join();
        backend_->release();

        backend_ = std::move(next);
        ++generation_;
        // A brand-new screen, for a session that may be brand-new or may be one this
        // window is going back to. It starts with the built-in colours and no locks,
        // which is the wrong starting point twice over: the theme would have to be
        // re-read from the file to survive a session switch, and until it was, the
        // new shell's own OSC 10/11 could paint over it. So the window dresses the
        // screen it is about to show — its theme first, then the colours that outrank
        // whatever the session says. (Kaida, 2026-11-20: "a session spawn still gets
        // rid of our theming". It did, because this line replaced the screen and
        // nobody re-applied anything afterwards.)
        screen_ = Screen(rows_, cols_);
        screen_.setDefaults(theme_.foreground, theme_.background);
        screen_.setThemePalette(theme_.palette);
        screen_.setHistoryLimit(history_limit_for_new_screen_);
        if (colour_locks_.set) {
            screen_.lockDefaults(colour_locks_.foreground, colour_locks_.background);
            screen_.lockPalette(colour_locks_.palette);
        }
        screen_.setDefaultCursorShape(default_cursor_shape_for_new_screen_);
        // … and the shape that outranks whatever the session asks for. That lock lives on
        // the screen this line has just replaced, so it is put back here beside the colour
        // locks, or a fresh start wears the shape from the file and every session after it
        // hands the shape straight back to the shell. (Kaida, 2026-11-21: "still loosing
        // cursor style on a new session / when attaching a session. Fresh start still hold
        // the right one from config".)
        screen_.lockCursorShape(cursor_style_lock_
                                    ? std::optional<Screen::CursorShape>(
                                          shapeFor(*cursor_style_lock_))
                                    : std::nullopt);
        scroll_offset_ = 0;
        clearSelection();
        status_.clear();
        // A dialog belongs to the shell it was asked about. The shell it was
        // asked about is gone; the answer is not owed to whoever arrives next.
        overlay_ = Overlay{};
        // The new shell is asked to fit this window rather than the size it was started
        // at: a session restored into a small window should be a small session.
        backend_->resize(rows_, cols_);
        feedWhatIsHeld();
        startReader();
        update();
        // Asked for again and again for a moment, because the one update() above is worth
        // nothing if it lands before the window has been handed a surface: a compositor
        // that is still decorating a fresh window swallows the repaint, and the screen the
        // session sent a moment later has nothing to be drawn into. The person sees a blank
        // window that fills in the instant a key is pressed — the keystroke is not what
        // fetches the content, it is only the next thing that asks for a paint. This asks
        // on its own until one has actually happened.
        if (repaint_timer_ == 0)
            repaint_timer_ = startTimer(kRepaintPollMs);
    }

    // The colours this window has told its screen are not the session's to move.
    struct ColourLocks {
        std::optional<Color> foreground;
        std::optional<Color> background;
        std::array<std::optional<Color>, 16> palette{};
        bool set = false;
    };

    // What the window's own keybinds mean, set by whoever owns the window. The widget
    // does not know what a session is, does not draw a menu and has never heard of the
    // daemon; it knows Ctrl+K then S was pressed and that somebody asked to be told.
    std::function<void()> on_save_session;
    std::function<void()> on_session_menu;
    // Ctrl+K then K: the settings menu, and through its `theme` row the colour editor.
    std::function<void()> on_settings_menu;
    // Ctrl+K then R: enter resize mode, where the arrows stretch the place in hand.
    std::function<void()> on_resize_mode;
    // Ctrl+K then F: a file manager in a new place. Not wired to a real spawn yet; the
    // key is reserved now so the surface is whole and the tty has no surprise later.
    std::function<void()> on_file_manager;

    // The tmux-style control: Ctrl+K arms a one-key command and the next key spends it.
    // Captured always, so the shell never sees the Ctrl+K that kills its line — the cost
    // of a single prefix that the future tty will need. `prefixArmed` lets a check ask.
    bool prefixArmed() const { return prefix_armed_; }
    // Whether the row of possible keys shows while the control waits. Off, arming still
    // works, it just says nothing; a person who has learned the keys need no reminder.
    void setShowKeyHints(bool on) { show_key_hints_ = on; update(); }
    bool showKeyHints() const { return show_key_hints_; }
    // Resize mode: the arrows stretch the place in hand. Enter keeps, Esc puts it back.
    bool inResizeMode() const { return resize_mode_; }
    void enterResizeMode() {
        if (resize_mode_)
            return;
        resize_mode_ = true;
        if (backend_)
            backend_->resizeBegin();
        update();
    }
    void endResizeMode(bool commit) {
        if (!resize_mode_)
            return;
        resize_mode_ = false;
        if (backend_)
            backend_->resizeEnd(commit);
        update();
    }

    // One line the window says for itself, shown in the corner for a moment and then
    // gone. A keybind that keeps your work and says nothing is a keybind that gets
    // pressed twice, and then stopped being trusted.
    void setStatusText(const std::string& text, int show_ms = 4000) {
        status_ = text;
        if (!status_timer_) {
            status_timer_ = new QTimer(this);
            status_timer_->setSingleShot(true);
            connect(status_timer_, &QTimer::timeout, this, [this] {
                status_.clear();
                update();
            });
        }
        status_timer_->start(show_ms);
        update();
    }
    const std::string& statusText() const { return status_; }

    // -----------------------------------------------------------------------
    // The terminal's own dialogs: the question asked when a window is closed
    // with something running in it, and the list of sessions you could go
    // back to. Both are drawn by this widget in the terminal's own font and
    // colours, and both take the keyboard while they are up, so they read as
    // part of the terminal rather than as a window from somewhere else that
    // happened to land on top of it. A dialog that arrives in its own native
    // window brings its own theme, font and button order with it, and the
    // first thing it does is remind you that the terminal is a rectangle the
    // desktop happens to be drawing.
    //
    // The answer comes back as an index into the choices, or -1 when the
    // question went away without one being given — Esc, or a click outside
    // the panel, both of which mean "not now" and nothing more.
    // -----------------------------------------------------------------------

    // A question, some detail under it (which may be empty — one clean line is
    // often the whole question), and a row of buttons. The arrow keys or Tab
    // walk the buttons; the first letter of an answer picks it from wherever
    // the focus is, because a person who can read "Yes" should not have to
    // walk to it. `focus` is the button that starts out highlighted, and it
    // is worth choosing with care: Enter fires it, so the safe answer is the
    // one that should sit under the finger.
    void askInTerminal(const std::string& question, const std::string& detail,
                       const std::vector<std::string>& choices, int focus,
                       std::function<void(int)> answer, const std::vector<char>& hotkeys = {}) {
        overlay_ = Overlay{};
        overlay_.active = true;
        overlay_.lines.push_back(question);
        if (!detail.empty())
            overlay_.lines.push_back(detail);
        overlay_.choices = choices;
        overlay_.hotkeys = hotkeys;
        if (focus >= 0 && focus < static_cast<int>(choices.size()))
            overlay_.selected = focus;
        overlay_.answer = std::move(answer);
        update();
    }

    // A title and a list of entries walked with Up/Down (or j/k), jumped to
    // with the number keys, chosen with Enter and put away with Esc — the
    // way every TUI picker works, because a menu you can only point at is a
    // menu that stops working the moment the pointer is elsewhere.
    void chooseInTerminal(const std::string& title, const std::vector<std::string>& entries,
                          std::function<void(int)> answer) {
        overlay_ = Overlay{};
        overlay_.active = true;
        overlay_.vertical = true;
        overlay_.lines.push_back(title);
        overlay_.choices = entries;
        overlay_.hint = "up/down choose · enter go · esc close · 1-9 jump";
        overlay_.answer = std::move(answer);
        update();
    }

    bool overlayActive() const { return overlay_.active; }
    // A panel that asks for a name and nothing else: a line of text you type, Enter to
    // hand it over, Esc to back out. `starting` is what the window would have chosen on its
    // own, offered so pressing Enter without typing takes the automatic name rather than an
    // empty one. Enter on an empty field means the same thing for the same reason.
    // (Kaida, 2026-11-24: "saving a session should now prompt for a name".)
    void askNameInTerminal(const std::string& question, const std::string& detail,
                           const std::string& starting, std::function<void(std::string)> gave) {
        overlay_ = Overlay{};
        overlay_.active = true;
        overlay_.editable = true;
        overlay_.field = starting;
        overlay_.field_offered = !starting.empty();
        overlay_.lines.push_back(question);
        if (!detail.empty())
            overlay_.lines.push_back(detail);
        overlay_.hint = "type a name · enter keep it · esc back out";
        overlay_.gave_text = std::move(gave);
        update();
    }
    // What is being asked and what it is being asked with — exposed so the
    // words and the order of the answers can be checked without an eye on a
    // screen, and so a screenshot tool can photograph a dialog on purpose.
    std::string overlayQuestion() const {
        return overlay_.lines.empty() ? std::string() : overlay_.lines.front();
    }
    const std::vector<std::string>& overlayChoices() const { return overlay_.choices; }
    int overlaySelected() const { return overlay_.selected; }
    // The field of a panel that asks for words rather than a button, exposed on the same
    // terms as the question: what has been typed, and whether there is a field at all.
    bool overlayEditable() const { return overlay_.editable; }
    std::string overlayField() const { return overlay_.field; }

    // Read-only view of the terminal model, for tests and for UI pieces such as
    // a scrollbar that must agree with the painted content.
    const Screen& screen() const { return screen_; }
    int rows() const { return rows_; }
    int cols() const { return cols_; }

    // Lines scrolled back from the live bottom; zero means following the shell.
    int scrollOffset() const { return scroll_offset_; }

    // The text of the row shown at display row `r` of the window's own view, honouring
    // the scroll offset — for a check that wants to say "scrolled up, this line is on
    // screen" rather than merely "the offset grew".
    std::string displayRowText(int r) const { return rowText(displayRow(r)); }
    // The same for a neighbour's own place, honouring that place's own scroll.
    std::string neighbourRowText(int pane, int r) const {
        const ExtraPane* view = neighbourFor(pane);
        if (!view)
            return {};
        return rowText(rowToShow(view->screen, view->scroll, r));
    }
    static std::string rowText(const Row& line) {
        std::string out;
        for (const Cell& c : line)
            out += c.text;
        return out;
    }

    // Where the scroll indicator sits right now, in this widget's pixels. Aimed
    // at by the mouse handlers and by anything that needs to click on it.
    ScrollBar scrollIndicator() const {
        return scrollBarGeometry(QSize(width(), height()), scrollRange(), rows_,
                                 scroll_offset_);
    }
    // How far the view could scroll back right now, which is the only number the view
    // should ever clamp itself against. An application that asked for the whole screen
    // (vim, less, btop, tmux) is drawing into a grid with no history above it, and the
    // history it cannot see belongs to the shell it is covering: offering it as
    // scrollback would show a full-screen program's viewer a screenful of somebody
    // else's text, and offer a scroll bar for a buffer that is not the one on screen.
    // So the answer is zero, and everything here — the indicator, the wheel,
    // Shift+PageUp, dragging the thumb, dragging to an edge — asks this rather than the
    // model, which is why it is one function and not a check in each place.
    int scrollRange() const { return screen_.inAlternateScreen() ? 0 : screen_.historySize(); }

    // Write what this widget currently believes, to the file KAI_TERM_STATE_FILE names.
    // Every number the screen's position depends on goes in — the window's pixels, the
    // cell's pixels, the grid's rows and columns, the padding the grid is inset by, the
    // top-left corner of the first row and of the last one, the cursor, the scroll
    // region, whose screen is on, the history and how far back the view is — followed by
    // the rows that carry text. That last part is the point: a picture shows where the
    // text looks like it is, and this shows where the terminal thinks it is, and the gap
    // between the two is the bug.
    void writeStateDiary(const char* why) const {
        const char* path = std::getenv("KAI_TERM_STATE_FILE");
        if (!path || !*path)
            return;
        std::ofstream out(path, std::ios::app);
        if (!out)
            return;
        const QPointF first = cellTopLeft(0, 0);
        const QPointF last = cellTopLeft(std::max(0, rows_ - 1), 0);
        out << why
            << " | window " << width() << "x" << height()
            << " | cell " << cellSize().width() << "x" << cellSize().height()
            << " | grid " << rows_ << "x" << cols_
            << " | model " << screen_.rows() << "x" << screen_.cols()
            << " | pad " << activePadding()
            << " | y0 " << first.y() << " ylast " << last.y()
            << " | cursor " << screen_.cursorY() << "," << screen_.cursorX()
            << " | region " << screen_.scrollRegionTop() << ".." << screen_.scrollRegionBottom()
            << " | origin " << (screen_.originMode() ? 1 : 0)
            << " | alt " << (screen_.inAlternateScreen() ? 1 : 0)
            << " | history " << screen_.historySize()
            << " | offset " << scroll_offset_ << "/" << scrollRange()
            << "\n";
        for (int r = 0; r < rows_ && r < screen_.rows(); ++r) {
            std::string text;
            for (int c = 0; c < cols_ && c < screen_.cols(); ++c) {
                const std::string& cell = screen_.at(r, c).text;
                text += cell.empty() ? " " : cell;
            }
            while (!text.empty() && text.back() == ' ')
                text.pop_back();
            if (!text.empty())
                out << "   r" << r << "|" << text.substr(0, 100) << "\n";
        }
        out.flush();
    }

    // Say where the panes are, so a drag stays inside the one it started in. An empty
    // list is the ordinary terminal: one pane, the whole screen. A host drawing its own
    // splits calls this with the cells each one owns; nothing else has to know it
    // exists.
    void setPanes(std::vector<PaneRect> panes) {
        panes_ = std::move(panes);
        // A layout change moves the walls around any selection already out there.
        // Pulling the ends in to the pane they started from beats leaving a highlight
        // that now runs through a neighbour, and the person can drag again.
        if (!selection_.empty()) {
            const PaneRect pane = paneForDrag(panes_, dragging_ ? anchor_ : selection_.anchor,
                                              rows_, cols_);
            selection_.anchor.row = pane.clampRow(selection_.anchor.row);
            selection_.head.row = pane.clampRow(selection_.head.row);
            selection_.anchor.col = pane.clampEndCol(selection_.anchor.col);
            selection_.head.col = pane.clampEndCol(selection_.head.col);
        }
        update();
    }

    const std::vector<PaneRect>& panes() const { return panes_; }

    // The current text selection, in display coordinates, and the text it covers.
    Selection selection() const { return selection_; }
    std::string selectedText() const {
        const PaneRect pane = paneForDrag(panes_, selection_.anchor, rows_, cols_);
        if (!splitActive() || (pane.firstRow() == 0 && pane.firstCol() == 0)) {
            return selectionTextInPane(pane, selection_,
                                       [&](int r) -> const Row& { return focusedRow(r); });
        }
        // Side by side, each place owns a corner of the grid and a screen of its own
        // width, so the drag is read in that place's coordinates. Reading its rows at the
        // grid's columns would run past the end of every line and bring back nothing but
        // newlines. A host that has only described rects over one screen keeps the
        // straightforward reading above, where the grid and the screen are the same thing.
        const Selection local{CellPos{selection_.anchor.row - pane.firstRow(),
                                      selection_.anchor.col - pane.firstCol()},
                              CellPos{selection_.head.row - pane.firstRow(),
                                      selection_.head.col - pane.firstCol()}};
        return selectionTextInPane(PaneRect{0, 0, pane.rows, pane.cols}, local,
                                   [&](int r) -> const Row& {
                                       return focusedRow(r + pane.firstRow());
                                   });
    }

    // Whether a cell is highlighted. The pane a selection started in clips it, because
    // the rows in the middle of a drag run to the end of the line, and past the divider
    // that line belongs to another shell.
    bool selectedAt(int row, int col) const {
        if (!selection_.contains(row, col))
            return false;
        return paneForDrag(panes_, selection_.anchor, rows_, cols_).contains(row, col);
    }

    // Geometry for anything that has to aim at a character: the size of one cell
    // and the pixel position of a cell's top-left corner. Tests use it to send
    // a mouse event at a specific piece of text; a scrollbar or tooltip would
    // want the same numbers.
    QSizeF cellSize() const { return QSizeF(cellWidth(), cellHeight()); }

    // How long a synchronized output block (?2026) may hold a repaint back. Long
    // enough that a redraw of a whole screen fits inside one block, short enough
    // that nothing on screen ever looks stuck.
    static constexpr qint64 kSyncRepaintIntervalMs = 100;

    // Whether this much time since the last repaint is enough to paint again. A
    // pure decision, public so the pacing can be asserted without racing a clock.
    static bool repaintAllowed(bool synchronized_output, qint64 ms_since_last_repaint) {
        return !synchronized_output || ms_since_last_repaint >= kSyncRepaintIntervalMs;
    }
    QPointF cellTopLeft(int row, int col) const {
        const double pad = activePadding();
        return QPointF(pad + col * cellWidth(), gridTop() + row * cellHeight());
    }

    // The strip along the top that a tab bar is drawn in, in pixels. Nothing while this
    // window has one place to look at — a bar with a single tab in it is a border that
    // lies about having tabs — and one row's height from two places onwards.
    double tabStrip() const { return tab_panes_ > 1 ? cellHeight() : 0.0; }

    // Where the first row of the grid is drawn, below the bar when there is one. The
    // height the grid is counted at comes from here as well, so the program inside is
    // told about the room the bar took rather than promised a row it will never see.
    double gridTop() const { return activePadding() + tabStrip(); }

    // What the session's tab bar holds. The count is the daemon's business; which tab is
    // lit is this window's, because two windows on one session can be looking at two
    // different places and both are right.
    void setTabBar(int panes, int focused, bool tabs, std::vector<PaneRect> cells = {},
                   std::vector<int> numbers = {}, int mine = -1) {
        if (panes == tab_panes_ && view_tabs_ == tabs && cells == tab_cells_ &&
            numbers == tab_numbers_ && (tab_chosen_ || focused == tab_focused_) &&
            (mine < 0 || mine == tab_focused_))
            return;
        const int places_before = tab_panes_;
        tab_panes_ = std::max(0, panes);
        view_tabs_ = tabs;
        tab_cells_ = std::move(cells);
        tab_numbers_ = std::move(numbers);
        if (static_cast<int>(tab_numbers_.size()) > tab_panes_)
            tab_numbers_.resize(static_cast<size_t>(tab_panes_));
        // The bar's cells are where each place sits, so they are also the answer to
        // "which place does this drag belong to". Kept as one copy on purpose: a drag
        // clipped by a different set of rects than the ones painted highlights the cells
        // of one place and copies the text of its neighbour. Only while places really are
        // side by side — with one place to look at the grid is the place, and clipping a
        // drag to a rect left over from a closed split would cut a selection short along
        // a divider that is no longer drawn.
        panes_ = (tab_panes_ > 1 && !view_tabs_) ? tab_cells_ : std::vector<PaneRect>{};
        if (!tab_chosen_)
            tab_focused_ = std::clamp(focused, 0, std::max(0, tab_panes_ - 1));
        else if (tab_focused_ >= tab_panes_)
            tab_focused_ = std::max(0, tab_panes_ - 1);
        // Before anything is drawn or measured, settle which place this window is looking
        // at: the size it asks the program to be, and the screen it paints, both come from
        // that answer.
        const int asked_for_a_place = pending_new_place_;
        followThePlaceJustMade();
        // The session also says which place *this* window is being shown, which is not the
        // session's focus: a place closing moves the view that was on it somewhere else
        // without anyone asking, and until now nothing said so over here. A window left
        // believing it is on the place it was on goes on typing into whichever program the
        // renumbering left under that number, which is how text comes to appear in a
        // neighbour while the cursor stays here. Adopt it quietly — the session has already
        // moved this view, so telling it back would be repeating a rumour about its own
        // doing. A place this window asked for itself outranks the report, since that bar
        // may have been on its way from before the new place existed.
        if (mine >= 0 && mine < tab_panes_ && mine != tab_focused_ && asked_for_a_place < 0) {
            pane_scroll_[tab_focused_] = scroll_offset_;
            tab_focused_ = mine;
            scroll_offset_ = 0;
            const auto saved = pane_scroll_.find(mine);
            pending_view_scroll_ = saved == pane_scroll_.end() ? 0 : saved->second;
            tab_chosen_ = true;
            resizeViewToFocusedCell(); // the place in hand is a different cell now
        }
        // A neighbour stands in for a PLACE rather than for a program: it is painted into
        // that place's rect, so what it should be showing is whatever program answers to
        // that number now. Adding or taking away a place renumbers the places after the
        // change, and a view opened for an old number would go on feeding the program it
        // was opened for into a rect that belongs to somebody else — which is how one
        // program came to be drawn twice while the place this window left behind was
        // painted blank. Only the count moves them: dragging a divider changes sizes, not
        // numbers, and re-opening every view on each frame of a drag would blank the
        // window continuously.
        if (places_before != tab_panes_)
            stopAllNeighbours();
        // The neighbours are views of particular places, so they move with the cells
        // of those places; and if a place is gone, or the session has gone back to
        // tabs, there is nothing beside the window to show any more.
        syncNeighbours();
        applyGridGeometry(); // the bar takes a row's height the moment it appears
        update();
    }

    // Look at another place in the session. The daemon sends the screen that belongs there
    // and starts carrying this window's typing to the same program; nothing here needs to
    // know what the other places contain.
    void showPaneAt(int pane) {
        if (pane < 0 || pane >= tab_panes_ || pane == tab_focused_)
            return;
        // The place just left keeps the scroll it was left at, so coming back finds it
        // where it was. The place being moved to starts at its live bottom while its fresh
        // snapshot fills the screen; the scroll saved for it is held until that history has
        // arrived (below, in the output path), because applying it to an empty screen would
        // only be clamped back to the bottom and lost.
        pending_new_place_ = -1; // a place chosen by hand outranks one merely asked for
        pane_scroll_[tab_focused_] = scroll_offset_;
        tab_focused_ = pane;
        scroll_offset_ = 0;
        const auto saved = pane_scroll_.find(pane);
        pending_view_scroll_ = saved == pane_scroll_.end() ? 0 : saved->second;
        tab_chosen_ = true;
        resizeViewToFocusedCell(); // the view now shows a different place's cell
        update();
        if (backend_)
            backend_->showPane(pane);
        // The window has moved onto a new place: that place is now drawn by the
        // window's own screen, so its neighbour (if it had one) goes away, and the
        // place this window used to look at now needs a neighbour of its own. The
        // daemon does not say so on the tab bar for a view that only moved itself, so
        // this is where the neighbours learn to get out of the way and step in.
        syncNeighbours();
    }

    // Adopt the place this window asked the session to make, once the bar says it is
    // there. The session puts a new place directly after the one it was divided from —
    // the new node holds the old place first and the new one second — and it moves the
    // view that asked onto the place it made, so that asking for a place lands you in it
    // instead of leaving you to press a second key. That move is invisible on the wire:
    // the window is simply sent the new place's screen. Saying here what the window asked
    // for is therefore the only way this side can keep its own screen, the place it
    // believes it is looking at, and the size it asks of that place's program telling the
    // same story — without it the new place's blank prompt is painted into the rect of the
    // place that was divided, at that place's old size, and the work that was there
    // disappears until something redraws it.
    void followThePlaceJustMade() {
        if (pending_new_place_ < 0)
            return;
        const int made = pending_new_place_;
        pending_new_place_ = -1;
        if (made >= tab_panes_ || made == tab_focused_)
            return;
        // The same bookkeeping as moving to a place by hand: the place being left keeps
        // the scroll it was left at, and the new place starts at its live bottom.
        pane_scroll_[tab_focused_] = scroll_offset_;
        tab_focused_ = made;
        scroll_offset_ = 0;
        const auto saved = pane_scroll_.find(made);
        pending_view_scroll_ = saved == pane_scroll_.end() ? 0 : saved->second;
        tab_chosen_ = true; // this window chose a place; which tab the session lights is not its business
    }
    // The keys, as methods so a check can press them without a window manager.
    void newTab(bool side_by_side = true) {
        if (!backend_)
            return;
        // Remembered here, applied when the bar arrives with the new place in it: the
        // number is the place being divided plus one, because the new place is put
        // immediately after it. See followThePlaceJustMade().
        pending_new_place_ = tab_focused_ + 1;
        backend_->newPane(side_by_side);
    }
    // Ctrl+K then N asks which way to make the new place before making it: a small dialog
    // offering Vertical (a place beside, a vertical divider) or Horizontal (a place below,
    // a horizontal one), answerable by the V or H key or by a click. Asking first is the
    // whole point — one key cannot guess which way a person wants to divide the screen.
    void askNewTab() {
        if (view_tabs_) {
            newTab(true); // one place at a time: both answers look the same on screen
            return;
        }
        askInTerminal("Split which way?",
                      "Vertical puts a place beside · Horizontal stacks one below",
                      {"Vertical", "Horizontal"}, 0,
                      [this](int which) {
                          if (which < 0)
                              return; // dismissed — no new place
                          newTab(which == 0);
                      },
                      {'V', 'H'});
    }
    void closeTab() {
        if (backend_)
            backend_->closePane();
    }
    // One place at a time with tabs, or all of them at once. The session decides and says so
    // back on the tab bar, which is how a second window on the same session finds out.
    void toggleViewMode() {
        if (backend_)
            backend_->setViewMode(!view_tabs_);
    }
    bool viewIsTabs() const { return view_tabs_; }

    // Add a place to draw beside the one this window is looking at, on a connection
    // the host has opened and pointed at `pane`. The window gives it a screen of its
    // own and a reader of its own, and paints it where the tab bar says that place
    // sits. The window does not open the connection itself: it does not know a daemon
    // exists. The host that does know opens one at the full window size — a smaller
    // view would shrink the whole session, since the session takes the size of the
    // smallest window looking at it — points it at the place to show, and hands it
    // over. This is the same seam as the first connection, one place further along.
    void addNeighbour(std::unique_ptr<TerminalBackend> backend, int pane) {
        if (!backend)
            return;
        if (pane < 0 || pane >= static_cast<int>(tab_cells_.size()))
            return; // nowhere to draw it; the tab bar has not said where that place is
        const PaneRect cell = tab_cells_[static_cast<size_t>(pane)];
        if (!cell.valid())
            return;
        auto view = std::make_unique<ExtraPane>();
        view->backend = std::move(backend);
        view->screen = Screen(cell.rows, cell.cols);
        view->cell = cell;
        view->pane = pane;
        view->generation = ++extra_generation_;
        view->alive = true;
        // The screen the new connection is already holding — the place as it looked
        // when the connection opened — is fed here, on this thread, for the same
        // reason the window feeds its own first screen there: a neighbour that waits
        // on posted events would be blank until something made it paint.
        const std::string held = view->backend->heldBytes();
        if (!held.empty()) {
            view->backend->clearHeldBytes();
            view->screen.feed(held);
        }
        // The place starts where the window left it looking, not at its live bottom.
        // The window keeps a scroll per place precisely so a place is found where it was
        // left, and a neighbour that began at the bottom would repaint the place you are
        // NOT looking at into a different place than the one you walked away from: scroll
        // the left half back to the top of a log, look at the right half, and the left
        // half silently drops to its prompt. (Kaida, 2026-11-24: "during pane 2 selection
        // we should still see the same in pane 1 as when pane 1 was selected".) Where the
        // snapshot has not arrived yet there is no history to scroll through, so the
        // number waits as `pending_scroll` and the first output applies it — the same
        // bargain the window makes with itself when it moves onto a place.
        const auto saved = pane_scroll_.find(pane);
        const int wanted = saved == pane_scroll_.end() ? 0 : saved->second;
        view->scroll = wanted;
        clampNeighbourScroll(*view);
        if (view->scroll != wanted)
            view->pending_scroll = wanted; // its history has not arrived yet
        ExtraPane* added = view.get();
        neighbours_.push_back(std::move(view));
        startNeighbourReader(added);
        update();
    }

    // The neighbour showing a given place, or null. There is at most one per place.
    ExtraPane* neighbourFor(int pane) {
        for (auto& view : neighbours_)
            if (view && view->pane == pane)
                return view.get();
        return nullptr;
    }
    const ExtraPane* neighbourFor(int pane) const {
        for (const auto& view : neighbours_)
            if (view && view->pane == pane)
                return view.get();
        return nullptr;
    }
    // Remove the neighbour looking at `pane`, if there is one, and join its reader.
    void dropNeighbour(int pane) {
        for (size_t i = 0; i < neighbours_.size(); ++i) {
            if (neighbours_[i] && neighbours_[i]->pane == pane) {
                stopNeighbourReader(std::move(neighbours_[i]));
                neighbours_.erase(neighbours_.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    bool hasExtraPane() const {
        for (const auto& view : neighbours_)
            if (view && view->alive)
                return true;
        return false;
    }
    // Everything the neighbours are showing, as one string — so a check can look for a
    // marker without saying which place it landed in. To ask one place, use
    // `neighbourText(pane)`.
    std::string extraScreenText() const {
        std::string all;
        for (const auto& view : neighbours_) {
            if (!view)
                continue;
            all += view->screen.text();
            all += '\n';
        }
        return all;
    }
    std::string neighbourText(int pane) const {
        const ExtraPane* view = neighbourFor(pane);
        return view ? view->screen.text() : std::string();
    }
    // How far a neighbour's own place is scrolled back, for a check that wants to say
    // one place scrolled without moving the others.
    int neighbourScrollAt(int pane) const {
        const ExtraPane* view = neighbourFor(pane);
        return view ? view->scroll : -1;
    }
    // Which place takes the typing: the one this window is looking at. A neighbour is a
    // pair of eyes only — to type into another place, you look at it (click its tab, or
    // its half of the screen), which moves the focus, the highlight and the cursor.
    int activePane() const { return tab_focused_; }

    // Say how to open a neighbour connection, so a window onto a session shows its
    // places side by side on its own rather than waiting to be handed one. The
    // factory is given the number of the place to show and returns a connection to
    // it; null says the place could not be opened, which leaves that place undrawn.
    void setPaneConnector(std::function<std::unique_ptr<TerminalBackend>(int pane)> connector) {
        pane_connector_ = std::move(connector);
        syncNeighbours(); // a connector set after the bar already grew should take effect now
    }

    // Bring the neighbours in line with what the tab bar says and where the window is
    // looking. Side by side with more than one place, every place other than the one
    // being looked at gets a neighbour; one place, or tabs, and there is nothing to
    // draw beside the window. A window with no way to open a connection opens none and
    // keeps whatever neighbours it was handed, only dropping the ones that have gone.
    void syncNeighbours() {
        const bool side = !view_tabs_ && tab_panes_ > 1;
        // One neighbour per place that is shown beside the focused one, and no more.
        for (int p = 0; p < tab_panes_; ++p) {
            const bool wants = side && p != tab_focused_ &&
                               p < static_cast<int>(tab_cells_.size()) &&
                               tab_cells_[static_cast<size_t>(p)].valid();
            ExtraPane* have = neighbourFor(p);
            if (!wants) {
                if (have)
                    dropNeighbour(p);
                continue;
            }
            const PaneRect cell = tab_cells_[static_cast<size_t>(p)];
            if (have) {
                // The same place, so keep its cell and screen in step with the bar.
                have->cell = cell;
                have->screen.resize(cell.rows, cell.cols);
                continue;
            }
            if (pane_connector_)
                addNeighbour(pane_connector_(p), p);
        }
        update();
    }

    // Something the session refused, said where it can be read: along the bottom of the
    // window, gone on its own after a few seconds, and gone the moment a key is typed. A
    // refusal nobody sees is a refusal that gets pressed again, and again.
    void showNotice(std::string_view text, int ms = 4000) {
        notice_ = QString::fromStdString(std::string(text));
        ++notice_revision_;
        update();
        if (ms > 0) {
            const int revision = notice_revision_;
            QTimer::singleShot(ms, this, [this, revision] {
                if (revision != notice_revision_)
                    return; // a newer notice owns the screen now
                notice_.clear();
                update();
            });
        }
    }
    std::string notice() const { return notice_.toStdString(); }

    // Where the tab slots begin and end, as fractions of the bar's width. Equal when the
    // places are shown one at a time, because then the bar is a list and no place is more
    // important than another. Proportional to the cells when they are shown side by side,
    // with the dividers left as gaps — the bar is a map of where the work is, and a map
    // that lies about the sizes of rooms is worse than no map at all.
    // The number a place is called by: the order it was made in, as the session says. When
    // the session has not said — an older session, or a shell started inside this window —
    // its position is its number, which is what those sessions mean anyway.
    int tabNumber(int slot) const {
        if (slot >= 0 && slot < static_cast<int>(tab_numbers_.size()) &&
            tab_numbers_[static_cast<size_t>(slot)] > 0)
            return tab_numbers_[static_cast<size_t>(slot)];
        return slot + 1;
    }

    // The places in the order the bar lists them: by the number each is called by, which is
    // the order they were made in. A person counting tabs counts them, so the bar counts;
    // where a place sits on screen is the arrangement's business, and the places themselves
    // show that far better than a strip above them can.
    std::vector<int> tabOrder() const {
        std::vector<int> order;
        for (int slot = 0; slot < tab_panes_; ++slot)
            order.push_back(slot);
        std::stable_sort(order.begin(), order.end(),
                         [this](int a, int b) { return tabNumber(a) < tabNumber(b); });
        return order;
    }

    std::vector<double> tabEdges() const {
        std::vector<double> edges;
        if (tab_panes_ <= 0)
            return edges;
        const bool proportional =
            !view_tabs_ && static_cast<int>(tab_cells_.size()) == tab_panes_;
        const std::vector<int> order = tabOrder();
        double total = 0.0;
        std::vector<double> widths;
        for (const int slot : order) {
            const double w =
                proportional ? std::max(1, tab_cells_[static_cast<size_t>(slot)].cols) : 1.0;
            widths.push_back(w);
            total += w;
        }
        const double gap = proportional ? 1.0 : 0.0; // a divider's worth, between places
        total += gap * (tab_panes_ - 1);
        edges.push_back(0.0);
        double used = 0.0;
        for (int at = 0; at < tab_panes_; ++at) {
            used += widths[static_cast<size_t>(at)] / total;
            edges.push_back(used);
            if (at + 1 < tab_panes_) {
                used += gap / total;
                edges.push_back(used);
            }
        }
        edges.back() = 1.0;
        return edges;
    }

    // Which place a point on the bar is over. A point in a divider belongs to no place, so
    // it changes nothing rather than picking the neighbour a hand was closest to.
    int tabSlotAt(double x, double bar_left, double bar_width) const {
        const std::vector<double> edges = tabEdges();
        if (edges.size() < 2 || bar_width <= 0.0)
            return -1;
        const double at = std::clamp((x - bar_left) / bar_width, 0.0, 1.0);
        const std::vector<int> order = tabOrder();
        for (int edge = 0; edge + 1 < static_cast<int>(edges.size()); ++edge) {
            // Odd boundaries are the far edge of a place, even ones the near edge; the
            // entries between them are the gaps left by dividers.
            if ((edge % 2) == 0 && at >= edges[edge] && at < edges[edge + 1])
                return order[static_cast<size_t>(edge / 2)];
        }
        return -1;
    }
    int shownPane() const { return tab_focused_; }
    int paneCount() const { return tab_panes_; }
    // The cell the session gave a place, so a check can tell which way a split ran:
    // side by side shares the columns (two places with the same rows), stacked shares
    // the rows (two places with the same cols).
    PaneRect paneCell(int pane) const {
        if (pane < 0 || pane >= static_cast<int>(tab_cells_.size()))
            return {};
        return tab_cells_[static_cast<size_t>(pane)];
    }

    // How far the grid is inset right now. An ordinary shell keeps the margin:
    // a borderless Wayland window has rounded corners, and a glyph in the corner
    // cell is clipped by the compositor rather than read. A full-screen
    // application holding the alternate screen wants the whole window — it
    // paints its own frame and knows where its own glyphs have to stay clear
    // of — so the inset goes to zero while it holds the screen, and the grid
    // grows to take the room the margin used to hold. The clash it removes is
    // a real one: the margin is the terminal's background, and btop's blue
    // inside a grey-black ring is two programs drawing one window.
    double activePadding() const {
        return screen_.inAlternateScreen() ? 0.0 : padding_;
    }

    // Paste as if the text came from the clipboard: newlines become carriage
    // returns and the payload is fenced when the application asked for bracketed
    // paste. Exposed because the middle-click path needs a clipboard, and a test
    // should still be able to check what the terminal would send.
    void pasteText(const std::string& text) {
        if (text.empty())
            return;
        // The same decision writeToPty makes, taken first so a paste whose program is
        // gone does not bother wrapping bytes that will be dropped anyway. Typing goes
        // to the place being looked at, whose program is this window's own backend.
        if (!shell_alive_ || !backend_->alive())
            return;

        // A line feed is delivered as Enter, so pasting a script would run
        // commands the user never pressed. Newlines go out as carriage returns
        // and the line discipline turns them back into line feeds.
        std::string out;
        out.reserve(text.size() + 16);
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '\r') {
                // CRLF counts as one newline: drop the CR, the LF becomes a CR.
                if (i + 1 < text.size() && text[i + 1] == '\n')
                    continue;
                out += '\r';
            } else if (c == '\n') {
                out += '\r';
            } else {
                out += c;
            }
        }

        // Bracketed paste tells the application these bytes are pasted text, not
        // typing, so readline and fish do not act on each line as it arrives. It is
        // the mode of the place being looked at.
        if (screen_.bracketedPaste())
            out = "\x1b[200~" + out + "\x1b[201~";

        writeToPty(out);
    }

    // A screen we are already holding does not need to be sent to us. A session's first
    // screen sits in the backend the moment its connection is made, and the only route
    // from there into the model was a reader thread posting an event to this widget —
    // which is worth nothing while the widget has not been shown, because a hidden widget
    // does not go looking at its own posted events. That is the black window on re-attach:
    // the bytes were there, the model never saw them, and the first keystroke was the next
    // thing to touch the queue. So the first screen is fed here, on the GUI thread, before
    // anybody is asked to paint.
    void feedWhatIsHeld() {
        if (!backend_)
            return;
        const std::string all = backend_->heldBytes();
        if (all.empty())
            return;
        backend_->clearHeldBytes();
        screen_.feed(all);
        if (auto title = screen_.takeTitle())
            setWindowTitle(QString::fromStdString(*title));
        if (screen_.inAlternateScreen())
            scroll_offset_ = 0;
        // A session can arrive already inside a full-screen program, and the
        // grid it was built for was inset for a shell it is not showing.
        syncGridToScreen();
        update();
    }

    bool event(QEvent* e) override {
        if (auto* tabs = dynamic_cast<TerminalTabsEvent*>(e)) {
            if (tabs->generation != generation_)
                return true; // about a session this window no longer shows
            setTabBar(tabs->panes, tabs->focused, tabs->tabs, tabs->cells, tabs->numbers,
                      tabs->mine);
            return true;
        }
        if (auto* out = dynamic_cast<TerminalOutputEvent*>(e)) {
            if (out->generation != generation_)
                return true; // belongs to a shell this window no longer shows
            const int history_before = screen_.historySize();
            const int view_top_before = viewTop();
            screen_.feed(out->data);

            // What an OSC sequence asked of the program around the model. This
            // runs before the replies are drained below, because an OSC 52 read
            // produces one and the application is waiting for it.
            if (auto title = screen_.takeTitle())
                setWindowTitle(QString::fromStdString(*title));
            // A copy request reaches us from the model when the session is a shell
            // started here, and from the daemon when it is a session elsewhere: the
            // program asked the same question either way.
            std::vector<Screen::ClipboardRequest> requests = screen_.takeClipboardRequests();
            for (auto& request : backend_->takeClipboardRequests())
                requests.push_back(std::move(request));
            for (const auto& request : requests) {
                const QClipboard::Mode mode =
                    request.primary ? QClipboard::Selection : QClipboard::Clipboard;
                if (request.query) {
                    // The application is asking to paste, over a link such as ssh
                    // where its own clipboard is not reachable. A session owned by the
                    // daemon is the same case with the link moved: the contents are
                    // here, in this window's clipboard, and the way back to the program
                    // is the byte stream it came up on. The answer has to be given
                    // either way — a view that ignored a read request would leave the
                    // program waiting, and one that mistook it for a write would empty
                    // the clipboard it was asked to read.
                    const std::string contents =
                        QGuiApplication::clipboard()->text(mode).toStdString();
                    if (backend_->answersQueries())
                        screen_.answerClipboardQuery(contents);
                    else
                        static_cast<void>(backend_->write(clipboardQueryReply(contents)));
                    continue;
                }
                // This is the application asking for its contents to be copied,
                // which is on demand like Ctrl+Shift+C is; selecting text in the
                // window still does not touch the clipboard by itself.
                QGuiApplication::clipboard()->setText(
                    QString::fromStdString(request.text), mode);
            }

            // Forward terminal-to-host query replies (DA1, DSR 5n/6n) to the PTY.
            // fish and tmux both probe the terminal at startup, and tmux uses
            // DSR 6n cursor position reports for output leak detection.
            const std::string response = screen_.takeResponse();
            if (!response.empty() && backend_->answersQueries())
                static_cast<void>(backend_->write(response));

            // The alternate screen has no history of its own, and the
            // application inside it (vim, less, tmux) owns scrolling there.
            if (screen_.inAlternateScreen())
                scroll_offset_ = 0;
            else
                followScroll(history_before);

            // A move onto a new place held its saved scroll until this snapshot filled the
            // screen; now the history is here, so put the view back where that place was
            // left. Only once there is enough history to hold the offset, so a snapshot that
            // has not fully arrived yet does not clamp the memory away.
            if (pending_view_scroll_ > 0 && !screen_.inAlternateScreen() &&
                screen_.historySize() >= pending_view_scroll_) {
                scroll_offset_ = pending_view_scroll_;
                clampScroll();
                pane_scroll_[tab_focused_] = scroll_offset_;
                pending_view_scroll_ = 0;
            }

            // The same bytes that changed whose screen this is may have changed
            // how much room the grid has: an application taking the screen over
            // gets the margin back and grows into it.
            syncGridToScreen();

            // New output moves the text under a selection. The highlight follows
            // it instead of being thrown away, so selecting from a log that is
            // still filling up keeps working; once the selected lines have gone
            // off the top the selection is dropped rather than left pointing at
            // text nobody selected.
            keepSelectionOnScreen(view_top_before);

            // ?2026 synchronized output: an application drawing a whole frame
            // writes it as one block and asks not to be shown half of it. Each read
            // from the PTY already ends in one repaint, but a burst can still land
            // mid-frame, so inside a block repaints are limited to one every
            // kSyncRepaintIntervalMs. The block ending is itself a chunk, so the
            // finished frame is painted then; and a program that turns the mode on
            // and dies cannot freeze the screen, because the next chunk repaints
            // once the interval has passed whether the mode is off or not.
            const qint64 now_ms = repaintClockElapsed();
            // The falling edge is never throttled. A block that ends less than an interval
            // after the repaint taken at its start used to be dropped — and the comment
            // here promised for years that the finished frame is painted at the end of the
            // block, which is the very frame the throttle was throwing away. A session's
            // screen arrives as one such block: painted empty at the start, filled in
            // inside the interval, and never asked to be drawn again. The person sees a
            // blank window until a keystroke asks on the screen's behalf.
            const bool block_ended = synced_block_open && !screen_.synchronizedOutput();
            synced_block_open = screen_.synchronizedOutput();
            if (block_ended
                || repaintAllowed(screen_.synchronizedOutput(), now_ms - last_repaint_ms_)) {
                last_repaint_ms_ = now_ms;
                update();
            }
            return true;
        }
        if (auto* leaving = dynamic_cast<TerminalExitEvent*>(e)) {
            // The shell this window has moved on from, saying goodbye late.
            if (leaving->generation != generation_)
                return true;
            shell_alive_ = false;
            QCoreApplication::quit();
            return true;
        }
        if (auto* extra = dynamic_cast<ExtraOutputEvent*>(e)) {
            // A neighbour, on its own screen. It is fed nothing but bytes: the title,
            // the clipboard and the queries are asked of the session through the
            // connection the window was built around, and a second pair of eyes has no
            // business answering them twice. The generation names which neighbour, so
            // output about a place this window has stopped showing is dropped.
            ExtraPane* view = nullptr;
            for (auto& n : neighbours_)
                if (n && n->generation == extra->generation) {
                    view = n.get();
                    break;
                }
            if (!view)
                return true; // about a place this window is no longer showing
            // Hold a scrolled-back neighbour on its lines while new output pushes its
            // history, the same way the window's own view is held still.
            const int history_before = view->screen.historySize();
            view->screen.feed(extra->data);
            // A place that was left scrolled comes back to there once its first screen
            // has arrived — the snapshot may reach the window here rather than at the
            // connection, and until it does the saved scroll has nowhere to be applied.
            // "Arrived" means history deep enough to hold that scroll: spending it on the
            // first chunk of a snapshot still on its way clamps the memory down to what
            // has landed and throws the rest away.
            if (view->pending_scroll > 0 && !view->screen.inAlternateScreen() &&
                view->screen.historySize() >= view->pending_scroll) {
                view->scroll = view->pending_scroll;
                view->pending_scroll = 0;
                clampNeighbourScroll(*view);
            } else if (view->scroll > 0) {
                // History shrinking is not ordinary output pushing old lines out of a
                // full ring — that leaves the count where it was. The screen has been
                // replaced under the neighbour: the daemon sending a fresh snapshot of the
                // place, or a reflow. Shifting the view by that negative difference buries
                // it below the oldest line there is, the clamp pulls it back to nothing,
                // and the place snaps to its live bottom — which is how the half you
                // walked away from stopped showing what you left it on. Put it back at the
                // scroll remembered for that place and keep the number waiting until that
                // place's own history is all in.
                const int history_now = view->screen.historySize();
                if (history_now < history_before) {
                    const auto saved = pane_scroll_.find(view->pane);
                    const int remembered =
                        saved == pane_scroll_.end() ? view->scroll : saved->second;
                    view->pending_scroll = remembered;
                    view->scroll = remembered;
                } else {
                    view->scroll += history_now - history_before;
                }
                clampNeighbourScroll(*view);
            }
            update();
            return true;
        }
        return QWidget::event(e);
    }

    void keyPressEvent(QKeyEvent* ev) override {
        // Any key that reaches the shell returns the view to the live bottom.
        // Shift+PageUp/PageDown/Home/End are kept by the terminal for scrollback
        // and are never sent to the PTY. The offset counts lines scrolled back
        // from the live bottom, so "older" means a larger offset.
        const Qt::KeyboardModifiers mods = ev->modifiers();
        const int key = ev->key();
        const bool shift = mods.testFlag(Qt::ShiftModifier);
        // The next thing being typed is worth more of the screen than the last thing that
        // did not happen.
        if (!notice_.isEmpty()) {
            notice_.clear();
            ++notice_revision_;
        }

        // Ctrl+Shift and an arrow step along the tab bar, wrapping at both ends: the places
        // are numbered, so there is nothing to remember about which is which, and the point
        // is to look at the other program without letting go of this one.
        //
        // They step by NUMBER rather than by position, because the numbers are what a person
        // is counting when they press this. The bar is drawn in creation order and the
        // places sit wherever the arrangement put them, so "the next one" from the place
        // called 2 is the one called 3 even when 3 is stacked underneath instead of beside
        // it. Right and Down run the numbers up, Left and Up run them back.
        // (Kaida, 2026-11-24: "Ctrl+Shift+Arrow should cycle in numerical order".)
        if (tab_panes_ > 1 && mods.testFlag(Qt::ControlModifier) && shift &&
            (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up ||
             key == Qt::Key_Down)) {
            const std::vector<int> order = tabOrder(); // the places, sorted by their numbers
            const int count = static_cast<int>(order.size());
            int here = 0;
            for (int i = 0; i < count; ++i)
                if (order[static_cast<std::size_t>(i)] == tab_focused_)
                    here = i;
            const int forward = (key == Qt::Key_Right || key == Qt::Key_Down) ? 1 : count - 1;
            showPaneAt(order[static_cast<std::size_t>((here + forward) % count)]);
            ev->accept();
            return;
        }

        // A modifier pressed on its own is not a key press in any sense: it is
        // the first half of a chord. Qt delivers it as Key_Control, Key_Shift and
        // so on, and treating it as ordinary input threw away the selection the
        // moment the user started pressing Ctrl+Shift+C to copy that selection.
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
            key == Qt::Key_AltGr || key == Qt::Key_Meta)
            return;

        // While one of the terminal's own dialogs is up it owns the keyboard.
        // Every key belongs to it and nothing reaches the shell: a stray
        // letter typed at a question would otherwise land in the command line
        // underneath, and pressing Enter at a "Quit while make is running?"
        // must mean the highlighted answer, not a newline someone else has to
        // explain.
        if (overlay_.active) {
            overlayKeyPress(key, mods, ev->text());
            return;
        }

        // The theme picker likewise owns the keyboard, and it is checked
        // before the copy/save/menu chords below: while you are typing hex
        // digits a `c` or an `s` is a colour, not the start of a keystroke the
        // shell would see.
        // The mixer is on top of the picker, so it is asked first: a digit belongs
        // to whichever panel is frontmost, and to nothing else.
        if (mixer_) {
            mixerKeyPress(key, mods, ev->text());
            return;
        }
        if (picker_) {
            pickerKeyPress(key, mods, ev->text());
            return;
        }

        // The settings menu owns the keyboard on the same terms: a digit is a
        // value there, and a letter may be the answer you are choosing. A menu that has
        // stepped aside for another panel has handed those terms over with the keys.
        if (settingsActive()) {
            settingsKeyPress(key, mods, ev->text());
            return;
        }

        // The tmux-style control. Ctrl+K arms a one-key command; the next key spends it
        // and nothing else. It is captured always — the shell never sees the Ctrl+K that
        // would kill its line — because a single prefix the future tty can lean on is
        // worth more here than one readline shortcut the terminal cannot offer anyway.
        if (!prefix_armed_ && mods.testFlag(Qt::ControlModifier) && !shift &&
            key == Qt::Key_K) {
            prefix_armed_ = true;
            update();
            ev->accept();
            return;
        }
        if (prefix_armed_) {
            prefix_armed_ = false; // spent, whatever the key turns out to be
            bool spent = false;
            switch (key) {
                case Qt::Key_K: if (on_settings_menu) on_settings_menu(); spent = true; break;
                case Qt::Key_N: askNewTab(); spent = true; break;
                case Qt::Key_T: toggleViewMode(); spent = true; break;
                case Qt::Key_R: enterResizeMode(); spent = true; break;
                case Qt::Key_F: if (on_file_manager) on_file_manager(); else setStatusText("file manager: not wired yet"); spent = true; break;
                case Qt::Key_S: if (on_save_session) on_save_session(); spent = true; break;
                case Qt::Key_A: if (on_session_menu) on_session_menu(); spent = true; break;
                case Qt::Key_Escape: spent = true; break; // a way back out that spends nothing
                default: break;                            // some other key: give it to the shell
            }
            update();
            if (spent) {
                ev->accept();
                return;
            }
            // Fall through with a disarmed control so an unrecognised key still reaches
            // the shell, exactly as if the control had never been pressed.
        }

        // Resize mode owns the keyboard: the arrows stretch the place in hand along the
        // axis they point, Shift runs the divider the other way, Enter keeps the new
        // arrangement and Esc puts it back. Nothing reaches the shell while it is up —
        // an arrow that resized a pane should not also move a cursor underneath it.
        if (resize_mode_) {
            const int step = 1;
            if (key == Qt::Key_Left || key == Qt::Key_Right) {
                const int dir = key == Qt::Key_Right ? step : -step;
                if (backend_)
                    backend_->resizeDivider(shift ? -dir : dir, 0);
            } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
                const int dir = key == Qt::Key_Down ? step : -step;
                if (backend_)
                    backend_->resizeDivider(0, shift ? -dir : dir);
            } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                endResizeMode(true);
            } else if (key == Qt::Key_Escape) {
                endResizeMode(false);
            }
            ev->accept();
            return;
        }

        // Ctrl+Shift+C and Ctrl+Shift+V are the terminal's own, handled before
        // anything else and never written to the PTY. Without the early return
        // Ctrl+Shift+C would encode as Ctrl+C and interrupt whatever is running.
        // These two stay on Ctrl+Shift while the rest of the window's commands moved
        // behind Ctrl+K: copy and paste are reflexes worth keeping free of a prefix,
        // and neither steals a key the shell or a program inside it needs.
        if (mods.testFlag(Qt::ControlModifier) && shift) {
            if (key == Qt::Key_C) {
                copySelection();
                return;
            }
            if (key == Qt::Key_V) {
                scroll_offset_ = 0;
                pasteFromClipboard();
                return;
            }
        }

        if (shift && (key == Qt::Key_PageUp || key == Qt::Key_PageDown ||
                      key == Qt::Key_Home || key == Qt::Key_End)) {
            if (key == Qt::Key_PageUp)
                scrollBy(std::max(1, rows_ - 1));
            else if (key == Qt::Key_PageDown)
                scrollBy(-std::max(1, rows_ - 1));
            else if (key == Qt::Key_Home)
                scrollBy(scrollRange());
            else
                scrollBy(-scrollRange());
            return;
        }

        returnToBottom();
        clearSelection();

        // Encoded against the modes of the place being looked at and written to its
        // program. Typing goes to the place this window is looking at, never to a
        // neighbour: to type in another place, look at it first.
        const std::string bytes =
            encodeKey(key, mods, ev->text(), screen_.applicationCursorKeys());
        writeToPty(bytes);
    }

    // Mouse handling splits on what the application asked for. With reporting on
    // (vim, htop, tmux) events are encoded and written to the PTY and the
    // terminal keeps no selection. With it off, the left button drags a
    // selection and the middle button pastes. Holding Shift overrides reporting,
    // the xterm rule, and is how you copy text out of a program that grabbed the
    // mouse.
    void mousePressEvent(QMouseEvent* ev) override {
        setFocus();
        // A dialog is up: the press either answers it or dismisses it, and it
        // is never a selection drag or a scroll-bar grab pretending not to be.
        if (overlay_.active) {
            overlayPress(ev->position());
            return;
        }
        // A press on the tab bar asks to be shown that pane, and is never the start of a
        // selection: the strip is above the grid, so there is nothing up there to select.
        if (tab_panes_ > 1 && ev->position().y() >= activePadding() &&
            ev->position().y() < gridTop()) {
            const double bar_left = activePadding();
            showPaneAt(tabSlotAt(ev->position().x(), bar_left, width() - 2.0 * bar_left));
            return;
        }
        if (mixer_) {
            mixerPress(ev->position());
            return;
        }
        if (settingsActive()) {
            settingsPress(ev->position());
            return;
        }
        if (picker_) {
            pickerPress(ev->position());
            return;
        }
        stopEdgeScroll();
        if (scrollBarPress(ev->position()))
            return;
        // Side by side, a left press on a place that is not the one being looked at
        // looks at it: the focus, the highlight and the typing all move there. A press
        // on the place already being looked at falls through, so it can still start a
        // selection and answer a mouse report like any other terminal.
        if (splitActive() && ev->button() == Qt::LeftButton) {
            const CellPos where = cellAt(ev->position());
            const int clicked = paneAtGrid(where.row, where.col);
            if (clicked >= 0 && clicked != tab_focused_) {
                showPaneAt(clicked);
                return;
            }
        }
        const CellPos pos = cellAt(ev->position());
        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        if (screen_.mouseReporting() && !force_local) {
            MouseReport rep;
            rep.button = mouseKey(ev->button());
            rep.held = heldBits(ev->buttons());
            rep.action = MouseAction::Press;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            last_reported_ = pos;
            writeToPty(mouseBytes(rep));
            return;
        }

        if (ev->button() == Qt::MiddleButton) {
            pasteFromClipboard();
            return;
        }
        if (ev->button() != Qt::LeftButton)
            return;

        // Shift+click moves the end of an existing selection, which is what
        // every other text surface does and the only way to make a long
        // selection without dragging across the whole window. The gesture that
        // started the selection decides whether it still grows by words or lines.
        if (force_local && !selection_.empty()) {
            dragging_ = true;
            extendSelection(pos);
            update();
            return;
        }

        // Qt 6.11 as packaged here exposes no click count on QMouseEvent, and a
        // synthesized event would not carry one anyway, so presses that land on
        // the same cell in quick succession are counted locally: two select a
        // word, three select the line.
        const int clicks = countClick(pos);
        if (clicks >= 3)
            drag_mode_ = DragMode::Line;
        else if (clicks == 2)
            drag_mode_ = DragMode::Word;
        else
            drag_mode_ = DragMode::Char;
        dragging_ = true;
        anchor_ = pos;
        extendSelection(pos);
        update();
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        // A bar being pushed along: the value follows the pointer for as long as
        // the button is down, which is what makes a colour you are hunting for
        // pass by on the way to the one you meant.
        if (mixer_ && mixer_drag_ >= 0 && (ev->buttons() & Qt::LeftButton)) {
            mixerBarPress(mixer_drag_, ev->position());
            ev->accept();
            return;
        }
        if (overlay_.active) {
            overlayHover(ev->position());
            return;
        }
        if (picker_)
            return;
        if (scrollbar_drag_) {
            scrollBarDragTo(ev->position());
            return;
        }
        const CellPos pos = cellAt(ev->position());
        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        if (screen_.mouseReporting() && !force_local) {
            const bool held = ev->buttons() != Qt::NoButton;
            if (!held && screen_.mouseMode() != MouseMode::Any)
                return;
            // Only cell changes are reported, which is what xterm does and what
            // keeps a slow application from drowning in motion events.
            if (pos.row == last_reported_.row && pos.col == last_reported_.col)
                return;
            MouseReport rep;
            rep.button = MouseKey::None;
            rep.held = heldBits(ev->buttons());
            rep.action = MouseAction::Motion;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            last_reported_ = pos;
            writeToPty(mouseBytes(rep));
            return;
        }

        if (!dragging_)
            return;
        updateEdgeScroll(pos);
        extendSelection(pos);
        update();
    }

    // Selecting in any other text surface keeps going when the pointer is held
    // against the top or bottom of the window: the view crawls and the selection
    // grows with it. Without this, a drag stops dead one row from the edge and
    // text further away can only be reached by letting go and dragging again.
    void updateEdgeScroll(const CellPos& pos) {
        edge_col_ = pos.col;
        int direction = 0;
        if (pos.row <= 0 && scroll_offset_ < scrollRange())
            direction = 1; // held at the top: bring older lines in
        else if (pos.row >= rows_ - 1 && scroll_offset_ > 0)
            direction = -1; // held at the bottom: bring newer lines back
        if (direction == edge_direction_)
            return;
        edge_direction_ = direction;
        if (direction == 0) {
            stopEdgeScroll();
            return;
        }
        if (!edge_timer_) {
            edge_timer_ = new QTimer(this);
            edge_timer_->setInterval(kEdgeScrollMs);
            connect(edge_timer_, &QTimer::timeout, this, [this] { edgeScrollStep(); });
        }
        edge_timer_->start();
    }

    void stopEdgeScroll() {
        edge_direction_ = 0;
        if (edge_timer_)
            edge_timer_->stop();
    }

    void edgeScrollStep() {
        const int direction = edge_direction_;
        const int view_top_before = viewTop();
        const CellPos anchor_before = anchor_;
        const bool had_selection = !selection_.empty();
        scrollBy(direction);
        if (had_selection && selection_.empty()) {
            // The view carried the anchor out of the window and the ordinary rule
            // dropped the selection. Mid-drag that is wrong: the pointer is still
            // held down and still selecting, so keep the drag alive at the edge
            // the anchor left. Text that has scrolled out of reach is not added to
            // the selection; what is on screen is.
            dragging_ = true;
            const PaneRect kept = paneForDrag(panes_, anchor_before, rows_, cols_);
            anchor_.row = kept.clampRow(anchor_before.row + (view_top_before - viewTop()));
        }
        // Crawling to the edge of the window stops at the edge of the pane: the rows
        // beyond it are somebody else's shell.
        const PaneRect pane = paneForDrag(panes_, anchor_, rows_, cols_);
        extendSelection(CellPos{direction > 0 ? pane.firstRow() : pane.lastRow(), edge_col_});
        update();
    }

    // Letting go, or taking the pointer out of the window, ends the crawl.
    void leaveEvent(QEvent* ev) override {
        stopEdgeScroll();
        QWidget::leaveEvent(ev);
    }

    void mouseReleaseEvent(QMouseEvent* ev) override {
        mixer_drag_ = -1;
        stopEdgeScroll();
        if (scrollbar_drag_) {
            endScrollBarDrag();
            return;
        }
        const CellPos pos = cellAt(ev->position());
        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        if (screen_.mouseReporting() && !force_local) {
            MouseReport rep;
            rep.button = mouseKey(ev->button());
            rep.held = heldBits(ev->buttons());
            rep.action = MouseAction::Release;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            writeToPty(mouseBytes(rep));
            return;
        }

        if (!dragging_)
            return;
        dragging_ = false;
        // A finished drag copies nothing, in either direction. The highlight
        // stays on screen for the user to read, and Ctrl+Shift+C is what puts the
        // text where anything can paste it.
        update();
    }

    // Wheel scrolls the retained history: a notch away from the user shows
    // older lines. With mouse reporting on it is a button for the application
    // instead, which is how scrolling works in vim and less.
    void wheelEvent(QWheelEvent* ev) override {
        // The view does not scroll out from under a dialog: while one is up
        // the wheel has nothing to do, and saying so is better than letting
        // the question float away from the text behind it.
        if (overlay_.active) {
            ev->accept();
            return;
        }
        // Over one of the two lists, the wheel walks the highlight, which is what
        // the arrows do. A picker is usually taller than the window holding it, and
        // a wheel that scrolled the terminal behind it would move text nobody is
        // looking at while leaving the row you meant to click off screen.
        if (mixer_ || picker_ || settingsActive()) {
            const int steps = ev->angleDelta().y() / 120;
            if (steps != 0) {
                const int direction = steps > 0 ? -1 : 1;
                if (mixer_) {
                    int row = mixer_->row();
                    overlayWalk(mixer_focus_, row, kMixChannelCount, direction);
                    if (mixer_focus_ == 0)
                        mixer_->setRow(row);
                } else if (picker_) {
                    int row = picker_->row();
                    overlayWalk(picker_focus_, row, static_cast<int>(picker_->fields().size()),
                                direction);
                    picker_->setRow(row);
                } else {
                    int row = settings_->row();
                    overlayWalk(settings_focus_, row, static_cast<int>(settings_->rows().size()),
                                direction);
                    settings_->setRow(row);
                }
                update();
            }
            ev->accept();
            return;
        }
        const int steps = ev->angleDelta().y() / 120;
        if (steps == 0)
            return;

        const bool force_local = ev->modifiers().testFlag(Qt::ShiftModifier);

        // The wheel scrolls the place being looked at, and only that place — wherever
        // the pointer happens to be. A scroll that reached the place under the cursor
        // moved a history the person was not working in, and left the place they *were*
        // working in untouched; the selected place is the one the keys, the highlight and
        // now the wheel all belong to. So a split is skipped here entirely and the scroll
        // falls through to the window's own view below.

        if (screen_.mouseReporting() && !force_local) {
            const CellPos pos = cellAt(ev->position());
            MouseReport rep;
            rep.button = (steps > 0) ? MouseKey::WheelUp : MouseKey::WheelDown;
            rep.held = 0;
            rep.action = MouseAction::Press;
            rep.mods = mouseMods(ev->modifiers());
            rep.row = pos.row;
            rep.col = pos.col;
            writeToPty(mouseBytes(rep));
            ev->accept();
            return;
        }

        // While the alternate screen is active the application owns scrolling.
        if (screen_.inAlternateScreen())
            return;
        scrollBy(steps * kWheelRows);
        ev->accept();
    }

    // The model has no font and no way to measure one, but answers geometry
    // queries (XTWINOPS) in pixels, so it is told what a cell measures here.
    void updateCellPixels() {
        screen_.setCellPixels(static_cast<int>(cellWidth() + 0.5),
                              static_cast<int>(cellHeight() + 0.5));
    }

    // A window being shown is a window about to be handed a surface by something that has
    // not finished its own work yet. From this moment a paint is wanted until one lands.
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        shown_paint_count_ = paint_count_;
        if (repaint_timer_ == 0)
            repaint_timer_ = startTimer(kRepaintPollMs);
        repaint_deadline_.invalidate();
    }

    // A window that is not the one you are typing into shows a steady cursor. The blink
    // is there so the eye can find where the typing goes, and no typing is going there;
    // the phase keeps turning underneath so coming back does not hand you a cursor that
    // was dark the moment focus left.
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::ActivationChange && cursor_blink_ms_ > 0)
            repaintCursorCell();
    }

    // The asking for a paint, for as long as one has not happened. It stops by itself once
    // the window has drawn, so a window that was shown normally is not left redrawing
    // itself for no reason, and a window that was shown into a surface that was not there
    // yet keeps asking until something appears. It gives up after a while too: a window
    // nobody ever looks at is not worth asking forever.
    void timerEvent(QTimerEvent* e) override {
        if (e->timerId() != repaint_timer_) {
            QWidget::timerEvent(e);
            return;
        }
        if (paint_count_ > shown_paint_count_) {
            killTimer(repaint_timer_);
            repaint_timer_ = 0;
            return;
        }
        if (!repaint_deadline_.isValid())
            repaint_deadline_.start();
        if (repaint_deadline_.elapsed() > kRepaintAskMs) {
            killTimer(repaint_timer_);
            repaint_timer_ = 0;
            return;
        }
        update();
    }

    void resizeEvent(QResizeEvent*) override {
        applyGridGeometry();
    }

    // Work out how many rows and columns fit the window at the padding the
    // screen is in — zero for a full-screen application, the margin for a
    // shell — and tell the model and the shell when that changed. Called on
    // every resize and every change of hands between the shell and an
    // application taking the screen over, because both change the room.
    void applyGridGeometry() {
        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;
        updateCellPixels(); // the cell size may have changed with a different font

        // The size of the screen is worked out from the margin the shell is drawn
        // inside, never from the inset whoever holds the screen is drawing under right
        // now. The two differ — a program painting its own frame starts at the window's
        // edge, so its inset is zero — and a grid that followed the inset would gain a
        // row and a column every time a program took the screen and lose them again
        // when it gave the screen back. The pty is told this number, so the shell would
        // hear that its terminal had grown and shrunk while every line it drew was
        // going to the hidden screen; fish answers that by repainting with `ESC[3J`,
        // and the user loses their scrollback to an afternoon of nano sessions. A size
        // is not a drawing detail: it is a promise to another process.
        const double pad = padding_;
        geometry_padding_ = activePadding(); // where that grid is drawn right now
        const int new_cols = std::max(1, static_cast<int>((width() - 2.0 * pad) / cw));
        // The tab bar's strip comes off the height the grid is counted at, so that the
        // size promised to the program is the room it is actually drawn in.
        const int new_rows =
            std::max(1, static_cast<int>((height() - 2.0 * pad - tabStrip()) / ch));
        if (new_rows == rows_ && new_cols == cols_) {
            // The window is the same size it was, but the place this window is looking at
            // may not be: dividing a place, closing one or dragging a divider changes that
            // place's cell while the window around it does not move. The screen this window
            // paints is the size of that cell, so it has to be asked again — otherwise a
            // new place is drawn from a screen sized for the place that was divided, which
            // shows a cell full of nothing where a prompt is waiting further down.
            resizeViewToFocusedCell();
            update();
            return;
        }

        rows_ = new_rows;
        cols_ = new_cols;
        screen_.resize(rows_, cols_);
        backend_->resize(rows_, cols_); // the session keeps the window's size, not the cell's
        // While the places share the screen, the window is shown the place in hand at that
        // place's cell — the same size the daemon draws the place's screen, and the same a
        // neighbour takes. Sized to the whole grid, the place in hand's screen would fill
        // the window and its bottom — the prompt, always the last line — would be painted
        // below the cell and lost. With one place, or places shown one at a time, there is
        // no cell smaller than the window to shrink to, so the view stays the whole window.
        resizeViewToFocusedCell();
        // Every neighbour is a view of the same session, so each is told the same window
        // size — a smaller view would shrink the session for everyone, since the session
        // takes the size of the smallest window looking at it. Each one's own screen then
        // follows the cells the session sends back for it, which the tab bar carries on
        // the next frame; a neighbour whose place has gone is dropped.
        for (auto& view : neighbours_) {
            if (!view || !view->backend)
                continue;
            view->backend->resize(rows_, cols_);
            if (view->pane >= 0 && view->pane < static_cast<int>(tab_cells_.size())) {
                const PaneRect cell = tab_cells_[static_cast<size_t>(view->pane)];
                if (cell.valid()) {
                    view->cell = cell;
                    view->screen.resize(cell.rows, cell.cols);
                    clampNeighbourScroll(*view);
                }
            }
        }
        // Reflow moves text between rows and columns, so a highlight drawn before
        // the resize would be sitting over different characters afterwards. It is
        // dropped rather than left to drift onto text nobody selected.
        clearSelection();
        clampScroll();
        update();
    }

    // The window's own screen shows the place in hand at that place's cell size while the
    // places share the screen, matching the screen the daemon draws for it; with one place,
    // or places shown one at a time, the view is the whole window. The connection always
    // reports the whole window, so the session is not shrunk to a single place.
    void resizeViewToFocusedCell() {
        if (!backend_)
            return;
        int vr = rows_;
        int vc = cols_;
        if (splitActive() && tab_focused_ >= 0 &&
            tab_focused_ < static_cast<int>(tab_cells_.size())) {
            const PaneRect cell = tab_cells_[static_cast<size_t>(tab_focused_)];
            if (cell.valid()) {
                vr = cell.rows;
                vc = cell.cols;
            }
        }
        if (screen_.rows() == vr && screen_.cols() == vc)
            return;
        screen_.resize(vr, vc);
        clampScroll();
    }

    // The screen changing hands between the shell and an application taking the
    // whole thing over moves the inset the grid is drawn at, because the margin comes
    // and goes with it. Nothing else follows: the size of the screen belongs to the
    // shell and stays put, so this is a repaint at the new inset and no more. Called
    // after anything that feeds the model, and cheap when nobody changed hands — one
    // comparison against the inset the last paint used, which is either the margin
    // itself or zero, never something computed.
    void syncGridToScreen() {
        if (geometry_padding_ == activePadding())
            return;
        geometry_padding_ = activePadding();
        update();
    }

    void paintEvent(QPaintEvent*) override {
        ++paint_count_;
        QPainter p(this);
        const Color background = screen_.defaultBackground();
        p.fillRect(0, 0, width(), height(), ground(background));

        QFontMetricsF fm(font_);
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        const double ch = fm.height();
        if (cw <= 0.0 || ch <= 0.0)
            return;

        const Color default_bg = background; // an OSC 11 may have changed it
        // Where the grid starts: inset for a shell, flush for the application
        // holding the alternate screen.
        const double grid_pad = activePadding();
        const double grid_top = gridTop();

        // The tab bar: one place to look at per tab, with the one this window is looking
        // at marked. The tabs are numbered rather than named after the program inside them,
        // because the program changes a dozen times an afternoon and the place does not.
        if (tab_panes_ > 1) {
            const double strip = tabStrip();
            const double bar_w = width() - 2.0 * grid_pad;
            p.fillRect(QRectF(grid_pad, grid_pad, bar_w, strip), ground(default_bg, 40));
            const std::vector<double> edges = tabEdges();
            const std::vector<int> order = tabOrder();
            for (int pane = 0; pane + 1 < static_cast<int>(edges.size()); ++pane) {
                if ((pane % 2) != 0)
                    continue; // the gap a divider leaves, which is left as the bar's ground
                const QRectF slot(grid_pad + edges[pane] * bar_w, grid_pad,
                                  (edges[pane + 1] - edges[pane]) * bar_w, strip);
                const int place = order[static_cast<size_t>(pane / 2)];
                const bool looked_at = place == tab_focused_;
                if (looked_at) {
                    p.fillRect(slot, ground(default_bg, 110));
                    // The accent, which is the colour a dialog puts on the answer its focus
                    // is on: the one colour in a theme that means "here", and which place a
                    // window is looking at is precisely the thing worth saying out loud. A
                    // lit rectangle of grey says only that something is selected, which every
                    // selected row everywhere already says.
                    const QColor accent_ink(theme_.accent.r, theme_.accent.g, theme_.accent.b);
                    p.fillRect(slot, QColor(accent_ink.red(), accent_ink.green(),
                                            accent_ink.blue(), 46));
                    p.setPen(QPen(accent_ink, 2.0));
                    p.drawLine(QPointF(slot.left(), slot.bottom() - 1.0),
                               QPointF(slot.right(), slot.bottom() - 1.0));
                    p.setPen(accent_ink);
                } else {
                    p.setPen(ink(theme_.foreground, 140));
                }
                // The number the place is called by, which is the order it was made in —
                // so a pane keeps the number it was given, and the bar lists them in order.
                p.drawText(slot, Qt::AlignCenter, QString::number(tabNumber(place)));
            }
            // A rule under the bar, so the strip reads as something the grid sits on
            // rather than as a row of the grid that failed to draw.
            p.fillRect(QRectF(grid_pad, grid_pad + strip, bar_w, 1.0),
                             ink(theme_.foreground, 46));
        }

        if (splitActive()) {
            // Every place painted into the cells the session gave it. The place this
            // window is looking at is its own screen; each other place is a neighbour's.
            // Which is left and which is right is the arrangement's, not a guess here, so
            // each is drawn from its own rect, and each scrolled on its own.
            for (int pane = 0; pane < tab_panes_ && pane < static_cast<int>(tab_cells_.size());
                 ++pane) {
                const PaneRect cell = tab_cells_[static_cast<size_t>(pane)];
                if (!cell.valid())
                    continue;
                if (pane == tab_focused_) {
                    // The place being looked at is the place a drag selects in, so it is
                    // painted with its selection — asked in grid coordinates, because that
                    // is the space the drag was recorded in.
                    paintScreenInto(p, fm, cw, ch, grid_pad, grid_top, screen_, cell,
                                    default_bg, scroll_offset_,
                                    [&](int row, int col) { return selectedAt(row, col); });
                } else if (const ExtraPane* n = neighbourFor(pane)) {
                    paintScreenInto(p, fm, cw, ch, grid_pad, grid_top, n->screen, cell,
                                    default_bg, n->scroll);
                }
            }
            // A line in every gap between two places, so each reads as its own place and not
            // as one screen with a stripe that failed to draw. It sits in the row or column
            // the arrangement left empty, so it never covers a character, and it is drawn for
            // every pair — not only the one the focus happens to touch, which is why a
            // divider used to vanish the moment the other place was selected. A place with
            // room empty to its right is divided from it by a vertical line; a place with
            // room empty below it is divided by a horizontal one, which is what a
            // top-and-bottom split otherwise has no way to show.
            //
            // Both are `divider_width_` pixels thick, centred in the gap: filling the gap
            // instead made the vertical one a column wide and the horizontal one a row tall,
            // which is two different thicknesses and neither of them a line. The colour is
            // the theme's accent, the same ink the rest of the window marks itself with.
            p.setPen(Qt::NoPen);
            const QColor divider_ink(theme_.accent.r, theme_.accent.g, theme_.accent.b);
            p.setBrush(divider_ink);
            const double thick = divider_width_;
            if (thick > 0.0) {
                for (int pane = 0;
                     pane < tab_panes_ && pane < static_cast<int>(tab_cells_.size()); ++pane) {
                    const PaneRect cell = tab_cells_[static_cast<size_t>(pane)];
                    if (!cell.valid())
                        continue;
                    // The vertical line, in the column just right of this place.
                    if (cell.endCol() < cols_ && paneAtGrid(cell.row, cell.endCol()) == -1)
                        p.drawRect(QRectF(grid_pad + cell.endCol() * cw + (cw - thick) / 2.0,
                                          grid_top + cell.row * ch, thick, cell.rows * ch));
                    // The horizontal line, in the row just below this place.
                    if (cell.endRow() < rows_ && paneAtGrid(cell.endRow(), cell.col) == -1)
                        p.drawRect(QRectF(grid_pad + cell.col * cw,
                                          grid_top + cell.endRow() * ch + (ch - thick) / 2.0,
                                          cell.cols * cw, thick));
                }
            }
        } else {
            for (int r = 0; r < rows_; ++r) {
                const Row& line = displayRow(r);
                // History rows keep the width they were written at, so a row can be
                // narrower than the current screen after a resize.
                const int line_cols = std::min(cols_, static_cast<int>(line.size()));
                for (int c = 0; c < line_cols; ++c) {
                    paintCell(p, fm, cw, ch, grid_pad + c * cw, grid_top + r * ch, line[c],
                              selectedAt(r, c), default_bg);
                }
            }
        }

        // The cursor of the place being looked at, drawn as a block in that place's own
        // cell. Only one cursor is drawn, and it is on the place that takes the typing:
        // a window that showed a cursor in each place would not say which one the keys
        // were going to, and the whole point of the split is that there is a place to be
        // typing. It sits where the highlight sits, so the bar and the grid agree.
        if (splitActive()) {
            const PaneRect& cell = tab_cells_[static_cast<size_t>(tab_focused_)];
            // The cursor belongs to a row of this place's screen, and a place scrolled
            // back shows every one of those rows `scroll` lines lower than it sits in the
            // screen — the same shift `paintScreenInto` runs on the text. Clamping the
            // cursor into the cell instead, which is what this used to do, leaves it
            // stranded on whatever history line happens to fall last, parked in the column
            // the prompt left empty and wearing the glyph of a row that is not the one
            // under it. It read as a cursor waiting in a place nobody is typing. So the
            // cursor is drawn where its own row is drawn and nowhere else: scroll back
            // past the live bottom and its row walks out below the cell and there is no
            // cursor at all, which is the same answer the single screen gives a few lines
            // below with `scroll_offset_ == 0`. Scroll only partway and a cursor that is
            // still on screen follows its own row down, because that is where the row is.
            const int shown = std::min(cell.rows, screen_.rows());
            const int cy = screen_.cursorY() - (screen_.rows() - shown) + scroll_offset_;
            if (screen_.cursorVisible() && cursorLit() && cy >= 0 && cy < shown) {
                const int cx = std::clamp(screen_.cursorX(), 0, cell.cols - 1);
                // The glyph under the cursor is the one that was actually painted on that
                // row, so it comes from the scrolled view rather than from the live
                // screen on its own — otherwise the block repaints a character from a row
                // that is not underneath it.
                const Row& line = rowToShow(screen_, scroll_offset_, screen_.cursorY());
                const Cell* under = cx < static_cast<int>(line.size())
                                        ? &line[static_cast<size_t>(cx)]
                                        : nullptr;
                const double x = grid_pad + (cell.col + cx) * cw;
                const double y = grid_top + (cell.row + cy) * ch;
                p.setPen(Qt::NoPen);
                p.setBrush(ink(theme_.cursor));
                p.drawRect(QRectF(x, y, cw * (under ? std::max(1, under->width) : 1), ch));
                if (under && !under->text.empty()) {
                    p.setPen(ink(cellColour(under->bg, under->bg_role, false)));
                    p.setBrush(Qt::NoBrush);
                    p.drawText(QPointF(x, y + fm.ascent()),
                               QString::fromStdString(under->text));
                }
            }
        }

        // Visible terminal cursor; tmux hides it during redraws via ESC[?25l.
        // It belongs to the live screen, so it is not drawn over history. Its
        // shape is the application's to choose (DECSCUSR); the default is the
        // underline this terminal has always drawn. The blink is a third answer to
        // the same question — is the cursor being drawn right now — and the dark half
        // of it is simply the cell underneath, which is why it is a gate here rather
        // than a colour. A split window has already drawn the cursor of the half being
        // typed into, so this one is for the ordinary single screen only.
        if (!splitActive() && scroll_offset_ == 0 && screen_.cursorVisible() && cursorLit()) {
            const int cx = std::clamp(screen_.cursorX(), 0, cols_ - 1);
            const int cy = std::clamp(screen_.cursorY(), 0, rows_ - 1);
            const Cell& under = screen_.at(cy, cx);
            const double x = grid_pad + cx * cw;
            const double y = grid_top + cy * ch;
            const double span = cw * std::max(1, under.width);
            p.setPen(Qt::NoPen);
            switch (screen_.cursorShape()) {
                case Screen::CursorShape::Block:
                    // A block covers the glyph it is on, which is only readable if
                    // that glyph is drawn again on top in the colour it is now
                    // sitting on. Skip the second pass and the character under the
                    // cursor simply disappears — a block cursor that eats text.
                    p.setBrush(ink(theme_.cursor));
                    p.drawRect(QRectF(x, y, span, ch));
                    if (!under.text.empty()) {
                        p.setPen(ink(cellColour(under.bg, under.bg_role, false)));
                        p.setBrush(Qt::NoBrush);
                        p.drawText(QPointF(x, y + fm.ascent()), QString::fromStdString(under.text));
                    }
                    break;
                case Screen::CursorShape::Bar:
                    p.setBrush(ink(theme_.cursor, 210));
                    p.drawRect(QRectF(x, y, std::min(2.0, cw), ch));
                    break;
                case Screen::CursorShape::Underline:
                    p.setBrush(ink(theme_.cursor, 210));
                    p.drawRect(QRectF(x, y + ch - 2.0, span, 2.0));
                    break;
            }
        }

        // The scroll indicator is painted last, over the grid, so it never takes
        // a column away from the text. It only appears when there is somewhere
        // else to look.
        scroll_bar_ = scrollIndicator();
        if (scroll_bar_.visible) {
            const int alpha = scrollbar_drag_ ? 190 : 90;
            p.setPen(Qt::NoPen);
            p.setBrush(ink(220, 220, 220, alpha));
            p.drawRoundedRect(scroll_bar_.thumb.adjusted(2, 0, -2, 0), kScrollBarRadius,
                              kScrollBarRadius);
        }

        // The window's own line, drawn last and over everything else, because it is a
        // word about the window rather than part of the shell inside it.
        if (!status_.empty()) {
            const QString line = QString::fromStdString(status_);
            const double box_w = fm.horizontalAdvance(line) + 2 * padding_;
            const double box_h = ch + padding_;
            const QRectF box(width() - box_w - padding_, height() - box_h - padding_, box_w,
                             box_h);
            p.setPen(QPen(ink(220, 220, 220, 200), 1));
            p.setBrush(ground(20, 20, 20, 235));
            p.drawRoundedRect(box, kScrollBarRadius, kScrollBarRadius);
            p.setPen(ink(240, 240, 240));
            p.drawText(box, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, line);
        }

        // A dialog, if one is up, sits over everything: it is the thing being
        // read at this moment, and it belongs to this terminal rather than to
        // the desktop around it.
        paintOverlay(p);
        // The colour list makes way for the mixer it was opened from. Two panels
        // stacked with a gap between them read as one crowded panel, and the row
        // the mixer came from is not something you are choosing between while you
        // are mixing; it is back the moment the mixer closes. (Kaida, 2026-11-20:
        // "don't show the color menu when in the color picker, it looks clustered
        // underneath".)
        // A refusal, along the bottom of the window where the line between the work and its
        // edge already is. tmux says these in its status line; there is no status line here,
        // and a refusal that only exists in a log is one nobody learns from. Centred, and
        // under the dialogs: a question being asked is worth more of the screen than a thing
        // that failed.
        if (!notice_.isEmpty()) {
            const double row = cellHeight();
            const double text_w = QFontMetricsF(font()).horizontalAdvance(notice_);
            const double box =
                std::min(static_cast<double>(width()) - 2.0 * grid_pad,
                         text_w + 2.0 * std::max(1.0, cellWidth()));
            const QRectF strip((width() - box) / 2.0, height() - grid_pad - row, box, row);
            p.fillRect(strip, ground(default_bg, 235));
            p.setPen(ink(theme_.foreground, 255));
            p.drawText(strip, Qt::AlignCenter, notice_);
        }

        // The control waiting on a key says so, and what it will take. Drawn over the
        // work like the refusal line, because arming is a word from the window. When the
        // hint is switched off the control still waits — it just keeps quiet about it.
        // The chord's own guide: a panel in the middle of the window, on the same surface
        // the settings and the split question are drawn on, with one key to a row and what
        // that key does beside it. It used to be a single line along the bottom edge, which
        // listed the letters and left their meaning to be remembered — and a line you have
        // to already understand is not a guide.
        // (Kaida, 2026-11-25: "the tooltips when pressing Ctrl+k should appear in a centered
        // windows (akin to the setting) explaining the individual keys".)
        if (prefix_armed_ && show_key_hints_)
            paintKeyGuide(p);

        if (!mixer_)
            paintPicker(p);
        paintSettings(p);
        paintMixer(p); // on top of the list it was opened from
    }

    // Whether the window is showing several places at once: side by side, a session
    // with more than one place, and at least one neighbour still there to be seen. The
    // grid is painted as places only in this case; every other state — one place, tabs,
    // neighbours not yet opened — is the ordinary single screen, painted as it was.
    bool splitActive() const {
        return hasExtraPane() && !view_tabs_ && tab_panes_ > 1;
    }

    // Paint one screen into the cells the session gave it, cell for cell, scrolled
    // back by `scroll` lines. This is the grid loop the window has always run, with the
    // cell's own origin and extent in place of the whole window's. Which cells are
    // highlighted is the caller's, because only the place being looked at is the place
    // being selected in. It is here so every place goes through the same `paintCell`,
    // and so the same rules.
    void paintScreenInto(QPainter& p, const QFontMetricsF& fm, double cw, double ch,
                         double grid_pad, double grid_top, const Screen& screen,
                         const PaneRect& cell, const Color& default_bg, int scroll = 0) const {
        // The neighbours are not the place being selected in, so nothing in them is
        // highlighted: a band in a place the pointer is not touching would promise text
        // that a copy would not bring back.
        paintScreenInto(p, fm, cw, ch, grid_pad, grid_top, screen, cell, default_bg, scroll,
                        [](int, int) { return false; });
    }

    template <typename Selected>
    void paintScreenInto(QPainter& p, const QFontMetricsF& fm, double cw, double ch,
                         double grid_pad, double grid_top, const Screen& screen,
                         const PaneRect& cell, const Color& default_bg, int scroll,
                         Selected selected) const {
        // The cell comes from the tab bar's current layout and the screen from the
        // connection, and during a resize they can disagree by a row or a column for a
        // frame: the bar has the new size before the neighbour's screen has been
        // resized to it. Paint what the screen actually holds and leave the rest as
        // ground — the same reason the columns are clamped to the row's width below.
        // When the screen runs taller than the cell (a frame where the two have not yet
        // agreed), show the bottom of the screen, not the top: a place is always read from
        // its live bottom — the newest line and the prompt, always the last line — and
        // painting the top would push that prompt below the cell and lose it.
        const int rows = std::min(cell.rows, screen.rows());
        const int top = screen.rows() - rows;
        for (int r = 0; r < rows; ++r) {
            const Row& line = rowToShow(screen, scroll, r + top);
            const int line_cols = std::min(cell.cols, static_cast<int>(line.size()));
            for (int c = 0; c < line_cols; ++c)
                paintCell(p, fm, cw, ch, grid_pad + (cell.col + c) * cw,
                          grid_top + (cell.row + r) * ch, line[c],
                          selected(cell.row + r, cell.col + c), default_bg);
        }
    }

    // Draw one cell of one screen at the pixel where it sits. This is the whole
    // of what "paint this grid" means for a single character, and it is one
    // method rather than a loop body because a window that shows a session side
    // by side paints two grids and they must be the *same* painting: two copies
    // of these rules is two chances for the second place to gain an underline the
    // first lost, or for a selected band to be legible in one half and not the
    // other. `selected` is passed in rather than asked of a selection, because
    // which cells are highlighted is the caller's business — the screen being
    // looked at has a selection, a neighbour shown beside it does not.
    void paintCell(QPainter& p, const QFontMetricsF& fm, double cw, double ch, double x,
                   double y, const Cell& cell, bool selected, const Color& default_bg) const {
        // Selection is drawn as a solid band with light text so it stays legible
        // whatever colours the application had chosen for those cells.
        const Color selection_bg = theme_.selection;
        const Color selection_fg = theme_.selection_foreground;

        // Blank cells are skipped below, so the highlight for them has
        // to be painted here or a dragged band would show gaps.
        if (selected && (cell.text.empty() || cell.width <= 0)) {
            p.fillRect(QRectF(x, y, cw, ch), ground(selection_bg));
        }

        if (cell.text.empty() || cell.width <= 0)
            return;
        if (cell.attr.hidden)
            return;

        Color fg = cellColour(cell.fg, cell.fg_role, true);
        Color bg = cellColour(cell.bg, cell.bg_role, false);
        if (cell.attr.reverse)
            std::swap(fg, bg);
        if (selected) {
            bg = selection_bg;
            fg = selection_fg;
        }

        if (bg.r != default_bg.r || bg.g != default_bg.g || bg.b != default_bg.b)
            p.fillRect(QRectF(x, y, cw * cell.width, ch), ground(bg));

        QFont f = font_;
        if (cell.attr.bold)
            f.setBold(true);
        if (cell.attr.italic)
            f.setItalic(true);
        p.setFont(f);
        p.setPen(ink(fg, cell.attr.dim ? 180 : 255));

        QString text = QString::fromUtf8(cell.text.c_str(), cell.text.size());
        p.drawText(QPointF(x, y + fm.ascent()), text);

        // Underline, strike and overline are independent attributes, so
        // each draws its own line. Sharing one variable for the position
        // made the last of them win, which is how a struck-out word with an
        // underline on lost its underline.
        if (cell.attr.underline || cell.attr.strike || cell.attr.overline) {
            p.setPen(QPen(ink(fg), 1.0));
            const double right = x + cw * cell.width;
            if (cell.attr.underline)
                p.drawLine(QPointF(x, y + ch - 1.5), QPointF(right, y + ch - 1.5));
            if (cell.attr.strike)
                p.drawLine(QPointF(x, y + ch / 2.0), QPointF(right, y + ch / 2.0));
            if (cell.attr.overline)
                p.drawLine(QPointF(x, y + 0.5), QPointF(right, y + 0.5));
        }
    }

private:
    // -----------------------------------------------------------------------
    // The dialog itself: state, keys, pointer, geometry and paint.
    //
    // The shape is deliberately small — a few lines of text and a set of
    // choices — because everything richer wants layout machinery, and layout
    // machinery is how a terminal ends up looking like a form letter. Two
    // shapes cover what a terminal actually asks: a question with a row of
    // buttons, and a list you walk.
    // -----------------------------------------------------------------------
    struct Overlay {
        bool active = false;
        std::vector<std::string> lines;   // the question, or a title and its detail
        std::vector<std::string> choices; // the buttons, or the rows to choose between
        bool vertical = false;            // a list walked with Up/Down, not a button row
        int selected = 0;
        std::string hint;                 // the keys, said once, under the list
        std::vector<char> hotkeys;        // letters that answer, checked before movement
        std::function<void(int)> answer;  // the index picked; -1 means it went away
        // A panel can also carry one line you type into. The letters then belong to that
        // line and not to the buttons under it, Enter hands the words over through
        // `gave_text`, and Esc still throws the whole thing away with `answer(-1)`.
        bool editable = false;
        std::string field;
        // True while the field still holds the name the window offered rather than anything
        // typed. The first letter typed replaces it outright — the offer is a default to
        // accept with Enter, not a word to delete a character at a time.
        bool field_offered = false;
        std::function<void(std::string)> gave_text;
    };

    // Where the panel and each choice landed, computed the same way for the
    // painter and for the pointer, so a click hits what it is sitting on.
    struct OverlayLayout {
        QRectF panel;
        std::vector<QString> lines;   // wrapped, as they are painted
        int question_lines = 1;       // how many of those belong to the first source line
        std::vector<QString> choices; // one per entry of Overlay::choices
        std::vector<QRectF> boxes;    // one per choice, in the same order
        QRectF hint;
    };

    // Break a paragraph into lines that fit `max_width`, on spaces. The
    // detail line of a question is long enough that it has to wrap, and a
    // dialog whose text runs off its own panel is a dialog you cannot read.
    static std::vector<QString> wrapLine(const QString& text, double max_width,
                                         const QFontMetricsF& fm) {
        std::vector<QString> out;
        QString current;
        const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& word : words) {
            const QString trial =
                current.isEmpty() ? word : current + QLatin1Char(' ') + word;
            if (!current.isEmpty() && fm.horizontalAdvance(trial) > max_width) {
                out.push_back(current);
                current = word;
            } else {
                current = trial;
            }
        }
        if (!current.isEmpty() || out.empty())
            out.push_back(current);
        return out;
    }

    OverlayLayout overlayLayout() const {
        OverlayLayout lay;
        QFontMetricsF fm(font_);
        const double cw = cellWidth();
        const double ch = cellHeight();
        const double pad = 1.5 * ch;
        // A dialog nearly as wide as the window is a wall; one barely wider
        // than its text cannot hold a wrapped line. Somewhere near five
        // sixths of the width, and never wider than the text needs.
        const double inner_max = std::max(240.0, width() * 0.8) - 2 * pad;

        double inner_w = 0.0;
        for (std::size_t i = 0; i < overlay_.lines.size(); ++i) {
            const std::vector<QString> wrapped =
                wrapLine(QString::fromStdString(overlay_.lines[i]), inner_max, fm);
            if (i == 0)
                lay.question_lines = static_cast<int>(wrapped.size());
            for (const QString& piece : wrapped) {
                inner_w = std::max(inner_w, fm.horizontalAdvance(piece));
                lay.lines.push_back(piece);
            }
        }
        // A typed field is a line of its own, straight under the question and painted as
        // loudly as the question: it is what you are looking at while you type. It is not a
        // choice, so it is not clickable and no letter of it is a hotkey.
        if (overlay_.editable) {
            const QString typed = QString::fromStdString(overlay_.field);
            inner_w = std::max(inner_w, fm.horizontalAdvance(typed) + 4 * cw);
            lay.lines.insert(lay.lines.begin() + lay.question_lines,
                             typed.isEmpty() ? QStringLiteral(" ") : typed);
            ++lay.question_lines; // full strength, like the question above it
        }
        for (const std::string& choice : overlay_.choices) {
            const QString label = QString::fromStdString(choice);
            inner_w = std::max(inner_w, fm.horizontalAdvance(label) + 2 * cw);
            lay.choices.push_back(label);
        }
        if (!overlay_.hint.empty())
            inner_w = std::max(inner_w, fm.horizontalAdvance(QString::fromStdString(overlay_.hint)));

        const double lines_h = lay.lines.size() * ch;
        const double gap = 0.9 * ch; // between the text and the choices
        const double box_h = overlay_.vertical ? ch + 6.0 : ch + 8.0;
        const double row_gap = 2.0 * cw; // between buttons, horizontally
        const int n = static_cast<int>(lay.choices.size());
        double choices_w = 0.0;
        if (n > 0) {
            if (overlay_.vertical) {
                choices_w = inner_w; // rows are full-width bands, not per-label boxes
            } else {
                for (const QString& label : lay.choices)
                    choices_w += fm.horizontalAdvance(label) + 2 * cw;
                choices_w += (n - 1) * row_gap;
            }
        }
        const double choices_h = n > 0 ? n * box_h + (n - 1) * (overlay_.vertical ? 3.0 : 0.0) : 0.0;
        const double hint_h = overlay_.hint.empty() ? 0.0 : 0.5 * ch + ch;

        const double panel_w = std::max({inner_w, choices_w}) + 2 * pad;
        const double panel_h = pad + lines_h + (n > 0 || hint_h > 0 ? gap : 0.0) + choices_h +
                               hint_h + pad;
        lay.panel = QRectF((width() - panel_w) / 2.0, (height() - panel_h) / 2.0, panel_w,
                           panel_h);

        double y = lay.panel.y() + pad + lines_h + (n > 0 || hint_h > 0 ? gap : 0.0);
        if (n > 0 && !overlay_.vertical) {
            double x = lay.panel.center().x() - choices_w / 2.0;
            for (std::size_t i = 0; i < lay.choices.size(); ++i) {
                const double w = fm.horizontalAdvance(lay.choices[i]) + 2 * cw;
                lay.boxes.push_back(QRectF(x, y, w, box_h));
                x += w + row_gap;
            }
        } else {
            for (int i = 0; i < n; ++i) {
                lay.boxes.push_back(QRectF(lay.panel.x() + pad, y, inner_w, box_h));
                y += box_h + 3.0;
            }
        }
        if (!overlay_.hint.empty())
            lay.hint = QRectF(lay.panel.x(), lay.panel.bottom() - pad - ch, panel_w, ch);
        return lay;
    }

    // Hand the answer over and put the dialog away before the callback runs:
    // the callback may close the window or switch the session, and either
    // way it should not find a live overlay pointing back at itself.
    void finishOverlay(int which) {
        const std::function<void(int)> answer = std::move(overlay_.answer);
        overlay_ = Overlay{};
        update();
        if (answer)
            answer(which);
    }

    // -----------------------------------------------------------------------
    // The theme picker: keys, the live preview, and leaving.
    //
    // The model decides everything — which setting is highlighted, which
    // digit the next keystroke lands on, what the colours now are. The widget
    // maps key codes onto it, repaints the whole window through `applyTheme`
    // after each change so the preview is the real thing, and on the way out
    // either keeps the previewed theme (save) or puts back the one that was up
    // when the picker opened (cancel). Which colours actually get written to
    // the file is the caller's decision, made from the fields handed back.
    // -----------------------------------------------------------------------

    // Fold the picker's five fields over a base theme. The keys are the
    // config-file names, so this switch is the one place the picker's rows and
    // the theme's members are tied together.
    static Theme themeWithFields(const Theme& base, const std::vector<ThemeField>& fields) {
        Theme theme = base;
        for (const ThemeField& field : fields) {
            const Color colour{static_cast<std::uint8_t>(field.value.r),
                               static_cast<std::uint8_t>(field.value.g),
                               static_cast<std::uint8_t>(field.value.b)};
            if (field.key == "foreground")
                theme.foreground = colour;
            else if (field.key == "background")
                theme.background = colour;
            else if (field.key == "cursor")
                theme.cursor = colour;
            else if (field.key == "selection_background" || field.key == "selection")
                theme.selection = colour;
            else if (field.key == "selection_foreground")
                theme.selection_foreground = colour;
            else if (field.key == "accent")
                theme.accent = colour;
            else if (field.key.rfind("color", 0) == 0 && field.key.size() > 5) {
                int index = -1;
                if (numberFromText(field.key.substr(5), 0, 15, index))
                    theme.palette[static_cast<std::size_t>(index)] = colour;
            }
        }
        return theme;
    }

    void previewPicker() {
        if (!picker_)
            return;
        const Theme preview = themeWithFields(picker_before_, picker_->fields());
        applyTheme(preview, owns_screen_);
        // The preview has to move the LOCK as well as the defaults. A colour the
        // config locked would ignore `setDefaults` and the preview would be frozen
        // on the saved value, so what you watched choosing would never change.
        // Locking the whole previewed theme is the honest "this is what you would
        // get"; a cancel hands the saved locks straight back, and a save lets the
        // caller re-derive them from the file it just wrote, down to only the
        // colours that were actually named.
        std::array<std::optional<Color>, 16> palette;
        for (std::size_t i = 0; i < palette.size(); ++i)
            palette[i] = preview.palette[i];
        screen_.lockDefaults(preview.foreground, preview.background);
        screen_.lockPalette(palette);
        update();
    }

    void finishPicker(bool saved) {
        if (!picker_)
            return;
        // A save hands back every colour as the panel now has it, and the name it should
        // be filed under. The caller works out which lines that is worth rewriting —
        // which is one list rather than two, and is also what makes a save under a new
        // name work: a brand new file wants every colour, not just the ones that moved.
        // A cancel hands back nothing; the old colours are already being put back.
        const std::vector<ThemeField> result =
            saved ? picker_->fields() : std::vector<ThemeField>{};
        const std::string name = themeFileName(picker_name_.toStdString(), picker_loaded_name_);
        const std::function<void(std::vector<ThemeField>, bool, std::string)> finish =
            std::move(picker_finish_);
        if (!saved) {
            applyTheme(picker_before_, owns_screen_); // the preview was a maybe
            screen_.setColourLocks(picker_saved_locks_); // and so were the locks
        }
        picker_.reset();
        picker_finish_ = nullptr;
        picker_overwrite_ = false;
        update();
        if (finish)
            finish(result, saved, name);
    }

    // Down off the last colour comes the name, then the two words, then the top of the
    // list again — the order they are painted in, so walking is looking.
    void pickerWalk(int direction) {
        // Walking off the name leaves it as it stands: the arrows are for moving between
        // the panel's stops, and a field that silently stays open would steal the next
        // pair of arrows from wherever the keys went.
        picker_editing_ = false;
        picker_caret_ = 0;
        const int rows = static_cast<int>(picker_->fields().size());
        int pos = picker_focus_ == 0   ? picker_->row()
                  : picker_focus_ == 3 ? rows
                  : picker_focus_ == 1 ? rows + 1
                                       : rows + 2;
        const int stops = rows + 3;
        pos = (pos + direction + stops) % stops;
        if (pos < rows) {
            picker_focus_ = 0;
            picker_->setRow(pos);
            return;
        }
        picker_focus_ = pos == rows ? 3 : (pos == rows + 1 ? 1 : 2);
    }

    // Saving, from either hand. Writing over a theme that is already there is the one
    // thing in this panel that loses work nobody meant to lose, so it is asked about
    // first; a name that is not taken writes without a word. The keyboard and the
    // pointer ask the same question because they mean the same thing.
    // (Kaida, 2026-11-20: "saving under the loaded name asks to overwrite, a new name
    // writes a new theme".)
    void requestPickerSave() {
        const bool taken =
            picker_name_taken_ && picker_name_taken_(pickerTargetName().toStdString());
        if (taken && !picker_overwrite_) {
            picker_overwrite_ = true;
            picker_focus_ = 1;
            update();
            return;
        }
        picker_->confirm();
        finishPicker(true);
    }

    // Every theme in the list is a `.conf` file, so the field does not spend four
    // characters of a narrow panel saying what is already true of all of them. A name
    // that carries some other extension keeps it — `odd.theme` is that file.
    static QString pickerDisplayName(const std::string& name) {
        const QString text = QString::fromStdString(name);
        return text.endsWith(QStringLiteral(".conf"))
                   ? text.left(text.size() - 5) // the extension is implied
                   : text;
    }

    // The name as it will be written: what is in the field, with the extension the other
    // themes have. The question the panel asks spells it this way too, so the name you
    // are asked about is the name you get.
    QString pickerTargetName() const {
        return QString::fromStdString(
            themeFileName(picker_name_.toStdString(), picker_loaded_name_));
    }

    // The title the panel wears. Normally the word that says what the panel is; while a
    // theme is already sitting at that name, the question of whether to write over it.
    QString pickerTitle() const {
        if (picker_overwrite_)
            return QStringLiteral("Overwrite ") + pickerTargetName() + QStringLiteral("?");
        // "Theme Editor", because that is what it is: the row of the settings menu that
        // opens it is called `theme`, and a window called Colours that is reached from a
        // row called theme makes you hold two names for one thing.
        // (Kaida, 2026-11-21: 'the title "Colours" to be a bit more up and also changed for
        // "Theme Editor"'.)
        return QStringLiteral("Theme Editor");
    }

    // One ring, not two modes: walking down past the last row arrives at `save`,
    // past that at `cancel`, and past that back at the first row — the same
    // gesture that walks the rows, carrying on. (Kaida, 2026-11-20: "no tab to
    // reach save and leave, they should be reachable when going down. the
    // interfaces must be Cohesive.") A chord of its own for the two words would
    // have been a second rule to remember for the motion you are already doing.

    // Walking the ring, which is one line of stops rather than two: the rows,
    // then `save`, then `cancel`, then the first row again. The row is moved here
    // instead of being stepped towards by the model, because the two disagree at
    // the join. Walking down off the last row leaves the model where it was and
    // puts the keys on `save`; stepping up from there then moved the model one
    // *more* time and landed on the second-to-last row, skipping the last one on
    // the way back. (Kaida, 2026-11-20: "when going back up from a save button
    // highlighted it jump to the second to last option instead of the last option".)
    static void overlayWalk(int& focus, int& row, int rows, int direction) {
        int pos = focus == 0 ? row : (focus == 1 ? rows : rows + 1);
        const int stops = rows + 2;
        pos = (pos + direction + stops) % stops;
        if (pos >= rows) {
            focus = pos == rows ? 1 : 2;
            return;
        }
        focus = 0;
        row = pos;
    }

    // The mixer's ring has one stop the other panels do not: the preview above the
    // channels. The order is the order on the screen — the channels, the two words,
    // and the preview back at the top — so walking is looking, and coming down off
    // `cancel` lands on the sentence rather than on the first row, because that is
    // where the panel goes from the bottom edge.
    void mixerWalk(int direction) {
        const int rows = static_cast<int>(kMixChannelCount);
        int pos = mixer_focus_ == 0   ? mixer_->row()
                  : mixer_focus_ == 1 ? rows
                  : mixer_focus_ == 2 ? rows + 1
                                      : rows + 2;
        const int stops = rows + 3;
        pos = (pos + direction + stops) % stops;
        if (pos < rows) {
            mixer_focus_ = 0;
            mixer_->setRow(pos);
            return;
        }
        mixer_focus_ = pos == rows ? 1 : (pos == rows + 1 ? 2 : 3);
    }

    // Left and Right switch between the two words while the keys are on them,
    // instead of walking digits that are no longer on screen. Tab is deliberately
    // not here: Tab is Down, here as everywhere else, and reaching the words by
    // walking is the whole point.
    static bool overlayWordSwitch(int key, Qt::KeyboardModifiers mods, int focus) {
        if (focus != 1 && focus != 2)
            return false;
        const bool plain = mods == Qt::NoModifier || mods == Qt::KeypadModifier;
        return key == Qt::Key_Left || key == Qt::Key_Right ||
               (plain && (key == Qt::Key_H || key == Qt::Key_L));
    }

    void pickerKeyPress(int key, Qt::KeyboardModifiers mods, const QString& text) {
        const bool plain = mods == Qt::NoModifier || mods == Qt::KeypadModifier;
        if (picker_editing_ &&
            (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier)))) {
            // Escape inside the field backs out of the field: the word goes back to what
            // it was when `e` was pressed and the panel stays up. A key that cancels the
            // whole panel from inside a half-typed name throws away the colours somebody
            // just chose along with the letters they did not finish.
            picker_name_ = picker_before_edit_;
            picker_editing_ = false;
            picker_caret_ = 0;
            update();
            return;
        }
        if (picker_overwrite_) {
            // The panel is asking one question, and the answer is one of its two words.
            // Nothing else counts as a keystroke right now: typing a letter while the
            // screen is asking about a file is how a name gets mangled unseen, and Esc
            // backs out of the question rather than out of the whole panel, because the
            // colours somebody just chose should still be there behind it.
            if (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier))) {
                picker_overwrite_ = false;
                update();
                return;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                if (picker_focus_ == 1)
                    finishPicker(true); // overwrite it
                else
                    finishPicker(false); // keep the file, and the colours it holds
                return;
            }
            if (overlayWordSwitch(key, mods, picker_focus_) || key == Qt::Key_Up ||
                key == Qt::Key_Down || (plain && (key == Qt::Key_K || key == Qt::Key_J))) {
                picker_focus_ = picker_focus_ == 1 ? 2 : 1;
                update();
            }
            return;
        }
        if (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier))) {
            finishPicker(false);
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            // Enter opens the thing the keys are standing on. On a colour that is
            // the mixer; on the two words at the bottom it is save and cancel,
            // which are reached by walking — there is no shortcut for saving,
            // because a panel where Enter both saves and dives into a row is a
            // panel where you have to remember which mood Enter is in.
            // (Kaida, 2026-11-20: "The color picker should open when pressing
            // Enter on a color"; "to save we go down to the save button, no need
            // for a direct key, but we keep Esc for quick cancel".)
            if (picker_focus_ == 2) {
                finishPicker(false);
                return;
            }
            if (picker_focus_ == 1) {
                requestPickerSave();
                return;
            }
            if (picker_focus_ == 3) {
                if (picker_editing_) {
                    // Enter closes the field: the name stays exactly as it was typed, and
                    // the keys go back to being the panel's keys. Confirming is not
                    // saving — the word for that is below, and it is one step away.
                    picker_editing_ = false;
                    picker_caret_ = 0;
                    update();
                    return;
                }
                // Enter opens the thing the keys are standing on, and here that thing is a
                // field. It used to be `e`, which was one more key to know and a rule this
                // panel did not otherwise follow.
                // (Kaida, 2026-11-21: "the Enter key should allow the Editing of the Theme
                // name field, replacing and removing the e key for the edit of that
                // field".)
                picker_editing_ = true;
                picker_before_edit_ = picker_name_;
                picker_caret_ = static_cast<int>(picker_name_.size());
                update();
                return;
            }
            openMixerForRow(picker_->row());
            return;
        }
        if (overlayWordSwitch(key, mods, picker_focus_)) {
            picker_focus_ = picker_focus_ == 1 ? 2 : 1;
            update();
            return;
        }
        if (key == Qt::Key_Up || (plain && key == Qt::Key_K)) {
            pickerWalk(-1);
            update();
            return;
        }
        if (key == Qt::Key_Down || (plain && key == Qt::Key_J) || key == Qt::Key_Tab) {
            pickerWalk(1);
            update();
            return;
        }
        if (picker_focus_ == 3) {
            // The field is shut until Enter opens it, which is the price of a panel where
            // a stray letter cannot quietly rename the theme you were about to save. In
            // edit mode the arrows walk the caret and Enter confirms and gets out.
            // (Kaida, 2026-11-20: "in edit mode the arrow can control the cursor and enter
            // will confirm and get us out of edit mode"; 2026-11-21: Enter is what opens
            // it, not `e`.)
            if (!picker_editing_)
                return; // every other keystroke is the panel's, not the field's
            if (key == Qt::Key_Left || (plain && key == Qt::Key_H)) {
                picker_caret_ = std::max(0, picker_caret_ - 1);
                update();
                return;
            }
            if (key == Qt::Key_Right || (plain && key == Qt::Key_L)) {
                picker_caret_ = std::min(static_cast<int>(picker_name_.size()),
                                         picker_caret_ + 1);
                update();
                return;
            }
            if (key == Qt::Key_Backspace) {
                if (picker_caret_ > 0) {
                    picker_name_.remove(picker_caret_ - 1, 1);
                    --picker_caret_; // the word is one letter shorter where the caret was
                }
                update();
                return;
            }
            // Shift is not a reason to lose a letter: a capital arrives as the same key
            // with Shift on it, and a name field that swallows them is a field where
            // `MyTheme` comes out as `y heme`. Control and Alt are still refused — they
            // mean something else to this window.
            // (Kaida, 2026-11-21: "make the Field accept Capital letters as it does not
            // currently".)
            const bool typed = text.size() == 1 && (plain || mods == Qt::ShiftModifier);
            if (typed) {
                const QChar c = text.at(0);
                // Printable and no spaces: a theme is a file name, and a file name with a
                // space in it is a thing you have to quote in every other program.
                if (c.unicode() >= 0x21 && c.unicode() <= 0x7e && picker_name_.size() < 48)
                    picker_name_.insert(picker_caret_++, c);
                update();
            }
            return;
        }
        if (key == Qt::Key_Left || (plain && key == Qt::Key_H)) {
            picker_->digitStep(-1);
            update();
            return;
        }
        if (key == Qt::Key_Right || (plain && key == Qt::Key_L)) {
            picker_->digitStep(1);
            update();
            return;
        }
        // A hex digit is the whole point: it edits the focused colour and the
        // window changes under the keystroke. Anything that is not hex and was
        // not a navigation key above is simply not the picker's business, and
        // it is swallowed rather than sent to the shell — a dialog owns the
        // keyboard while it is up.
        if ((plain || mods == Qt::ShiftModifier) && text.size() == 1) {
            const char c = text.at(0).toLatin1();
            if (picker_->typeHex(c)) {
                picker_focus_ = 0; // you are editing a colour again, not a word
                previewPicker();
            }
            update();
        }
    }

    // The settings menu's keys. Walking is the colour picker's walking — arrows
    // or k/j for the lines, h/l along the value — with `+` and `-` added, because
    // a row of numbers is as often nudged as it is typed, and a choice row has no
    // digits to type into at all.

    // --- the colour mixer ------------------------------------------------------

    // Space on a colour row, or a click on its swatch: open the mixer for that one
    // colour. The picker stays behind it, because mixing is a detour into a colour
    // and the next thing you want is the list you came from, with the colour
    // changed in the row it belongs to.
    void openMixerForRow(int row) {
        if (!picker_ || row < 0 || row >= static_cast<int>(picker_->fields().size()))
            return;
        const Rgb start = picker_->fields()[static_cast<std::size_t>(row)].value;
        // The name goes with the colour: the panel is about one colour and the
        // title is where it says which.
        const ThemeField& field = picker_->fields()[static_cast<std::size_t>(row)];
        mixer_title_ = QString::fromStdString(field.label.empty() ? field.key : field.label);
        // Both the live preview and the result go through the picker's own field,
        // so the mixer needs nothing of its own to apply colours: the window is
        // already showing what the fields say, and saving is still the list's
        // business. Cancelling hands the original colour back the same way, which
        // is also why a mix and an unmix leave the file untouched.
        mixColour(
            start,
            [this, row](Rgb colour, bool) {
                if (picker_ && row < static_cast<int>(picker_->fields().size())) {
                    picker_->setRowColour(row, colour);
                    previewPicker();
                }
                update();
            },
            [this, row](Rgb colour) {
                if (picker_ && row < static_cast<int>(picker_->fields().size())) {
                    picker_->setRowColour(row, colour);
                    previewPicker();
                }
            });
    }

    void mixerKeyPress(int key, Qt::KeyboardModifiers mods, const QString& text) {
        const bool plain = mods == Qt::NoModifier || mods == Qt::KeypadModifier;
        if (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier))) {
            finishMixer(false);
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            // A channel has nothing to open, so Enter on one does nothing; saving
            // is the word at the bottom, reached by walking, as everywhere else.
            if (mixer_focus_ == 1)
                finishMixer(true);
            else if (mixer_focus_ == 2)
                finishMixer(false);
            return;
        }
        if (overlayWordSwitch(key, mods, mixer_focus_)) {
            mixer_focus_ = mixer_focus_ == 1 ? 2 : 1;
            update();
            return;
        }
        if (key == Qt::Key_Up || (plain && key == Qt::Key_K)) {
            mixerWalk(-1);
            update();
            return;
        }
        if (key == Qt::Key_Down || (plain && key == Qt::Key_J) || key == Qt::Key_Tab) {
            mixerWalk(1);
            update();
            return;
        }
        // Left and Right change the value, as they do on every other row of this
        // program: the row is already chosen and the question is what colour it
        // should be. That includes the `hex` row, where the pair of digits the
        // cursor is on steps by one — a row that can only be edited by typing is
        // a row the arrows do not reach. The cursor a typed number wants is
        // Shift+Left and Shift+Right, and the colour *list* keeps the plain
        // arrows for the cursor, because there the whole row is one string.
        // (Kaida, 2026-11-20: "everything else should be adjustable with the left
        // right arrows".)
        //
        // On the preview the value is the ground the sentence sits on, and there are
        // three of them, so the arrows walk round rather than step.
        // (Kaida, 2026-11-20: "left right when the cursor is on the preview zone
        // seems nice".)
        if (key == Qt::Key_Left || (plain && key == Qt::Key_H)) {
            if (mixer_focus_ == 3) {
                mixer_ground_ = previewGroundStep(mixer_ground_, -1);
                update();
                return;
            }
            if (mods.testFlag(Qt::ShiftModifier))
                mixer_->digitStep(-1);
            else if (mixer_->nudge(-1)) {
                mixer_focus_ = 0;
                previewMixer();
            }
            update();
            return;
        }
        if (key == Qt::Key_Right || (plain && key == Qt::Key_L)) {
            if (mixer_focus_ == 3) {
                mixer_ground_ = previewGroundStep(mixer_ground_, 1);
                update();
                return;
            }
            if (mods.testFlag(Qt::ShiftModifier))
                mixer_->digitStep(1);
            else if (mixer_->nudge(1)) {
                mixer_focus_ = 0;
                previewMixer();
            }
            update();
            return;
        }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus ||
            key == Qt::Key_Underscore) {
            const int direction = key == Qt::Key_Plus || key == Qt::Key_Equal ? 1 : -1;
            if (mixer_focus_ == 3) {
                mixer_ground_ = previewGroundStep(mixer_ground_, direction);
            } else if (mixer_->nudge(direction)) {
                mixer_focus_ = 0;
                previewMixer();
            }
            update();
            return;
        }
        // Typing a digit belongs to a channel row. With the keys on the sentence it
        // would change a number nobody is looking at, on the one part of this panel
        // whose whole job is to be looked at.
        if (plain && text.size() == 1 && mixer_focus_ != 3) {
            const char c = text.at(0).toLatin1();
            const bool typed = mixer_->channel() == MixChannel::Hex
                                   ? mixer_->typeHex(c)
                                   : mixer_->typeDigit(c);
            if (typed) {
                mixer_focus_ = 0;
                previewMixer();
            }
            update();
        }
    }

    void previewMixer() {
        if (!mixer_ || !mixer_preview_)
            return;
        mixer_preview_(mixer_->colour());
    }

    // Saved hands back the mixed colour, cancelled hands back the one the mixer
    // opened with. Either way the caller gets a colour rather than a flag, because
    // the row it came from needs a colour either way.
    void finishMixer(bool saved) {
        if (!mixer_)
            return;
        const Rgb colour = saved ? mixer_->colour() : mixer_->startingColour();
        auto finish = std::move(mixer_finish_);
        mixer_finish_ = nullptr;
        mixer_preview_ = nullptr;
        mixer_drag_ = -1;
        mixer_.reset();
        update();
        if (finish)
            finish(colour, saved);
    }

    void mixerPress(const QPointF& where) {
        if (!mixer_)
            return;
        const OverlayHit& hit = mixer_hit_;
        if (!hit.panel.isValid() || !hit.panel.contains(where))
            return;
        if (hit.save.isValid() && hit.save.contains(where)) {
            finishMixer(true);
            return;
        }
        if (hit.cancel.isValid() && hit.cancel.contains(where)) {
            finishMixer(false);
            return;
        }
        // The switch before the strip, because it sits inside it: a click on the word
        // chooses a ground, and a click anywhere else on the sentence moves the keys
        // there so the arrows have something to change.
        if (hit.ground_switch.isValid() && hit.ground_switch.contains(where)) {
            mixer_ground_ = previewGroundStep(mixer_ground_, 1);
            mixer_focus_ = 3;
            update();
            return;
        }
        if (hit.preview.isValid() && hit.preview.contains(where)) {
            mixer_focus_ = 3;
            update();
            return;
        }
        for (std::size_t i = 0; i < hit.bars.size(); ++i) {
            if (!hit.bars[i].contains(where))
                continue;
            mixer_focus_ = 0;
            mixer_->setRow(static_cast<int>(i));
            mixer_drag_ = static_cast<int>(i);
            mixerBarPress(static_cast<int>(i), where);
            return;
        }
        for (std::size_t i = 0; i < hit.rows.size(); ++i) {
            if (!hit.rows[i].contains(where))
                continue;
            mixer_focus_ = 0;
            mixer_->setRow(hit.row_index[i]);
            update();
            return;
        }
    }

    // A bar is pushed as a fraction of its own width, which is the same
    // calculation the paint used to fill it — so the value lands where the pointer
    // is, whatever size the window happens to be.
    void mixerBarPress(int channel_row, const QPointF& where) {
        if (!mixer_ || channel_row < 0 ||
            channel_row >= static_cast<int>(mixer_hit_.bars.size()))
            return;
        const QRectF& bar = mixer_hit_.bars[static_cast<std::size_t>(channel_row)];
        if (bar.width() <= 0.0)
            return;
        mixer_->setRow(channel_row);
        if (mixer_->setFromFraction((where.x() - bar.left()) / bar.width()))
            previewMixer();
        update();
    }

    void paintMixer(QPainter& p) const {
        if (!mixer_)
            return;
        const Color bg = screen_.defaultBackground();
        const Color fg = screen_.defaultForeground();
        const QString hint = QStringLiteral("Arrows to Navigate - Enter to Select - Escape to Cancel");

        const int label_w = 12;    // `saturation` is the longest name
        const int bar_cells = 20;  // a bar wide enough to push with a pointer
        const int value_cells = 4; // `359` and a space
        const int table_cells = label_w + bar_cells + value_cells + 1;
        // Title, two rows of preview, the six channels. The two words and the line of keys
        // under them are the panel's bottom and are sized by themselves.
        const double content_rows = 3 + 2 + kMixChannelCount;

        // The panel is measured in cells and then fitted to the window, so a
        // small window gets a small panel rather than one whose edges, hint line
        // and two words are painted off the screen.
        const double gap = kPanelRowGap;
        const PanelFont pf = panelFont(table_cells, content_rows + 3.0, gap * (content_rows + 3.0));
        const double cw = pf.cw;
        const double ch = pf.ch;
        const QFontMetricsF& fm = pf.fm;
        const double pad = kPanelPadCells * ch;
        const double table_w = table_cells * cw;
        // As wide as its table, which is the widest thing in it. This used to be as wide as
        // the sentence at the bottom of the panel, and a sentence is wider than any table:
        // the hint, not the colours, is what was making every menu large.
        // (Kaida, 2026-11-20: "the tooltips are the culprit, they take too much space and
        // make the windows larger, tooltips should have a reduced size".)
        const double panel_w = std::min(table_w + 2 * pad, std::max(2.0, width() - 2.0));
        const PanelBottom bottom = panelBottom({hint}, panel_w - pad, pf);
        const double panel_h = std::min(content_rows * (ch + gap) + kPanelBottomAir * ch +
                                            bottom.height + 2 * pad,
                                        std::max(2.0, height() - 2.0));
        const QRectF panel((width() - panel_w) / 2.0, (height() - panel_h) / 2.0, panel_w,
                           panel_h);
        const QColor ground = panelGroundColour(bg);

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(panel_ink(fg, 80), 1));
        p.setBrush(ground);
        p.drawRoundedRect(panel, panelRadius(ch), panelRadius(ch));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(pf.font);

        double y = panel.y() + pad + fm.ascent();
        p.setPen(panel_ink(fg));
        // The heading is raised out of the panel's padding a little: it is a title rather
        // than a row, and at one padding from the edge it sat as deep as the first line of
        // the thing it is heading. The rows stay where they are, so what the rise buys is
        // air between a panel's name and its contents.
        // (Kaida, 2026-11-21: "we would need the title "Colours" to be a bit more up".)
        const QRectF title_rect(panel.x(), y - fm.ascent() - kPanelTitleRise * ch,
                                panel.width(), fm.height());
        p.drawText(title_rect, Qt::AlignHCenter | Qt::AlignVCenter, mixerTitle());
        title_hit_ = title_rect;
        y += ch + kTitleGapRows * ch;

        // Everything the mixer paints is centred in the panel: the panel is as
        // wide as the widest thing in it, and a table hung off the left padding
        // leaves all the rest of the width as dead space on the right.
        // (Kaida, 2026-11-20: "text should be centenred in the interface, lots of
        // empty space on the right as it is now".)
        const double table_x = panel.x() + (panel_w - table_w) / 2.0;
        const Rgb mixed = mixer_->colour();

        // The preview is the sentence, and the sentence is the colour. Flooding a
        // strip with the mixed colour and writing the sample over it in black or
        // white gave a wall of the colour with a caption on it, and painted the
        // menu the same colour twice over; what is being chosen is a colour to
        // read *through*, so it goes on the letters and nowhere else. (Kaida,
        // 2026-11-20: "the Quick brown fox line didn't get paint in red when it
        // should be the only thing getting painted red on this menu, since it's
        // the preview".)
        //
        // What it sits on is a choice rather than a guess, and the switch at the end
        // of the strip walks three of them: the window's own background as the window
        // shows it, black, white. The colour being mixed is generally the colour of
        // the row it was opened from, so the ground worth defaulting to is the one the
        // mix will actually be read on — and when that ground turns out to be the very
        // colour being mixed, the two flat ones are a keystroke away rather than a
        // mystery.
        const int chip_cells = 11;  // `background`, and a space to breathe in
        const QRectF preview(panel.x() + pad, y - ch + fm.ascent() * 0.15,
                             panel_w - 2 * pad, 2 * ch);
        const QRectF chip(preview.right() - (chip_cells + 1) * cw,
                          preview.center().y() - ch * 0.75, chip_cells * cw, ch * 1.5);
        const QColor strip =
            previewGroundColour(mixer_ground_, bg, ground, background_opacity_);
        const QColor accent_ink(theme_.accent.r, theme_.accent.g, theme_.accent.b);
        const bool preview_focused = mixer_focus_ == 3;

        mixer_hit_ = OverlayHit{};
        mixer_hit_.panel = panel;
        mixer_hit_.preview = preview;
        mixer_hit_.ground_switch = chip;
        mixer_hit_.table_x = table_x;
        mixer_hit_.label_w = label_w;
        mixer_hit_.cw = cw;

        p.setRenderHint(QPainter::Antialiasing, true);
        if (preview_focused) {
            // The keys are standing on the preview, so the preview is lit — the same
            // band a row gets, and the only thing on the panel wearing it.
            paintFocusBand(p, preview.adjusted(-pad / 2, -3.0, pad / 2, 3.0));
            mixer_hit_.highlight = preview;
        }
        p.setPen(QPen(preview_focused ? accent_ink : panel_ink(fg, 70), 1));
        p.setBrush(strip);
        p.drawRoundedRect(preview, kMixerBarRadius, kMixerBarRadius);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QColor(mixed.r, mixed.g, mixed.b));
        const QRectF sample_box = preview.adjusted(cw, 0, -(chip_cells + 2) * cw, 0);
        // The sentence says what it is. It used to be a pangram, which is long enough
        // that the centred draw clipped both ends of it and the strip read as a
        // sentence with pieces missing — and a caption in a colour swatch has one job,
        // which is to be the colour, so it does not need to be a typing test too.
        // (Kaida, 2026-11-21: 'the preview pane in the Theme editor should read
        // "  0123 #$%& Color Preview" instead of " 0123 #$%& The quick brown fox ...".)
        p.drawText(sample_box, Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine,
                   kMixerSample);
        mixer_sample_fits_ = fm.horizontalAdvance(kMixerSample) <= sample_box.width();
        // The switch: labelled with the ground it is on, and painted as that ground, so
        // the word and the colour say the same thing and a border is all that keeps it
        // an object of its own when the two agree. Its ink follows its own ground, the
        // one place in this program where black-on-white is decided by the swatch
        // rather than by the theme — the swatch is what is being shown.
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(preview_focused ? accent_ink : panel_ink(fg, 120), preview_focused ? 2 : 1));
        p.setBrush(strip);
        p.drawRoundedRect(chip, kMixerBarRadius, kMixerBarRadius);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(inkLuminance(strip.red(), strip.green(), strip.blue()) > 0.55
                     ? QColor(0, 0, 0)
                     : QColor(255, 255, 255));
        p.drawText(chip, Qt::AlignCenter | Qt::TextSingleLine, previewGroundName(mixer_ground_));
        // More than one row's worth of air under the preview: it is the answer to
        // the rows below it, and rows crowded under it read as its labels.
        // (Kaida, 2026-11-20: "there should be a bit more room between the preview
        // pane and other elements".)
        y += 2 * ch + 2 * gap;

        for (int i = 0; i < kMixChannelCount; ++i) {
            const MixChannel c = mixChannelAt(i);
            const bool focused = mixer_focus_ == 0 && i == mixer_->row();
            const QRectF row(panel.x() + pad / 2, y - ch + fm.ascent() * 0.15,
                             panel_w - pad, ch);
            mixer_hit_.rows.push_back(row);
            mixer_hit_.row_index.push_back(i);
            // Nothing is lit along the whole line in this panel. What the keys move
            // here is the band — the same strip of colour a pointer drags left and
            // right — so the band wears the highlight and the number at the end of
            // the row stays its caption. (Kaida, 2026-11-20: "the colour mixer only
            // we highlight the color band (the same one we move left to right to edit
            // the value)".)
            p.setPen(panel_ink(fg));
            p.drawText(QPointF(table_x, y),
                       QString::fromUtf8(channelKey(c)).leftJustified(label_w, QLatin1Char(' ')));

            const QRectF bar(table_x + label_w * cw, row.top() + 2,
                             bar_cells * cw, ch - 4);
            mixer_hit_.bars.push_back(bar);
            if (c == MixChannel::Hex) {
                // No bar along a string. The six digits are the row, with the
                // cursor under the pair the next keystroke changes. Where there is
                // no band to light, the value is what gets it, as everywhere else —
                // and lit to its own length, not to the length a bar would have had.
                const QString hex_text =
                    QStringLiteral("#") + QString::fromStdString(mixer_->text(c));
                const QRectF hex_box(bar.left() - cw * 0.4, row.top() + 1.0,
                                     (fm.horizontalAdvance(hex_text) / cw + 0.8) * cw,
                                     ch - 2.0);
                if (focused) {
                    paintFocusBand(p, hex_box);
                    mixer_hit_.highlight = hex_box;
                }
                p.setPen(focused ? band_ink() : panel_ink(fg));
                p.drawText(QPointF(bar.left(), y), hex_text);
                if (focused && mixer_focus_ == 0) {
                    const QColor accent(theme_.accent.r, theme_.accent.g, theme_.accent.b);
                    p.setPen(QPen(accent, 1));
                    const double dx = bar.left() + (1 + mixer_->digit() * 0.5) * cw;
                    p.drawLine(QPointF(dx, y + fm.descent() * 0.4),
                               QPointF(dx + cw * 0.5 - 1, y + fm.descent() * 0.4));
                }
                y += ch + gap;
                continue;
            }

            // The bar is the range you are pushing, painted as what pushing it
            // does: the left end is this channel at zero and the right end at
            // full, the other channels held where they are. A plain filled
            // proportion would show how full a bar is rather than what the colour
            // becomes, and the colour is the thing being chosen.
            // The bar is the range you are pushing, painted as what pushing it
            // does: the left end is this channel at nothing, the right end at
            // everything, the others held where they are. Hue is the whole wheel
            // laid flat rather than a slide from the current colour back to red;
            // lightness runs black through the colour to white rather than black to
            // white. Either one without its middle stops is a ramp lying about where
            // the colour goes.
            const auto paint_at = [&](double f) {
                const Rgb e = channelAt(*mixer_, c, f);
                return QColor::fromRgb(e.r, e.g, e.b);
            };
            QLinearGradient ramp(bar.left(), 0.0, bar.right(), 0.0);
            if (c == MixChannel::Hue) {
                for (int k = 0; k <= 6; ++k)
                    ramp.setColorAt(k / 6.0, paint_at(k / 6.0));
            } else if (c == MixChannel::Lightness) {
                ramp.setColorAt(0.0, paint_at(0.0));
                ramp.setColorAt(0.5, paint_at(0.5));
                ramp.setColorAt(1.0, paint_at(1.0));
            } else {
                ramp.setColorAt(0.0, paint_at(0.0));
                ramp.setColorAt(1.0, paint_at(1.0));
            }
            if (focused) {
                // Behind the ramp, so lighting the band does not paint over one of
                // its colours: here the highlight is a frame, and the frame is still
                // the band.
                paintFocusBand(p, bar.adjusted(-3.0, -2.5, 3.0, 2.5));
                mixer_hit_.highlight = bar;
            }
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(ramp));
            p.drawRoundedRect(bar, kMixerBarRadius, kMixerBarRadius);
            p.setPen(QPen(panel_ink(fg, focused ? 160 : 80), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(bar, kMixerBarRadius, kMixerBarRadius);

            // Where the value sits along that range, drawn as something you could get
            // a finger on: a knob the height of the bar, the panel's own ground under
            // a light edge, so it reads at the dark end of a ramp and the bright one.
            const double mark = bar.left() + channelFraction(c, mixer_->value(c)) * bar.width();
            const double knob_x = qBound(bar.left() + kMixerBarRadius, mark,
                                         bar.right() - kMixerBarRadius);
            const QRectF knob(knob_x - kMixerKnobWidth / 2.0, bar.top() - 1.5,
                              kMixerKnobWidth, bar.height() + 3.0);
            p.setPen(QPen(panel_ink(fg, 230), 1));
            p.setBrush(panel_ground(theme_.background, 235));
            p.drawRoundedRect(knob, 2.0, 2.0);
            p.setRenderHint(QPainter::Antialiasing, false);

            p.setPen(panel_ink(fg));
            p.drawText(QRectF(bar.right() + cw, y - ch + fm.ascent() * 0.15,
                              value_cells * cw, ch),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::fromStdString(mixer_->text(c)));
            if (focused && mixer_focus_ == 0) {
                const QColor accent(theme_.accent.r, theme_.accent.g, theme_.accent.b);
                p.setPen(QPen(accent, 1));
                const int digits = channelDigits(c);
                const double digits_left = bar.right() + cw + (value_cells - digits) * cw;
                const double dx = digits_left + mixer_->digit() * cw;
                p.drawLine(QPointF(dx, y + fm.descent() * 0.4),
                           QPointF(dx + cw - 1, y + fm.descent() * 0.4));
            }
            y += ch + gap;
        }

        paintOverlayButtons(p, panel, pf, fg, mixer_hit_.save, mixer_hit_.cancel, mixer_focus_,
                            panelWordsY(panel, pad, bottom));
        noteWordHit(mixer_hit_, mixer_focus_);
        p.setPen(panel_ink(fg, 140));
        paintPanelHint(p, bottom.hint, pf, panel, pad, bottom.reserved, panel_ink(fg, 140));
    }

    // The colour of a bar at one point along it: this channel set to `fraction` of
    // its own range, everything else left exactly where the mixer holds it — at the
    // precise wheel, not the rounded numbers the rows display, or the ramp would be
    // painted from a colour that is not the one on the screen.
    static Rgb channelAt(const ColorMixer& mixer, MixChannel c, double fraction) {
        if (fraction < 0.0)
            fraction = 0.0;
        if (fraction > 1.0)
            fraction = 1.0;
        const Rgb colour = mixer.colour();
        const Hsl10 wheel = mixer.hsl10();
        const auto tenth = [](double whole) {
            return static_cast<int>(whole * 1000.0 + 0.5);
        };
        switch (c) {
            case MixChannel::Red: return Rgb{static_cast<std::uint8_t>(fraction * 255.0 + 0.5), colour.g, colour.b};
            case MixChannel::Green: return Rgb{colour.r, static_cast<std::uint8_t>(fraction * 255.0 + 0.5), colour.b};
            case MixChannel::Blue: return Rgb{colour.r, colour.g, static_cast<std::uint8_t>(fraction * 255.0 + 0.5)};
            case MixChannel::Hue:
                return hsl10ToRgb(Hsl10{static_cast<int>(fraction * 3599.0 + 0.5), wheel.s, wheel.l});
            case MixChannel::Saturation:
                return hsl10ToRgb(Hsl10{wheel.h, tenth(fraction), wheel.l});
            case MixChannel::Lightness:
                return hsl10ToRgb(Hsl10{wheel.h, wheel.s, tenth(fraction)});
            case MixChannel::Hex: return colour;
        }
        return colour;
    }

    void settingsKeyPress(int key, Qt::KeyboardModifiers mods, const QString& text) {
        const bool plain = mods == Qt::NoModifier || mods == Qt::KeypadModifier;
        if (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier))) {
            finishSettings(false);
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            // As in the other panels: Enter answers the word the keys are on, and
            // a row has no word in it. Walking down past the last setting reaches
            // `save` and `cancel`; Esc still leaves from anywhere.
            if (settings_focus_ == 1) {
                settings_->confirm();
                finishSettings(true);
            } else if (settings_focus_ == 2) {
                finishSettings(false);
            } else if (settings_focus_ == 0 && settings_->current().opens && settings_open_) {
                // Enter on a row that opens a panel does exactly that, and the menu steps
                // aside rather than closing: what you had changed in it is still changed
                // when the panel closes, and the keys come back to this same row.
                // (Kaida, 2026-11-20: "the Theme editor should be an item in the setting
                // menu under Theme"; "the setting keeps your change until you save or
                // cancel them, even when in a sub-menu".)
                const SettingRow row = settings_->current();
                settings_suspended_ = true;
                settings_hit_ = OverlayHit{}; // nothing of ours is out there to click
                update();
                settings_open_(row);
            }
            return;
        }
        if (overlayWordSwitch(key, mods, settings_focus_)) {
            settings_focus_ = settings_focus_ == 1 ? 2 : 1;
            update();
            return;
        }
        // Left and Right change the value while the keys are on a row, which is
        // what they mean on a row of settings: the row is already chosen and the
        // question is what it should say. The colour menu is the one place they
        // mean something else — six digits to choose between — and it keeps that.
        // (Kaida, 2026-11-20: "Arrows should edit the value instead of moving the
        // cursor on everything but the colors".)
        if (settings_focus_ == 0 && (key == Qt::Key_Left || (plain && key == Qt::Key_H))) {
            if (settings_->cycle(-1))
                previewSetting(); // live, exactly as `-` and `+` are
            update();
            return;
        }
        if (settings_focus_ == 0 && (key == Qt::Key_Right || (plain && key == Qt::Key_L))) {
            if (settings_->cycle(1))
                previewSetting();
            update();
            return;
        }
        const int rows = static_cast<int>(settings_->rows().size());
        if (key == Qt::Key_Up || (plain && key == Qt::Key_K)) {
            int row = settings_->row();
            overlayWalk(settings_focus_, row, rows, -1);
            if (settings_focus_ == 0)
                settings_->setRow(row);
            update();
            return;
        }
        if (key == Qt::Key_Down || (plain && key == Qt::Key_J) || key == Qt::Key_Tab) {
            int row = settings_->row();
            overlayWalk(settings_focus_, row, rows, 1);
            if (settings_focus_ == 0)
                settings_->setRow(row);
            update();
            return;
        }
        if (key == Qt::Key_Left || (plain && key == Qt::Key_H)) {
            if (settings_->current().kind == SettingKind::Choice) {
                settings_->digitStep(-1);
                previewSetting();
            } else
                settings_->digitStep(-1);
            update();
            return;
        }
        if (key == Qt::Key_Right || (plain && key == Qt::Key_L)) {
            if (settings_->current().kind == SettingKind::Choice) {
                settings_->digitStep(1);
                previewSetting();
            } else
                settings_->digitStep(1);
            update();
            return;
        }
        if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus ||
            key == Qt::Key_Underscore) {
            const bool up = key == Qt::Key_Plus || key == Qt::Key_Equal;
            if (settings_->cycle(up ? 1 : -1)) {
                settings_focus_ = 0;
                previewSetting();
            }
            update();
            return;
        }
        // A digit edits a number; a letter picks an answer on a choice row. Both
        // clamp inside what the setting accepts, so there is no sequence of
        // keystrokes that lands a row on a value its own file would refuse.
        if (plain && text.size() == 1) {
            const char c = text.at(0).toLatin1();
            const bool typed = settings_->current().kind == SettingKind::Choice
                                   ? settings_->typeLetter(c)
                                   : settings_->typeDigit(c);
            if (typed) {
                settings_focus_ = 0;
                previewSetting();
            }
            update();
        }
    }

    // The preview of one row, handed to whoever owns the window: the widget can
    // change its own font, padding, scrollback and cursor, but the colours and
    // the file are the window's business.
    void previewSetting() {
        if (!settings_ || !settings_preview_)
            return;
        settings_preview_(settings_->current());
    }

    void finishSettings(bool saved) {
        if (!settings_)
            return;
        const std::vector<SettingRow> result =
            saved ? settings_->changes() : std::vector<SettingRow>{};
        const std::function<void(std::vector<SettingRow>, bool)> finish =
            std::move(settings_finish_);
        settings_.reset();
        settings_finish_ = nullptr;
        settings_preview_ = nullptr;
        settings_open_ = nullptr;
        settings_suspended_ = false;
        settings_hit_ = OverlayHit{}; // nothing is there to click any more
        update();
        if (finish)
            finish(result, saved);
    }

    void overlayKeyPress(int key, Qt::KeyboardModifiers mods, const QString& typed = QString()) {
        const int count = static_cast<int>(overlay_.choices.size());
        const bool plain = mods == Qt::NoModifier || mods == Qt::KeypadModifier;
        auto step = [this, count](int delta) {
            if (count == 0)
                return;
            overlay_.selected = (overlay_.selected + delta + count) % count;
            update();
        };

        if (key == Qt::Key_Escape || (key == Qt::Key_G && mods.testFlag(Qt::ControlModifier))) {
            finishOverlay(-1);
            return;
        }
        // A panel with a field: everything the keyboard can hand over as a character goes
        // into the field, and nothing else reaches the buttons underneath it — a `y` typed
        // into a name is a `y`, not an answer. Enter gives the name and Esc, handled above,
        // backs out. Backspace takes one letter off the end; the caret lives there.
        if (overlay_.editable) {
            if (key == Qt::Key_Backspace) {
                if (overlay_.field_offered) {
                    overlay_.field.clear(); // the offer, not a letter of it
                    overlay_.field_offered = false;
                } else if (!overlay_.field.empty()) {
                    overlay_.field.pop_back();
                }
                update();
                return;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                const std::string name = overlay_.field;
                std::function<void(std::string)> gave = overlay_.gave_text;
                overlay_ = Overlay{};
                update();
                if (gave)
                    gave(name);
                return;
            }
            // Shift is not a reason to lose a letter: a capital arrives as the same key
            // with Shift on it, and a field that refused them types `My-Session` as
            // `ySession`. Control and Alt still mean something else to this window.
            const bool writable = mods == Qt::NoModifier || mods == Qt::KeypadModifier ||
                                  mods == Qt::ShiftModifier ||
                                  mods == (Qt::ShiftModifier | Qt::KeypadModifier);
            if (writable && typed.size() == 1 && typed.at(0).isPrint() &&
                !typed.at(0).isSpace() && overlay_.field.size() < 48) {
                if (overlay_.field_offered) {
                    // The offered name was a default to accept, not a word to edit: the
                    // first letter typed stands in its place.
                    overlay_.field.clear();
                    overlay_.field_offered = false;
                }
                overlay_.field.push_back(typed.at(0).toLatin1());
                update();
                return;
            }
            update();
            return; // the panel owns the rest of the keyboard, and does nothing with it
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            finishOverlay(count > 0 ? overlay_.selected : -1);
            return;
        }
        if (key == Qt::Key_Tab) {
            step(1);
            return;
        }
        if (key == Qt::Key_Backtab) {
            step(-1);
            return;
        }

        // Named hotkeys answer ahead of everything else, movement included. A choice
        // spelled to collide with a walk key — "Horizontal" starting with the `h` that
        // steps left — would otherwise never be reachable by the very letter it shows.
        if (plain && key >= Qt::Key_A && key <= Qt::Key_Z) {
            for (std::size_t i = 0; i < overlay_.hotkeys.size(); ++i) {
                if (std::toupper(static_cast<unsigned char>(overlay_.hotkeys[i])) ==
                    static_cast<char>(key)) {
                    finishOverlay(static_cast<int>(i));
                    return;
                }
            }
        }

        if (overlay_.vertical) {
            if (key == Qt::Key_Up || (plain && key == Qt::Key_K)) {
                step(-1);
                return;
            }
            if (key == Qt::Key_Down || (plain && key == Qt::Key_J)) {
                step(1);
                return;
            }
            if (key == Qt::Key_Home) {
                overlay_.selected = 0;
                update();
                return;
            }
            if (key == Qt::Key_End && count > 0) {
                overlay_.selected = count - 1;
                update();
                return;
            }
            // A numbered list is picked by number, the way every picker list
            // has ever worked: one key instead of one key per row skipped.
            if (plain && key >= Qt::Key_1 && key <= Qt::Key_9) {
                const int which = key - Qt::Key_1;
                if (which < count)
                    finishOverlay(which);
                return;
            }
            return;
        }

        if (key == Qt::Key_Left || (plain && key == Qt::Key_H)) {
            step(-1);
            return;
        }
        if (key == Qt::Key_Right || (plain && key == Qt::Key_L)) {
            step(1);
            return;
        }
        // A letter whose first letter is an answer answers it, wherever the
        // focus happens to be: at a question with Yes and No in front of you,
        // Y and N are answers, and making somebody walk a button they can read
        // is a detour invented by toolkits. It runs after the movement keys
        // above so that a plain `l` still moves when no answer starts with it.
        if (plain && key >= Qt::Key_A && key <= Qt::Key_Z) {
            for (std::size_t i = 0; i < overlay_.choices.size(); ++i) {
                const std::string& label = overlay_.choices[i];
                if (!label.empty() &&
                    std::toupper(static_cast<unsigned char>(label[0])) ==
                        static_cast<char>(key)) {
                    finishOverlay(static_cast<int>(i));
                    return;
                }
            }
        }
    }

    void overlayPress(const QPointF& where) {
        const OverlayLayout lay = overlayLayout();
        for (std::size_t i = 0; i < lay.boxes.size(); ++i) {
            if (lay.boxes[i].contains(where)) {
                finishOverlay(static_cast<int>(i));
                return;
            }
        }
        if (lay.panel.contains(where))
            return; // a click inside the panel that hit no choice chooses nothing
        finishOverlay(-1); // a click outside it is the same word as Esc
    }

    // A press on the colour picker: the row under the pointer becomes the row you
    // are editing, a press in its value puts the cursor on the digit it landed on,
    // and the two words at the bottom do what they say. A press outside the panel
    // does nothing — a colour menu that closed because you clicked elsewhere would
    // have thrown away colours you were still deciding on, which is why the same
    // click on the question dialog means "no" and this one means nothing.
    void pickerPress(const QPointF& where) {
        if (!picker_)
            return;
        // The swatch is checked before the row it sits in, because the swatch is
        // inside the row's rect and the more specific thing has to answer first.
        if (pickerSwatchPress(where))
            return;
        const OverlayPress hit = overlayPressAt(picker_hit_, where);
        if (!hit.hit)
            return;
        if (hit.save) {
            requestPickerSave();
            return;
        }
        if (hit.leave) {
            finishPicker(false);
            return;
        }
        if (hit.name) {
            // A pointer landing on the name means the next thing typed is a name, so the
            // click opens the field the way `e` does. A box you can point at but not type
            // into is a label with a border round it.
            picker_focus_ = 3;
            if (!picker_editing_) {
                picker_editing_ = true;
                picker_before_edit_ = picker_name_;
                picker_caret_ = static_cast<int>(picker_name_.size());
            }
            update();
            return;
        }
        if (hit.row < 0)
            return;
        picker_focus_ = 0; // a click on a row takes the keys back to the rows
        picker_->setRow(hit.row);
        if (hit.digit >= 0)
            picker_->setDigit(hit.digit);
        update();
    }

    // A press on one of the picker's colour boxes: which row it belongs to is read
    // back from the geometry the paint recorded, not guessed from the y the click
    // happened to arrive at.
    bool pickerSwatchPress(const QPointF& where) {
        if (!picker_)
            return false;
        for (std::size_t i = 0; i < picker_hit_.swatches.size(); ++i) {
            if (!picker_hit_.swatches[i].contains(where))
                continue;
            const int index = i < picker_hit_.row_index.size()
                                  ? picker_hit_.row_index[i]
                                  : static_cast<int>(i);
            picker_->setRow(index);
            openMixerForRow(index);
            return true;
        }
        return false;
    }

    // A press on the settings menu answers with the same three moves the keyboard
    // makes: pick the row, put the cursor on a digit, or step the value. The step
    // arrows and a click on an answer both go through the model, so the mouse
    // cannot reach a value the keys could not.
    void settingsPress(const QPointF& where) {
        const OverlayPress hit = overlayPressAt(settings_hit_, where);
        if (!settings_ || !hit.hit)
            return;
        if (hit.save) {
            settings_->confirm();
            finishSettings(true);
            return;
        }
        if (hit.leave) {
            finishSettings(false);
            return;
        }
        if (hit.row < 0)
            return;
        settings_focus_ = 0; // as above: a click on a row means you are editing rows
        settings_->setRow(hit.row);
        if (hit.step_down || hit.step_up) {
            if (settings_->cycle(hit.step_up ? 1 : -1))
                previewSetting();
            update();
            return;
        }
        if (hit.digit >= 0) {
            // Clicking a whole answer moves to the next one; clicking a number
            // puts the cursor on the digit under the pointer.
            if (settings_->current().kind == SettingKind::Choice) {
                settings_->digitStep(1);
                previewSetting();
            } else {
                settings_->setDigit(hit.digit);
            }
        }
        update();
    }

    // Over a list, the pointer is a selector: where it rests is what Enter
    // would choose, the same rule every TUI menu follows.
    void overlayHover(const QPointF& where) {
        if (!overlay_.vertical)
            return;
        const OverlayLayout lay = overlayLayout();
        for (std::size_t i = 0; i < lay.boxes.size(); ++i) {
            if (!lay.boxes[i].contains(where))
                continue;
            if (overlay_.selected != static_cast<int>(i)) {
                overlay_.selected = static_cast<int>(i);
                update();
            }
            return;
        }
    }

    // The Ctrl+K guide. The key stands in the accent so the eye can find the letter it is
    // looking for; the words beside it are the panel's own voice, and both columns are as
    // wide as their widest entry, so the rows read as a table rather than as eight notes
    // that happen to start near each other.
    void paintKeyGuide(QPainter& p) const {
        struct Guide {
            const char* key;
            const char* what;
            // The one row that also says what it currently is, because toggling something
            // you cannot see the state of is a coin toss.
            // (Kaida, 2026-11-25: "T toggle view mode Current : Value".)
            bool reads_back = false;
        };
        // In the order they are reached for, not in the order the letters fall: the panes
        // and the sessions first, then the view and the resizing, then the settings, then
        // the way out. (Kaida, 2026-11-25: "we will also re-order it: N, S, A, F, T, R, K,
        // Esc".)
        static const Guide kGuide[] = {
            {"N", "new pane", false},
            {"S", "save session", false},
            {"A", "Attach a saved session", false},
            {"F", "open the file manager", false},
            {"T", "toggle view mode", true},
            {"R", "Resize a pane in side by side view.", false},
            {"K", "open settings", false},
            {"Esc", "leave this menu", false},
        };
        static const char kTitle[] = "Keybind Tooltips";
        const double ch = cellHeight();
        const QFontMetricsF fm(font_);
        // What the view is wearing at the moment the panel is drawn, read off the widget
        // rather than remembered, so the panel cannot disagree with the window.
        const QString reading_label = QStringLiteral("Current : ");
        const QString reading_value =
            view_tabs_ ? QStringLiteral("tabs") : QStringLiteral("side-by-side");
        const double reading_w =
            fm.horizontalAdvance(reading_label) + fm.horizontalAdvance(reading_value);
        double key_w = 0.0;
        double what_w = 0.0;
        const double between = 3.0 * fm.horizontalAdvance(QLatin1Char(' '));
        for (const Guide& entry : kGuide) {
            key_w = std::max(key_w, fm.horizontalAdvance(QString::fromUtf8(entry.key)));
            double width = fm.horizontalAdvance(QString::fromUtf8(entry.what));
            if (entry.reads_back)
                width += between + reading_w;
            what_w = std::max(what_w, width);
        }
        const double gap = 3.0 * fm.horizontalAdvance(QLatin1Char(' '));
        const QString title = QString::fromUtf8(kTitle);
        const double title_w = fm.horizontalAdvance(title);
        // A little more room at the sides than the rows strictly need, so no letter is
        // standing against the edge of the panel.
        // (Kaida, 2026-11-25: "add a bit of padding left and right (about 8px)".)
        const double side = 2.0 * padding_ + 8.0;
        const double box = std::max(key_w + gap + what_w, title_w) + 2.0 * side;
        const double tall =
            (sizeof(kGuide) / sizeof(kGuide[0]) + 2.0) * ch + 2.0 * padding_;
        const QRectF panel((width() - box) / 2.0, (height() - tall) / 2.0, box, tall);
        const Color bg = screen_.defaultBackground();
        const Color fg = screen_.defaultForeground();
        const QColor accent_ink(theme_.accent.r, theme_.accent.g, theme_.accent.b);

        // The same surface as every other question this program asks: the terminal's own
        // background, dimmed so the grid behind it reads as behind, its own font on it, and
        // nothing borrowed from the desktop's idea of a menu.
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(ink(fg, 80), 1));
        p.setBrush(ground(std::clamp(static_cast<int>(bg.r * 0.62), 0, 255),
                          std::clamp(static_cast<int>(bg.g * 0.62), 0, 255),
                          std::clamp(static_cast<int>(bg.b * 0.62 + 12), 0, 255)));
        p.drawRoundedRect(panel, panelRadius(ch), panelRadius(ch));

        // The title says what the panel is for, centred and ruled under in the accent so it
        // reads as a heading rather than as a ninth key. The row under it is left empty:
        // a heading with the list touching it is not a heading.
        // (Kaida, 2026-11-25: "Title Centered and underlined at the top with spacing between
        // it and the keys below".)
        const double title_y = panel.y() + padding_ + fm.ascent();
        p.setPen(ink(fg, 255));
        p.drawText(QRectF(panel.x(), title_y - fm.ascent(), panel.width(), fm.height()),
                   Qt::AlignHCenter | Qt::AlignVCenter, title);
        const double rule_y = title_y + 3.0;
        const double rule_left = panel.center().x() - title_w / 2.0;
        p.setPen(QPen(accent_ink, 1));
        p.drawLine(QPointF(rule_left, rule_y), QPointF(rule_left + title_w, rule_y));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(font_);

        const double key_x = panel.x() + side;
        const double what_x = key_x + key_w + gap;
        double y = title_y + 2.0 * ch; // the blank row between the heading and the list
        for (const Guide& entry : kGuide) {
            p.setPen(accent_ink);
            p.drawText(QPointF(key_x, y), QString::fromUtf8(entry.key));
            const QString what = QString::fromUtf8(entry.what);
            p.setPen(ink(fg, 225));
            p.drawText(QPointF(what_x, y), what);
            if (entry.reads_back) {
                // The readout is a fact about the window, not another instruction, so it
                // is said in a quieter voice — with the answer itself in the accent, which
                // is the one word worth reading.
                const double read_x = what_x + fm.horizontalAdvance(what) + between;
                p.setPen(ink(fg, 150));
                p.drawText(QPointF(read_x, y), reading_label);
                p.setPen(accent_ink);
                p.drawText(QPointF(read_x + fm.horizontalAdvance(reading_label), y),
                           reading_value);
            }
            y += ch;
        }
    }

    void paintOverlay(QPainter& p) const {
        if (!overlay_.active)
            return;
        const OverlayLayout lay = overlayLayout();
        QFontMetricsF fm(font_);
        const Color bg = screen_.defaultBackground();
        const Color fg = screen_.defaultForeground();

        // The panel is the terminal's own surface — its background, dimmed a
        // little so the grid behind it reads as "behind" — with the terminal's
        // own font on it. Nothing about it belongs to the desktop, which is
        // the entire point of drawing the question here rather than in a
        // window the window manager decorates for you.
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(ink(fg, 80), 1));
        p.setBrush(ground(std::clamp(static_cast<int>(bg.r * 0.62), 0, 255),
                         std::clamp(static_cast<int>(bg.g * 0.62), 0, 255),
                         std::clamp(static_cast<int>(bg.b * 0.62 + 12), 0, 255)));
        p.drawRoundedRect(lay.panel, panelRadius(cellHeight()), panelRadius(cellHeight()));

        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(font_);
        double y = lay.panel.y() + 1.5 * cellHeight() + fm.ascent();
        for (std::size_t i = 0; i < lay.lines.size(); ++i) {
            // The question is said at full strength; the detail under it is a
            // quieter voice, so the eye goes to the question first.
            const bool detail = static_cast<int>(i) >= lay.question_lines;
            p.setPen(detail ? ink(fg, 190) : ink(fg));
            p.drawText(QRectF(lay.panel.x(), y - fm.ascent(), lay.panel.width(), fm.height()),
                       Qt::AlignHCenter | Qt::AlignVCenter, lay.lines[i]);
            y += cellHeight();
        }

        // The field's caret: a bar in the accent, standing where the next letter will go.
        // It does not blink — a panel you are about to type into has nothing to compete
        // with, and a cursor that pulses in a dialog is a cursor that steals the question.
        if (overlay_.editable) {
            const int field_line = lay.question_lines - 1;
            const double field_y = lay.panel.y() + 1.5 * cellHeight() + fm.ascent() +
                                   field_line * cellHeight();
            const QString typed = QString::fromStdString(overlay_.field);
            const double x = lay.panel.center().x() + fm.horizontalAdvance(typed) / 2.0 + 1.0;
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(theme_.accent.r, theme_.accent.g, theme_.accent.b), 2));
            p.drawLine(QPointF(x, field_y - fm.ascent() + 2), QPointF(x, field_y + fm.descent()));
            p.setRenderHint(QPainter::Antialiasing, false);
        }

        for (std::size_t i = 0; i < lay.boxes.size(); ++i) {
            const bool focused = static_cast<int>(i) == overlay_.selected;
            if (overlay_.vertical) {
                // The focused row wears the same band as a selection made
                // with the pointer, because it means the same thing: this is
                // the line you are on.
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(focused ? ground(theme_.selection)
                                   : QColor(0, 0, 0, 0));
                if (focused)
                    p.drawRoundedRect(lay.boxes[i], kScrollBarRadius, kScrollBarRadius);
                p.setRenderHint(QPainter::Antialiasing, false);
                p.setPen(focused ? ink(240, 240, 240) : ink(fg, 225));
                p.drawText(lay.boxes[i].adjusted(8, 0, -8, 0),
                           Qt::AlignLeft | Qt::AlignVCenter, lay.choices[i]);
                continue;
            }
            // Buttons, kitty's way: no fill to speak of, a rule around the
            // word, and the highlighted one lit in blue rather than painted
            // over, so the row reads as part of the screen it is on.
            p.setRenderHint(QPainter::Antialiasing, true);
            const QColor accent(theme_.accent.r, theme_.accent.g, theme_.accent.b);
            p.setPen(QPen(focused ? accent : ink(fg, 150), 1));
            p.setBrush(focused ? ground(accent.red(), accent.green(), accent.blue(), 40)
                               : QColor(0, 0, 0, 0));
            p.drawRoundedRect(lay.boxes[i], kScrollBarRadius + 1, kScrollBarRadius + 1);
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(focused ? ink(240, 244, 255) : ink(fg, 200));
            p.drawText(lay.boxes[i], Qt::AlignCenter, lay.choices[i]);
        }

        if (!lay.hint.isEmpty()) {
            p.setPen(ink(fg, 140));
            p.drawText(lay.hint, Qt::AlignHCenter | Qt::AlignVCenter,
                       QString::fromStdString(overlay_.hint));
        }
    }

    // What an overlay last painted, so that a click is answered with the geometry
    // the pointer was looking at rather than a second guess at the layout. The
    // painting writes these on its way to the screen and a press only reads them;
    // computed twice, the two would drift and a click would land on the line above
    // the one you aimed at.
    // Air between a panel's title and the first line under it, so the title
    // reads as the name of the panel and not as the first row's label. In rows,
    // because six pixels written when every panel was drawn at one size does not
    // grow with a panel that is — and a title that touches its rows stops being a
    // title. (Kaida, 2026-11-20: "a bit more space between the title and the actual
    // settings".)
    static constexpr double kTitleGapRows = 0.75;
    // Air between the last row of a panel and the two words under it. The rows are the
    // thing you are choosing and the words are the thing you press afterwards; painted
    // against each other they read as one more row of the list, and `save` is not one more
    // colour. (Kaida, 2026-11-20: "a bit more space between the settings and the save /
    // cancel buttons".)
    static constexpr double kPanelBottomAir = 0.8;
    // Air between the lines of a panel. Rows used to sit exactly one row-height
    // apart, which made a panel read as one solid block of text with a highlight
    // somewhere in it; a couple of pixels between the lines is what lets each row
    // be a thing of its own. It is in pixels rather than cells because the ask was
    // for a little room, not for a bigger font.
    // (Kaida, 2026-11-20: "every elements could do with 1 or 2 px more space in
    // between them for breathing".)
    static constexpr double kPanelRowGap = 2.0;
    // How far down a scroll mark sits in its band of air, as a fraction of that band. The
    // same fraction from the opposite end for the other direction, so each mark sits
    // nearer the rows it is beside than the empty air it points away into.
    static constexpr double kPanelMarkLow = 0.62;
    // How much closer to the panel's top edge its title is painted than the rows are —
    // the title is a heading, not a row, and flush with the padding it sat too deep.
    // (Kaida, 2026-11-21: "we would need the title \"Colours\" to be a bit more up".)
    static constexpr double kPanelTitleRise = 0.45;
    // A menu is not the terminal. Its type starts smaller than the grid's, so a panel
    // reads as something laid over the screen rather than a block of text that fell off
    // it, and an ordinary window has room for the whole list at once. The terminal's own
    // font is untouched — the menu got smaller, not the person's type.
    // (Kaida, 2026-11-20: "The settings windows are too large and should be more
    // rounded".)
    static constexpr double kPanelFontScale = 0.9;
    // The margin inside a panel's edge, in rows. A row and a half was air nobody asked
    // for: the panel is wide because of what is in it, and the extra half row made every
    // one of them bigger than its own content.
    static constexpr double kPanelPadCells = 1.4;
    // How round a panel is. Four pixels on a panel several hundred wide reads as a
    // rectangle with soft edges; this is a corner you can see, and it grows with the
    // panel's own type so a small panel is not all curve.
    static double panelRadius(double ch) { return std::max(8.0, ch * 0.55); }
    static constexpr double kMixerBarRadius = 3.0;
    static constexpr double kMixerKnobWidth = 7.0;
    // How small a panel may go before it stops being a panel. Below this the
    // words turn into marks, and a menu you cannot read is not one you can
    // answer — better that it lose an edge than lose its letters.
    static constexpr int kPanelMinFontPixels = 8;
    // A list scrolls, so it is not allowed to demand a font: this is how many
    // rows of a long panel the fallback shrinks to fit, and beyond it the rows
    // walk off the bottom and come back the way they always have.
    static constexpr double kPanelListRows = 12.0;
    // How small a colour list may be set in order to show all of itself. Above this, the
    // whole list; below it, a dozen rows and a scroll — eight-pixel type you cannot read
    // is not better than a scroll you can.
    static constexpr int kPanelListFitMinPixels = 11;

    // What a panel is painted in: a font, its metrics, and the cell that font
    // makes. The panels measure themselves in cells, so these three are the
    // whole of their geometry.
    struct PanelFont {
        QFont font;
        QFontMetricsF fm;
        double cw = 1.0;
        double ch = 1.0;
    };

    // The font a panel of this many cells across and tall is drawn in. `extra_h`
    // is the room between the rows: pixels rather than cells, so it does not shrink
    // away on the very panel that most needs the air.
    //
    // A panel is laid out from its own content — a title, a table, a line of
    // keys at the bottom — and none of that content knows how big the window is.
    // Put one in a window smaller than it wants to be and it used to be painted
    // anyway, off both edges, with the hint running underneath the rows and the
    // two words landing on top of them. So the panel takes a smaller drawing
    // font until it fits. The terminal's own font is untouched: the window got
    // smaller, not the person's type, and a grid is not a menu.
    // (Kaida, 2026-11-20: "maybe a fallback max font size depending on the
    // windows size, to make sure our menu can fit properly without getting
    // mangled".)
    // `min_px` is how small the type may go on the way to fitting. A list that is trying
    // to show all of itself passes a higher floor than a panel that merely must not
    // overflow: shrinking a colour list to eight pixels to avoid scrolling is trading a
    // scroll for something worse.
    PanelFont panelFont(double cells_w, double cells_h, double extra_h = 0.0,
                        int min_px = kPanelMinFontPixels) const {
        // A panel starts smaller than the grid it is laid over — see kPanelFontScale — and
        // then shrinks further if the window still will not hold it. The metrics have to be
        // worked out from the smaller font rather than taken from the terminal's, or every
        // row is measured with one ruler and painted with another.
        PanelFont out{font_, QFontMetricsF(font_), cellWidth(), cellHeight()};
        if (out.font.pixelSize() <= 0) // a point-sized font still has to shrink
            out.font.setPixelSize(14);
        const int menu_px = std::max(
            kPanelMinFontPixels, static_cast<int>(out.font.pixelSize() * kPanelFontScale));
        out.font.setPixelSize(menu_px);
        out.fm = QFontMetricsF(out.font);
        out.cw = cellAdvance(out.fm);
        out.ch = out.fm.height();
        const double room_w = std::max(1.0, static_cast<double>(width()) - 2.0);
        const double room_h = std::max(1.0, static_cast<double>(height()) - 2.0);
        const double pad_two = 2.0 * kPanelPadCells; // left and right, or top and bottom
        for (int guard = 0; guard < 32; ++guard) {
            // A panel's padding is in rows, whatever the font, so it shrinks with it
            // rather than eating a small panel.
            const double want_w = cells_w * out.cw + pad_two * out.ch;
            const double want_h = cells_h * out.ch + extra_h + pad_two * out.ch;
            if (want_w <= room_w && want_h <= room_h)
                break;
            if (out.font.pixelSize() <= min_px)
                break; // nothing smaller is still readable; let it clip a little
            const double scale = std::min(room_w / want_w, room_h / want_h);
            int px = static_cast<int>(out.font.pixelSize() * scale);
            if (px >= out.font.pixelSize())
                px = out.font.pixelSize() - 1; // the ratio rounded up; step anyway
            px = std::max(min_px, px);
            out.font.setPixelSize(px);
            out.fm = QFontMetricsF(out.font);
            out.cw = cellAdvance(out.fm);
            out.ch = out.fm.height();
        }
        return out;
    }

    // A small triangle in a band of air, pointing at the rows that are off screen. Drawn
    // as a shape rather than a character because a panel's font is whatever the
    // terminal's font is, and a glyph it does not carry draws an empty box. Answers with
    // the rect it drew — empty if the band was too thin to hold one, because a mark
    // squashed into nothing is a smudge and not a sign.
    static QRectF paintScrollMark(QPainter& p, const QRectF& band, bool points_up,
                                  const QColor& ink, double cw, double ch) {
        const double hw = std::max(4.0, cw * 0.4);
        const double hh = std::max(3.0, ch * 0.28);
        if (band.height() < hh + 2.0)
            return QRectF();
        // Not centred in the band: a mark belongs to the rows it is next to, so it sits
        // nearer them than nearer the empty air. Centred, it read as a thing in the middle
        // of a gap that had nothing to do with either end of the list.
        // (Kaida, 2026-11-21: "the scrolling indicator on the Theme editor pointing down
        // should be closer to the color a bit more up".)
        const double cx = band.center().x();
        const double cy =
            band.top() + band.height() * (points_up ? kPanelMarkLow : 1.0 - kPanelMarkLow);
        const QPolygonF mark = points_up ? QPolygonF{QPointF(cx - hw, cy + hh / 2.0),
                                                     QPointF(cx + hw, cy + hh / 2.0),
                                                     QPointF(cx, cy - hh / 2.0)}
                                         : QPolygonF{QPointF(cx - hw, cy - hh / 2.0),
                                                     QPointF(cx + hw, cy - hh / 2.0),
                                                     QPointF(cx, cy + hh / 2.0)};
        p.setPen(Qt::NoPen);
        p.setBrush(ink);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawPolygon(mark);
        p.setRenderHint(QPainter::Antialiasing, false);
        return QRectF(cx - hw, cy - hh / 2.0, 2 * hw, hh);
    }

    // How wide the line of keys at the bottom of a panel is, in cells. It used to be what
    // the panel was fitted against, which is how a panel ended up as wide as a sentence
    // about how to use it. It wraps now — see `panelHint` — and this is only what it would
    // have wanted, kept for the panels that still measure themselves against it.
    int hintCells(const QString& hint) const {
        const QFontMetricsF fm(font_);
        const double cw = cellAdvance(fm);
        if (cw <= 0.0)
            return 0;
        return static_cast<int>(std::ceil(fm.horizontalAdvance(hint) / cw));
    }

    // The line of keys at the bottom of a panel, as small print broken over as many lines
    // as the panel's own width needs.
    //
    // It is set smaller because it is said once and read once: the keys are what you are
    // doing, not the thing to look at, and a panel whose hint is the same size as its rows
    // is a panel arguing with itself about which of them you are meant to read.
    // It is wrapped because it is a sentence, and a sentence that is not allowed to break
    // makes everything around it wider to hold it — the hint was the widest line in every
    // panel, and the panels were that wide.
    // (Kaida, 2026-11-20: "the tooltip at the bottom should use a smaller font to help and
    // maybe use more than 1 long line".)
    struct PanelHint {
        QFont font;
        QFontMetricsF fm{font};
        QStringList lines;
        double height = 0.0; // the lines themselves; the panel adds its own air
    };
    static constexpr double kPanelHintScale = 0.85;
    static constexpr int kPanelHintMaxLines = 3;

    PanelHint panelHint(const QString& hint, double avail_w, const PanelFont& pf) const {
        PanelHint out;
        out.font = pf.font;
        out.font.setPixelSize(
            std::max(kPanelMinFontPixels, static_cast<int>(pf.font.pixelSize() * kPanelHintScale)));
        out.fm = QFontMetricsF(out.font);
        double widest = 0.0;
        for (const QString& line : wrapWords(hint, out.fm, avail_w, kPanelHintMaxLines)) {
            out.lines.append(line);
            widest = std::max(widest, out.fm.horizontalAdvance(line));
        }
        // The height the lines need, at the size they are painted. A panel that gave one
        // row to this line and then drew two over its own words is the bug this replaces.
        out.height = out.fm.height() * out.lines.size();
        return out;
    }

    // Greedy word wrap, by width rather than by cells: the hint is proportional text in a
    // proportional font, and breaking it at a cell count would cut it mid-word on the words
    // that are wider than their share. A word longer than the line is left to overflow
    // rather than hyphenated — this is a hint, not a book.
    //
    // It breaks evenly rather than filling each line to the wall. Filled greedily, a hint
    // that needs two lines gets one full one and one with a word left over, and a line of
    // centred text with three words on it under a line with eight looks like a mistake even
    // when both are inside the panel. So: work out how many lines it takes, then aim each
    // line at the width they would have if the sentence were shared out.
    static QStringList wrapWords(const QString& text, const QFontMetricsF& fm, double avail_w,
                                 int max_lines) {
        const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        double total = 0.0;
        for (const QString& word : words)
            total += fm.horizontalAdvance(word) + fm.horizontalAdvance(QLatin1Char(' '));
        int lines_needed = 1;
        if (avail_w > 0.0)
            lines_needed = std::max(1, static_cast<int>(std::ceil(total / avail_w)));
        const int wanted = std::min(lines_needed, max_lines);
        const double target =
            std::min(avail_w, total / wanted + fm.horizontalAdvance(QLatin1Char(' ')));

        QStringList lines;
        QString line;
        for (const QString& word : words) {
            const QString joined = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
            if (!line.isEmpty() && fm.horizontalAdvance(joined) > target &&
                lines.size() + 1 < wanted) {
                lines.append(line);
                line = word;
            } else {
                line = joined; // even when it overflows: one wide word beats two cut ones
            }
        }
        if (!line.isEmpty())
            lines.append(line);
        if (lines.isEmpty())
            lines.append(text);
        return lines;
    }

    // Paint it, centred in the height that was reserved for it, bottom-up.
    void paintPanelHint(QPainter& p, const PanelHint& hint, const PanelFont& pf,
                        const QRectF& panel, double pad, double reserved_h,
                        const QColor& ink) const {
        if (hint.lines.isEmpty())
            return;
        p.setFont(hint.font);
        // Centred in the reservation rather than hung off its bottom, so a panel whose
        // second line of keys is shorter — or longer, on the way into the theme field —
        // keeps the same edge and the same gap above it.
        double y = panel.bottom() - pad - (reserved_h - hint.height) / 2.0;
        QRectF painted;
        for (int i = hint.lines.size() - 1; i >= 0; --i) {
            const QRectF line(panel.x() + pad / 2, y - hint.fm.height(), panel.width() - pad,
                              hint.fm.height());
            p.drawText(line,
                       static_cast<int>(Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine),
                       hint.lines.at(i));
            painted = painted.isNull() ? line : painted.united(line);
            y -= hint.fm.height();
        }
        p.setPen(ink); // the panel's rows are drawn at the panel's own size, not this
        // Recorded so a check can ask whether the hint is small print that fits, rather
        // than guessing at where a line of text was drawn.
        hint_hit_ = painted;
        panel_hint_lines_ = static_cast<int>(hint.lines.size());
        panel_hint_text_ = hint.lines.join(QLatin1Char(' '));
        panel_hint_px_ = hint.font.pixelSize();
        panel_rows_px_ = pf.font.pixelSize();
    }

    // The bottom of every panel is the same three things in the same order: the two words,
    // a breath, and the line of keys underneath them. It is worked out here, once, because
    // the hint is the one part whose height is not known until it has been set in its own
    // font and broken over the panel's width — and a panel that reserved one row for it and
    // then drew two would paint them through the words.
    //
    // A panel may have more than one thing it wants to say down there — the colour list says
    // something different when the keys are standing on the theme name — and then the height
    // is reserved for the longest of them. A panel that grew a row every time you walked to
    // the bottom of it would move the row you were looking at.
    struct PanelBottom {
        PanelHint hint;
        double reserved = 0.0; // the tallest variant of the line, which is what was paid for
        double height = 0.0;   // words, breath and the reservation together
    };
    static constexpr double kPanelWordsAir = 0.5; // rows of air between the words and the hint

    PanelBottom panelBottom(const QStringList& variants, double inner_w,
                            const PanelFont& pf) const {
        PanelBottom out;
        out.hint = panelHint(variants.isEmpty() ? QString() : variants.first(), inner_w, pf);
        for (const QString& variant : variants)
            out.reserved = std::max(out.reserved, panelHint(variant, inner_w, pf).height);
        out.height = pf.ch + kPanelWordsAir * pf.ch + out.reserved;
        return out;
    }
    static double panelWordsY(const QRectF& panel, double pad, const PanelBottom& bottom) {
        return panel.bottom() - pad - bottom.height;
    }

    // The surface every panel is painted on: the window's own background, dimmed
    // and with a little blue lifted, so it reads as a thing sitting over the
    // grid rather than a hole in it.
    static QColor panelGroundColour(const Color& bg) {
        return panel_ground(std::clamp(static_cast<int>(bg.r * 0.62), 0, 255),
                            std::clamp(static_cast<int>(bg.g * 0.62), 0, 255),
                            std::clamp(static_cast<int>(bg.b * 0.62 + 12), 0, 255));
    }

    // The one thing lit on a panel: a band in the selection colour behind whatever
    // the keys are acting on. It goes behind the value rather than across the row —
    // a row painted edge to edge says "this line", when what the keys mean is "this
    // number" — and in the mixer behind the colour band, which is the same value
    // seen as something you push. (Kaida, 2026-11-20: "the highlight shouldn't
    // color the whole row, it should look cleaner, and that on all settings page".)
    void paintFocusBand(QPainter& p, const QRectF& box) const {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(panel_ground(theme_.selection));
        p.drawRoundedRect(box, kScrollBarRadius, kScrollBarRadius);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // The ink that goes on top of a focus band. A band is filled with the selection's own
    // ground, so what stands on it wears the selection's own ink — the rule the dragged
    // selection already follows, and the reason a theme whose ordinary ink happens to be
    // its selection ground does not paint the one thing the keys are about in the colour it
    // is sitting on. Asking the panel's ink instead worked on the themes the panels were
    // first drawn against and nowhere else.
    // (Kaida, 2026-11-21: "In the settings menus the higlight uses the selection_background,
    // But it should also use selection_foreground. to ensure visibility in the case of
    // foreground being the same value as selection_background ... and make the whole thing
    // more cohesive".)
    QColor band_ink(int alpha = 255) const { return panel_ink(theme_.selection_foreground, alpha); }

    // What a cell is wearing right now, which is not always what it was written with.
    //
    // A cell nobody coloured wears the screen's own ink and ground, and a cell a program
    // coloured by number wears whatever that palette entry holds today. Both of those are
    // the theme's to decide, so both are answered here rather than remembered at the moment
    // the byte arrived: change the theme and the whole scrollback changes with it, which is
    // what a person means when they say a theme should change the colours. A colour a
    // program chose digit by digit is that program's, and stays exactly what it said.
    Color cellColour(const Color& stored, std::uint8_t role, bool foreground) const {
        if (role == kDefaultColour)
            return foreground ? screen_.defaultForeground() : screen_.defaultBackground();
        if (role < 16)
            return screen_.paletteColor(role);
        return stored;
    }

    // Which part of a theme's name its field shows, and whether either end is off-screen.
    // The caret is handed in when the field is open and the window follows it, so the letter
    // you are about to type next to is never off the panel; a shut field shows the front of
    // the word, which is the part a person recognises. The room asked for has the ellipses
    // already taken out of it, so a cut word is shorter rather than overflowing by a mark.
    struct NameFieldView {
        int start = 0;
        int end = 0;
        bool cut_front = false;
        bool cut_back = false;
    };

    static NameFieldView nameFieldView(const QString& name, int caret, double room_px,
                                       const QFontMetricsF& fm) {
        NameFieldView view{0, name.size(), false, false};
        if (name.isEmpty() || fm.horizontalAdvance(name) <= room_px)
            return view;
        const auto fits = [&](int from, int to) {
            return fm.horizontalAdvance(name.mid(from, to - from)) <= room_px;
        };
        if (caret < 0) { // shut: the front of the word, and an ellipsis where the rest is
            int to = 0;
            while (to < name.size() && fits(0, to + 1))
                ++to;
            view.end = to;
            view.cut_back = to < name.size();
            return view;
        }
        // Slide the window along until it holds the caret with something past it, or until
        // the caret is the last thing it can hold — which is where a caret at the end of a
        // word lives.
        for (int from = 0; from <= caret; ++from) {
            int to = from;
            while (to < name.size() && fits(from, to + 1))
                ++to;
            view.start = from;
            view.end = std::max(to, from);
            if (caret >= from && caret <= to && (to > caret || to == name.size()))
                break;
        }
        view.cut_front = view.start > 0;
        view.cut_back = view.end < name.size();
        return view;
    }

    // The mixer's title, naming the colour it is editing. "Mix colour" asks which
    // colour and leaves the question open; the row it came from has a name, and a
    // panel that edits one thing should say which. (Kaida, 2026-11-20: "The Colour
    // mixer should say the name of the colour currently being edited at the top".)
    QString mixerTitle() const {
        return mixer_title_.isEmpty() ? QStringLiteral("Mix colour")
                                      : QStringLiteral("Mix ") + mixer_title_;
    }

    // How light a colour reads, on the scale the eye actually uses.
    static double inkLuminance(int r, int g, int b) {
        return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
    }

    // `t` of the way from one flat colour to another. Also what compositing an
    // opaque colour over an opaque one at an alpha means, which is how the preview
    // shows a background at `background_opacity` on a panel that is not transparent.
    static QColor blendToward(const QColor& from, const QColor& toward, double t) {
        const auto part = [t](int a, int b) {
            return static_cast<int>(a + (b - a) * t + 0.5);
        };
        return QColor(std::clamp(part(from.red(), toward.red()), 0, 255),
                      std::clamp(part(from.green(), toward.green()), 0, 255),
                      std::clamp(part(from.blue(), toward.blue()), 0, 255));
    }

    // What the preview sentence sits on, for the three answers the switch beside it
    // walks. `Background` is the ground the colour is actually going to be read on:
    // the window's own background, at `background_opacity`, laid over the panel —
    // which is what "with the opacity of the background" has to mean here, since a
    // menu paints itself opaque whatever the transparency says. `Black` and `White`
    // are the two test grounds: a colour that reads on neither is a colour with a
    // problem, and the mixer should be able to say so about a colour you are one
    // keystroke from saving.
    // (Kaida, 2026-11-20: "The ground should just be of the current background color
    // (with the opacity of the backgroud) and have a switch on the side that
    // alternate between Black / White / Current Background colour".)
    static constexpr int kPreviewGroundCount = 3;

    static QColor previewGroundColour(int choice, const Color& bg, const QColor& panel,
                                      double opacity) {
        switch (choice) {
            case 1:
                return QColor(0, 0, 0);
            case 2:
                return QColor(255, 255, 255);
            default:
                break;
        }
        return blendToward(panel, QColor(bg.r, bg.g, bg.b), std::clamp(opacity, 0.0, 1.0));
    }

    static QString previewGroundName(int choice) {
        switch (choice) {
            case 1:
                return QStringLiteral("black");
            case 2:
                return QStringLiteral("white");
            default:
                break;
        }
        return QStringLiteral("background");
    }

    // Walking the switch. The words run in the order they read best against a
    // coloured ground — your own ground, then the two flat ones — and wrap, because
    // the way back from `white` should be the same gesture as the way to it.
    static int previewGroundStep(int choice, int direction) {
        return (choice + direction + kPreviewGroundCount) % kPreviewGroundCount;
    }

    struct OverlayHit {
        QRectF panel;
        std::vector<QRectF> rows;    // the visible rows, top to bottom
        std::vector<int> row_index;  // which line of the model each rect is
        double table_x = 0.0;        // where the value column starts, in pixels
        int label_w = 12;            // the label column, in cells
        double cw = 1.0;             // one cell, in pixels
        int lead = 0;                // cells before the first editable digit
        QRectF save;                 // the two words at the bottom of the panel
        QRectF cancel;
        QRectF preview;    // the mixer's sentence, which is the colour being mixed
        QRectF ground_switch;  // the word at the end of it, which changes what it sits on
        QRectF name;       // the picker's theme-name line, which is typed rather than walked
        std::vector<QRectF> bars;     // the mixer's channels, one bar per row
        std::vector<QRectF> swatches; // the picker's colour boxes, per row
        std::vector<QRectF> minus;    // the two arrows of each row, for a mouse
        std::vector<QRectF> plus;
        // The one thing on the panel wearing the highlight. It is recorded rather
        // than guessed, because "only one thing is lit at a time" is a rule a test
        // can check, and a test that has to work out where the highlight was drawn
        // is a test that disagrees with the panel about what a row is.
        QRectF highlight;
    };

    // The picker, painted in the same voice as the dialogs: the terminal's
    // dimmed own background, its own font, its own selection band under the
    // row you are on. Two things the other dialogs never needed: a swatch,
    // because a colour spelled in hex is a colour described and not shown;
    // and a cursor inside the hex text, because the next keystroke lands on
    // one specific digit and that has to be as visible as the digit itself.
    void paintPicker(QPainter& p) const {
        if (!picker_)
            return;
        const Color bg = screen_.defaultBackground();
        const Color fg = screen_.defaultForeground();

        const QString hint =
            QStringLiteral("Arrows to Navigate - Enter to Select - Escape to Cancel");
        // The theme field's own keys, said where every other key in this panel is said.
        // They used to be written after the name on its row, which is ten cells wide, and a
        // note twelve cells long walked out of the panel's side and hung over the terminal.
        // (Kaida, 2026-11-20: "the (e to edit) tool tip render outside our frame and it's not
        // beatiful at all".)
        // The name field's keys, said in the same three parts as every panel says them,
        // with one word swapped: the line of keys is the same size whoever the keys are
        // standing on. It used to grow a clause at the front, which made the panel taller
        // the moment the keys walked on to the name — a tooltip that changes the shape of
        // the thing it is describing is a tooltip that moves what you are aiming at.
        // (Kaida, 2026-11-20: "the (e to edit) tool tip render outside our frame and it's
        // not beatiful at all"; 2026-11-21: "The tooltips should stay the same size as
        // every other field even on the name field. The name field should just change the
        // \"Select\" word to \"Edit\" instead of adding a whole new line in the beginning
        // of the tooltip".)
        const QString name_hint =
            picker_editing_
                ? QStringLiteral("Arrows to Move - Enter to Confirm - Escape to Put Back")
                : QStringLiteral("Arrows to Navigate - Enter to Edit - Escape to Cancel");
        const auto& fields = picker_->fields();

        // The label column is as wide as the longest name plus a space —
        // `bright magenta` will not squeeze into a column sized for
        // `cursor` — and never narrower than the twelve cells the window
        // colours have always had.
        int label_w = 12;
        for (const ThemeField& field : fields) {
            const std::string& name = field.label.empty() ? field.key : field.label;
            label_w = std::max(label_w, static_cast<int>(name.size()) + 2);
        }
        // label column, hex 7, a two-cell gap, a two-cell swatch.
        const int table_cells = label_w + 7 + 2 + 2;

        // Twenty-one rows — five window colours and sixteen palette entries —
        // do not fit a short window, so the panel caps at the window's height
        // and the list scrolls beneath the highlight. The focused row is
        // always on screen, because a picker that walks its selection off the
        // top edge has lost the one line you were editing.
        const int total = static_cast<int>(fields.size());
        // A list has an answer for running out of room already, so the fallback
        // font is asked only to fit a panel of a dozen rows and the rest scrolls.
        const double gap = kPanelRowGap;
        // Three lines are the panel's own rather than the list's: a title, and the name the
        // colours go into with a row of air under it. The two words and the line of keys
        // under them are the panel's bottom, sized by themselves. The list gets what height
        // is left and scrolls.
        const double own_rows = 3.0;
        const double all_rows = own_rows + 2.0 + total;
        // Show the whole list when the window will hold it at a size worth reading. The
        // font used to be asked to fit a dozen rows and the rest scrolled, which meant a
        // window that had room for twenty-two rows of slightly smaller type was given
        // twelve of slightly larger type and a scroll. So: try for all of them down to
        // `kPanelListFitMinPixels`, and only if that fails fall back to the dozen-row
        // panel — which is where the scroll, and the mark saying there is more, come in.
        // (Kaida, 2026-11-21: "when there is enough space show all color in the theme
        // editor instead of having to scroll. prioritize showing everything without
        // scrolling, and when scrolling is needed can we show a little indicator to say
        // that there is more to be seen if the user scrolls".)
        const double room_w = std::max(1.0, static_cast<double>(width()) - 2.0);
        const double room_h = std::max(1.0, static_cast<double>(height()) - 2.0);
        const auto width_for = [table_cells, room_w](const PanelFont& f) {
            return std::min(table_cells * f.cw + 2.0 * kPanelPadCells * f.ch, room_w);
        };
        // The line of keys is the tallest thing at the bottom of a panel and its height
        // depends on the panel's width, so asking "would the whole list fit?" without it
        // is asking about a stack of rows rather than about the panel that gets built.
        // The reservation is for the longest of the lines the panel could say, which is
        // the same reservation the panel itself makes further down.
        const auto bottom_for = [this, &hint, &name_hint, &width_for](const PanelFont& f) {
            return panelBottom({hint, name_hint}, width_for(f) - kPanelPadCells * f.ch, f);
        };
        const double scroll_rows = std::min(all_rows, kPanelListRows);
        const PanelFont scrolling = panelFont(table_cells, scroll_rows, gap * scroll_rows);
        const double extra_all = gap * all_rows +
                                 (kTitleGapRows + kPanelBottomAir) * scrolling.ch +
                                 bottom_for(scrolling).height;
        const PanelFont whole =
            panelFont(table_cells, all_rows, extra_all, kPanelListFitMinPixels);
        const bool whole_fits =
            all_rows * whole.ch + extra_all + 2.0 * kPanelPadCells * whole.ch <= room_h;
        const PanelFont pf = whole_fits ? whole : scrolling;
        const double cw = pf.cw;
        const double ch = pf.ch;
        // The title's air, and the air the rows stop at before the two words — both in rows,
        // so they are the same shape of gap at every size a panel is drawn at.
        const double own_px = (kTitleGapRows + kPanelBottomAir) * ch;
        const QFontMetricsF& fm = pf.fm;
        const double pad = kPanelPadCells * ch;
        const double table_w = table_cells * cw;
        // As wide as its table, not as wide as the sentence at the bottom of it.
        const double panel_w = std::min(table_w + 2 * pad, std::max(2.0, width() - 2.0));
        // The line the panel says right now, and the height reserved for whichever of the
        // two it is — so walking on to and off the theme name changes the words and not the
        // size of the panel underneath them.
        const bool on_name = picker_focus_ == 3;
        const PanelBottom bottom =
            panelBottom({on_name ? name_hint : hint, hint, name_hint}, panel_w - pad, pf);
        const double panel_h =
            std::min(own_rows * (ch + gap) + own_px + total * (ch + gap) + bottom.height + 2 * pad,
                     std::max(2.0, height() - 2.0));
        const int visible = std::max(1, static_cast<int>((panel_h - 2 * pad - bottom.height -
                                                          own_px - own_rows * (ch + gap)) /
                                                         (ch + gap)));
        int first = 0;
        if (total > visible)
            first = std::clamp(picker_->row() - visible + 1, 0, total - visible);
        picker_scroll_ = PickerScroll{first, visible, total, QRectF(), QRectF()};

        const QRectF panel((width() - panel_w) / 2.0, (height() - panel_h) / 2.0, panel_w,
                           panel_h);

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(panel_ink(fg, 80), 1));
        p.setBrush(panelGroundColour(bg));
        p.drawRoundedRect(panel, panelRadius(ch), panelRadius(ch));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(pf.font);

        double y = panel.y() + pad + fm.ascent();
        p.setPen(panel_ink(fg));
        // The heading is raised out of the panel's padding a little: it is a title rather
        // than a row, and at one padding from the edge it sat as deep as the first line of
        // the thing it is heading. The rows stay where they are, so what the rise buys is
        // air between a panel's name and its contents.
        // (Kaida, 2026-11-21: "we would need the title "Colours" to be a bit more up".)
        const QRectF title_rect(panel.x(), y - fm.ascent() - kPanelTitleRise * ch,
                                panel.width(), fm.height());
        p.drawText(title_rect, Qt::AlignHCenter | Qt::AlignVCenter, pickerTitle());
        title_hit_ = title_rect;
        y += ch + kTitleGapRows * ch;

        // The rows are centred in the panel rather than hung off its left edge.
        // The panel is as wide as the widest thing in it, which is the line at the
        // bottom, and a table starting at the padding left all the extra width as
        // dead space on the right. (Kaida, 2026-11-20: "text should be centenred in
        // the interface, lots of empty space on the right as it is now".)
        const double table_x = panel.x() + (panel_w - table_w) / 2.0;
        const int last = std::min(first + visible, total);
        const double list_top = y - ch + fm.ascent() * 0.15;
        double list_bottom = list_top;
        picker_hit_ = OverlayHit{};
        picker_hit_.panel = panel;
        picker_hit_.table_x = table_x;
        picker_hit_.label_w = label_w;
        picker_hit_.cw = cw;
        for (int i = first; i < last; ++i) {
            const ThemeField& field = fields[static_cast<std::size_t>(i)];
            const bool focused = picker_focus_ == 0 && i == picker_->row();
            const QRectF row(panel.x() + pad / 2, y - ch + fm.ascent() * 0.15,
                             panel_w - pad, ch);
            picker_hit_.rows.push_back(row);
            picker_hit_.row_index.push_back(i);
            list_bottom = row.bottom();
            // The value is what is lit, not the line: see the same choice in
            // `paintSettings`. Here the value is the six digits, and the swatch
            // beside them stays a plain square — it is the colour, not a number
            // anybody is standing on.
            const QRectF value_box(table_x + label_w * cw - cw * 0.4, row.top() + 1.0,
                                   7.8 * cw, ch - 2.0);
            if (focused) {
                paintFocusBand(p, value_box);
                picker_hit_.highlight = value_box;
            }
            p.setPen(panel_ink(fg));
            // The label sits in its column so every hex value starts at the
            // same x; a table that jaggeds is harder to read than one with a
            // column nobody asked for.
            const QString label =
                QString::fromStdString(field.label.empty() ? field.key : field.label);
            p.drawText(QPointF(table_x, y), label.leftJustified(label_w, QLatin1Char(' ')));
            const std::string hex = colourText(field.value);
            // The value stands on the band, so it wears the band's ink; the label beside it
            // is on bare panel and keeps the panel's.
            p.setPen(focused ? band_ink() : panel_ink(fg));
            p.drawText(QPointF(table_x + label_w * cw, y), QString::fromStdString(hex));

            // The digit the next keystroke will replace, lit with the same
            // accent a dialog puts on the answer its focus is on. A colour's text
            // is `#rrggbb`, so its first editable digit sits one cell in from where
            // the text starts — skip that cell or every cursor lands one digit
            // early. A percentage has no leading cell, so its first digit is first.
            if (focused && picker_focus_ == 0) {
                const QColor accent(theme_.accent.r, theme_.accent.g, theme_.accent.b);
                p.setPen(QPen(accent, 1));
                const double dx = table_x + (label_w + 1 + picker_->digit()) * cw;
                p.drawLine(QPointF(dx, y + fm.descent() * 0.4),
                           QPointF(dx + cw - 1, y + fm.descent() * 0.4));
            }

            // The swatch: the colour itself, two cells wide, wearing a thin
            // rule of the foreground so a near-background colour is still a
            // rectangle you can see. For transparency the swatch shows the
            // transparency — the same ink filled at the chosen opacity, so a low
            // number is a faint rectangle and 100 is solid.
            const QRectF swatch(table_x + (label_w + 7 + 2) * cw,
                                y - ch + fm.ascent() * 0.15 + 2, 2 * cw, ch - 4);
            p.setPen(QPen(panel_ink(fg, 120), 1));
            const Rgb& c = field.value;
            p.setBrush(QColor(c.r, c.g, c.b));
            p.drawRect(swatch);
            // Recorded so a press on the colour box means "mix this colour": the
            // swatch is the one part of the row that is the colour rather than a
            // description of it, so it is where a pointer lands when somebody wants
            // to change it.
            picker_hit_.swatches.push_back(swatch);
            y += ch + gap;
        }

        // The name the colours go into: the table's own columns, one line above the two
        // words. It is a row of the table rather than a caption on the panel because it
        // is one of the things you are choosing, and it is always there — a field you
        // have to be told about before you can save a theme under a new name is a field
        // you will not find. (Kaida, 2026-11-20: "the picker's name field is always
        // visible — saving under the loaded name asks to overwrite, a new name writes a
        // new theme".)
        const double words_y = panelWordsY(panel, pad, bottom);
        // A row of air between the name and the two words. Hanging it one row-gap below
        // the table made it read as a third word rather than the last line of the list —
        // the two things on that bottom edge are the ones you press, and the line above
        // them is the thing you are naming.
        // (Kaida, 2026-11-20: "The theme name is too close to the save and cancel buttons
        // it should be a bit higher".)
        // The scroll marks: a triangle in the air above the list when there are rows
        // above it, and one below when there are more below. They sit in air the panel
        // already leaves — between its title and its rows, and between its rows and its
        // two words — so appearing costs no row and the panel does not change size when
        // one is drawn, which is the mistake to avoid in anything that comes and goes
        // while the keys are walking.
        // (Kaida, 2026-11-21: "when scrolling is needed can we show a little indicator to
        // say that there is more to be seen if the user scrolls".)
        if (first > 0)
            picker_scroll_.up = paintScrollMark(p, QRectF(panel.x(), title_hit_.bottom(),
                                                          panel_w, list_top - title_hit_.bottom()),
                                                true, panel_ink(fg, 120), cw, ch);
        if (first + visible < total)
            picker_scroll_.down =
                paintScrollMark(p, QRectF(panel.x(), list_bottom, panel_w, words_y - list_bottom),
                                false, panel_ink(fg, 120), cw, ch);
        const double name_air = ch;
        const QRectF name_box(panel.x() + pad / 2, words_y - name_air - ch, panel_w - pad, ch);
        picker_hit_.name = name_box;
        const double name_y = words_y - name_air - fm.ascent() * 0.15;
        const double value_x = table_x + label_w * cw;
        const QString name_value = picker_name_;
        // The name is a field with a fixed mouth, not a label that stretches for as long as
        // the word lets it: from the value column to the panel's own right edge, which is
        // air the panel already reserves. A theme named for the machine it belongs to is
        // longer than that, and the word used to be drawn at whatever length it happened to
        // be — out through the panel's side, across the terminal and off the window, with
        // the panel's border left somewhere behind it. What does not fit now scrolls, and a
        // mark says there is more on that side.
        // (Kaida, 2026-11-21: "A theme name that is too long will get out of the window
        // breaking the ui when in the theme editor".)
        const double field_x = value_x - cw * 0.4;
        const double field_w =
            std::max(2.0 * cw, panel.x() + panel_w - pad * 0.5 - field_x);
        const QRectF name_value_box(field_x, name_box.top() + 1.0, field_w, ch - 2.0);
        const QString ellipsis = QStringLiteral("\u2026");
        const double ell = fm.horizontalAdvance(ellipsis);
        const NameFieldView view =
            nameFieldView(name_value, picker_editing_ ? picker_caret_ : -1,
                          field_w - cw * 0.8 - 2.0 * ell, fm);
        const QString shown = name_value.mid(view.start, view.end - view.start);
        // Lit like every other value here is: the thing the keys act on, which in a name
        // is the name and not the line it sits in. The band is the field rather than the
        // word, so lighting the row does not change width as the name grows.
        if (picker_focus_ == 3) {
            paintFocusBand(p, name_value_box);
            picker_hit_.highlight = name_value_box;
        }
        p.setPen(panel_ink(fg));
        p.drawText(QPointF(table_x, name_y),
                   QStringLiteral("theme").leftJustified(label_w, QLatin1Char(' ')));
        p.setPen(picker_focus_ == 3 ? band_ink() : panel_ink(fg));
        const double word_x = field_x + cw * 0.4 + (view.cut_front ? ell : 0.0);
        p.setClipRect(name_value_box);
        p.drawText(QPointF(word_x, name_y), shown);
        p.setClipping(false);
        if (view.cut_front)
            p.drawText(QPointF(field_x + cw * 0.2, name_y), ellipsis);
        if (view.cut_back)
            p.drawText(QPointF(field_x + field_w - cw * 0.4 - ell, name_y), ellipsis);
        if (picker_focus_ == 3 && picker_editing_) {
            // Where the next character goes. The colour rows show the same rule under a
            // digit; a word has the same rule between its letters, and the arrows move it
            // the way they move a digit along a hex value.
            //
            // A shut field used to write `(e to edit)` here, after the name. The value
            // column is ten cells and the note was twelve, so it walked out through the
            // panel's side and hung over the terminal; the way to open the field is said
            // along the bottom of the panel with every other key now.
            // (Kaida, 2026-11-20: "the (e to edit) tool tip render outside our frame and
            // it's not beatiful at all".)
            const QColor accent(theme_.accent.r, theme_.accent.g, theme_.accent.b);
            p.setPen(QPen(accent, 1));
            const double caret =
                word_x + fm.horizontalAdvance(shown.left(picker_caret_ - view.start)) + 1.0;
            p.drawLine(QPointF(caret, name_y + fm.descent() * 0.4),
                       QPointF(caret + cw - 2.0, name_y + fm.descent() * 0.4));
        }

        // While the panel is asking about a theme that already exists, the two words are
        // the question's two answers. Same places, same walking, one less thing to learn
        // for a panel that occasionally has to ask.
        paintOverlayButtons(p, panel, pf, fg, picker_hit_.save, picker_hit_.cancel,
                            picker_focus_, words_y,
                            picker_overwrite_ ? QStringLiteral("overwrite")
                                              : QStringLiteral("save"),
                            picker_overwrite_ ? QStringLiteral("keep it")
                                              : QStringLiteral("cancel"));
        noteWordHit(picker_hit_, picker_focus_);

        // The hint is anchored to the bottom of the panel, because the panel
        // may be shorter than the list: a hint that scrolls away with the
        // rows would be the one line the picker always had and never kept.
        p.setPen(panel_ink(fg, 140));
        paintPanelHint(p, bottom.hint, pf, panel, pad, bottom.reserved, panel_ink(fg, 140));
    }

    // The settings menu, in the picker's voice: the same dimmed panel, the same
    // highlight band, the same cursor over one digit. Two things differ, and both
    // are because a setting is not a colour. A value is padded to the width it can
    // reach, so `05000` and `01000` line up and the cursor stays over the digit it
    // names instead of sliding when the number loses a place. And every row carries
    // a `-` and a `+`: a number you can nudge by one as well as type, and an answer
    // you can only ever step between the ones it has — which is what makes an
    // invalid value unreachable with the mouse too, not only with the keys.
    void paintSettings(QPainter& p) const {
        if (!settingsActive())
            return;
        const Color bg = screen_.defaultBackground();
        const Color fg = screen_.defaultForeground();
        // On a row that opens a panel, Enter does not "select" anything: it opens. The
        // line along the bottom says which of the two it is, because the keys standing on
        // a row that behaves differently should not be described as if they were not.
        const QString hint =
            settings_->current().opens
                ? QStringLiteral("Enter opens the theme editor - Arrows to Navigate - "
                                 "Escape to Cancel")
                : QStringLiteral("Arrows to Navigate - Enter to Select - Escape to Cancel");
        const auto& rows = settings_->rows();

        int label_w = 16;
        int value_w = 5;
        for (const SettingRow& row : rows) {
            const std::string& name = row.label.empty() ? row.key : row.label;
            label_w = std::max(label_w, static_cast<int>(name.size()) + 2);
            value_w = std::max(value_w, static_cast<int>(settingValueText(row).size()));
        }
        const int table_cells = label_w + value_w + 4; // label, value, gap, two arrows
        const int total = static_cast<int>(rows.size());
        // As in the picker: the fallback font fits a panel of a dozen rows and
        // the list scrolls, because a list already has an answer for height.
        const double gap = kPanelRowGap;
        const double own_rows = 1.0; // the title; the words and the hint are the panel's bottom
        const double rows_tall = std::min(own_rows + 2.0 + total, kPanelListRows);
        const PanelFont pf = panelFont(table_cells, rows_tall, gap * rows_tall);
        const double cw = pf.cw;
        const double ch = pf.ch;
        const double own_px = (kTitleGapRows + kPanelBottomAir) * ch;
        const QFontMetricsF& fm = pf.fm;
        const double pad = kPanelPadCells * ch;
        const double table_w = table_cells * cw;
        // As wide as its rows. The line of keys underneath used to set the width, which is
        // how a menu about a font size ended up as wide as a sentence about arrow keys.
        const double panel_w = std::min(table_w + 2 * pad, std::max(2.0, width() - 2.0));
        const PanelBottom bottom = panelBottom({hint}, panel_w - pad, pf);
        const double panel_h =
            std::min(own_rows * (ch + gap) + own_px + total * (ch + gap) + bottom.height + 2 * pad,
                     std::max(2.0, height() - 2.0));
        const int visible = std::max(1, static_cast<int>((panel_h - 2 * pad - bottom.height -
                                                          own_px - own_rows * (ch + gap)) /
                                                         (ch + gap)));
        int first = 0;
        if (total > visible)
            first = std::clamp(settings_->row() - visible + 1, 0, total - visible);
        const QRectF panel((width() - panel_w) / 2.0, (height() - panel_h) / 2.0, panel_w,
                           panel_h);

        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(panel_ink(fg, 80), 1));
        p.setBrush(panelGroundColour(bg));
        p.drawRoundedRect(panel, panelRadius(ch), panelRadius(ch));
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setFont(pf.font);

        double y = panel.y() + pad + fm.ascent();
        p.setPen(panel_ink(fg));
        // The heading is raised out of the panel's padding a little: it is a title rather
        // than a row, and at one padding from the edge it sat as deep as the first line of
        // the thing it is heading. The rows stay where they are, so what the rise buys is
        // air between a panel's name and its contents.
        // (Kaida, 2026-11-21: "we would need the title "Colours" to be a bit more up".)
        const QRectF title_rect(panel.x(), y - fm.ascent() - kPanelTitleRise * ch,
                                panel.width(), fm.height());
        p.drawText(title_rect, Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Settings"));
        title_hit_ = title_rect;
        y += ch + kTitleGapRows * ch;

        // The rows are centred in the panel rather than hung off its left edge.
        // The panel is as wide as the widest thing in it, which is the line at the
        // bottom, and a table starting at the padding left all the extra width as
        // dead space on the right. (Kaida, 2026-11-20: "text should be centenred in
        // the interface, lots of empty space on the right as it is now".)
        const double table_x = panel.x() + (panel_w - table_w) / 2.0;
        const int last = std::min(first + visible, total);
        settings_hit_ = OverlayHit{};
        settings_hit_.panel = panel;
        settings_hit_.table_x = table_x;
        settings_hit_.label_w = label_w;
        settings_hit_.cw = cw;
        for (int i = first; i < last; ++i) {
            const SettingRow& row = rows[static_cast<std::size_t>(i)];
            const bool focused = settings_focus_ == 0 && i == settings_->row();
            const QRectF box(panel.x() + pad / 2, y - ch + fm.ascent() * 0.15,
                             panel_w - pad, ch);
            settings_hit_.rows.push_back(box);
            settings_hit_.row_index.push_back(i);
            const QString label =
                QString::fromStdString(row.label.empty() ? row.key : row.label);
            const QString value = QString::fromStdString(settingValueText(row));
            // What is lit is the value, not the row. A band from edge to edge says
            // "this line", which is true but not what the keys mean: they are on the
            // number, and the number is what they change. The row's own rect stays
            // full-width all the same, so a click anywhere on the line still picks
            // it. (Kaida, 2026-11-20: "the highlight shouldn't color the whole row,
            // it should look cleaner" — and, on what to light instead, "highlight the
            // value being edited".)
            //
            // Measured against this row's own value rather than the column's widest:
            // a short answer wearing a long band is the same lie as a whole row
            // wearing one, and it runs into the arrows besides. The band goes down
            // before the text does, which is the only reason the strings are worked
            // out this far ahead of being drawn.
            const double value_cells_here = fm.horizontalAdvance(value) / cw;
            const QRectF value_box(table_x + label_w * cw - cw * 0.4, box.top() + 1.0,
                                   (value_cells_here + 0.8) * cw, ch - 2.0);
            if (focused) {
                paintFocusBand(p, value_box);
                settings_hit_.highlight = value_box;
            }
            p.setPen(panel_ink(fg));
            p.drawText(QPointF(table_x, y), label.leftJustified(label_w, QLatin1Char(' ')));
            // Only the value is on the band, so only the value changes ink.
            if (focused)
                p.setPen(band_ink());
            p.drawText(QPointF(table_x + label_w * cw, y), value);

            // The row's two arrows: the same two keystrokes, drawn where a pointer
            // can find them. Both go through the model's own clamps, so clicking
            // `+` on a font of 72 does what pressing it does, which is nothing.
            const QRectF minus(table_x + (label_w + value_w + 1) * cw, box.top(),
                               cw * 1.5, ch);
            const QRectF plus(table_x + (label_w + value_w + 2.5) * cw, box.top(),
                              cw * 1.5, ch);
            settings_hit_.minus.push_back(minus);
            settings_hit_.plus.push_back(plus);
            p.setPen(panel_ink(fg, focused ? 255 : 150));
            p.drawText(minus, Qt::AlignCenter, QStringLiteral("-"));
            p.drawText(plus, Qt::AlignCenter, QStringLiteral("+"));

            // The digit the next keystroke or the next click will replace. A choice
            // row gets no cursor: its value is one of a few whole answers, and half
            // of an answer is not a thing.
            if (focused && settings_focus_ == 0 && row.kind != SettingKind::Choice) {
                const QColor accent(theme_.accent.r, theme_.accent.g, theme_.accent.b);
                p.setPen(QPen(accent, 1));
                const double dx = table_x + (label_w + settings_->digit()) * cw;
                p.drawLine(QPointF(dx, y + fm.descent() * 0.4),
                           QPointF(dx + cw - 1, y + fm.descent() * 0.4));
            }
            y += ch + gap;
        }

        paintOverlayButtons(p, panel, pf, fg, settings_hit_.save, settings_hit_.cancel,
                            settings_focus_, panelWordsY(panel, pad, bottom));
        noteWordHit(settings_hit_, settings_focus_);
        p.setPen(panel_ink(fg, 140));
        paintPanelHint(p, bottom.hint, pf, panel, pad, bottom.reserved, panel_ink(fg, 140));
    }

    // The two words at the bottom of an overlay: `save` and `leave`. A panel that
    // only the keyboard can answer is a panel a mouse cannot finish, and these are
    // the two things every dialog has always been click-worthy — do it, or don't.
    // They are painted in the panel's own voice, and both stay live whatever row
    // the highlight is on, because clicking `leave` should never depend on where
    // you were editing.
    //
    // They are measured in the panel's font, not the window's. Sized from the
    // terminal's cells they kept their full height in a panel that had shrunk to
    // fit a small window, and landed on the last row of the list.
    void paintOverlayButtons(QPainter& p, const QRectF& panel, const PanelFont& pf,
                             const Color& fg, QRectF& save, QRectF& cancel, int focus,
                             double words_y,
                             const QString& save_word = QStringLiteral("save"),
                             const QString& cancel_word = QStringLiteral("cancel")) const {
        const double ch = pf.ch;
        const double cw = pf.cw;
        const double gap = 3 * cw;
        const double save_w = pf.fm.horizontalAdvance(save_word) + cw;
        const double cancel_w = pf.fm.horizontalAdvance(cancel_word) + cw;
        // The caller says where the words go, because how far above the hint they have to
        // sit depends on how tall the hint turned out to be. Painted flush, the two read as
        // one line of small text, and the words are the part you are meant to be able to
        // hit. (Kaida, 2026-11-20: "they are a bit too close to the control indication
        // written under them".)
        const double y = words_y;
        const double left = panel.x() + (panel.width() - (save_w + gap + cancel_w)) / 2.0;
        save = QRectF(left, y, save_w, ch);
        cancel = QRectF(left + save_w + gap, y, cancel_w, ch);
        struct { const QString* word; const QRectF* box; QColor colour; int index; } buttons[2] = {
            {&save_word, &save, QColor(theme_.accent.r, theme_.accent.g, theme_.accent.b), 1},
            {&cancel_word, &cancel, QColor(fg.r, fg.g, fg.b), 2}};
        for (const auto& button : buttons) {
            const bool lit = focus == button.index;
            p.setRenderHint(QPainter::Antialiasing, true);
            // The word the keyboard is on is filled the way a focused row is, so
            // `tab` has something visible to move on to instead of appearing to do
            // nothing at the bottom of the panel.
            p.setPen(QPen(panel_ink(button.colour.red(), button.colour.green(),
                              button.colour.blue(), lit ? 255 : 150), 1));
            p.setBrush(lit ? panel_ground(theme_.selection) : QColor());
            if (lit)
                p.setBrush(QColor(theme_.selection.r, theme_.selection.g,
                                  theme_.selection.b, scaleAlpha(220, background_opacity_)));
            p.drawRoundedRect(*button.box, kScrollBarRadius, kScrollBarRadius);
            p.setRenderHint(QPainter::Antialiasing, false);
            // A lit word sits on a band like any other value, so it takes the band's ink
            // rather than its own: `leave` is drawn in the ordinary foreground, which is
            // exactly the colour a theme can also use for its selection ground.
            p.setPen(lit ? band_ink()
                         : panel_ink(button.colour.red(), button.colour.green(),
                                     button.colour.blue()));
            p.drawText(*button.box, Qt::AlignCenter, *button.word);
        }
    }

    // The two words paint their own band, so the only thing missing from "where is
    // the highlight" is the answer for them: the word the keys are standing on is
    // the lit thing, same as a value or a band.
    static void noteWordHit(OverlayHit& hit, int focus) {
        if (focus == 1)
            hit.highlight = hit.save;
        else if (focus == 2)
            hit.highlight = hit.cancel;
    }

    // Where a press landed inside an overlay. The answer is one of the things the
    // panel can act on, worked out from the geometry that was actually painted.
    struct OverlayPress {
        int row = -1;    // which line of the model, or -1 for none of them
        int digit = -1;  // which digit of its value, or -1
        bool save = false;
        bool leave = false;
        bool name = false; // the picker's theme-name line
        bool step_down = false; // the row's `-` arrow
        bool step_up = false;   // and its `+` arrow
        bool hit = false; // anywhere inside the panel, buttons and rows included
    };
    OverlayPress overlayPressAt(const OverlayHit& hit, const QPointF& where) const {
        OverlayPress out;
        if (!hit.panel.isValid() || !hit.panel.contains(where))
            return out;
        out.hit = true;
        if (hit.save.contains(where)) {
            out.save = true;
            return out;
        }
        if (hit.cancel.contains(where)) {
            out.leave = true;
            return out;
        }
        if (hit.name.contains(where)) {
            out.name = true;
            return out;
        }
        for (std::size_t i = 0; i < hit.rows.size(); ++i) {
            if (!hit.rows[i].contains(where))
                continue;
            out.row = hit.row_index[i];
            if (i < hit.minus.size() && hit.minus[i].contains(where)) {
                out.step_down = true;
                return out;
            }
            if (i < hit.plus.size() && hit.plus[i].contains(where)) {
                out.step_up = true;
                return out;
            }
            // The value column starts after the labels; a press in it picks the
            // digit under the pointer, which is the whole point of a cursor you
            // can put where you want it.
            const double value_x = hit.table_x + (hit.label_w + hit.lead) * hit.cw;
            if (hit.cw > 0.0 && where.x() >= value_x)
                out.digit = static_cast<int>((where.x() - value_x) / hit.cw);
            return out;
        }
        return out;
    }

    // A press that lands on the indicator belongs to the terminal, not to the
    // application inside it and not to a text selection.
    bool scrollBarPress(const QPointF& where) {
        scroll_bar_ = scrollIndicator();
        if (!scroll_bar_.visible)
            return false;
        const QPoint point = where.toPoint();
        if (!trackContains(scroll_bar_, point))
            return false;
        if (thumbContains(scroll_bar_, point)) {
            scrollbar_drag_ = true;
            scrollbar_grab_ = point.y() - scroll_bar_.thumb.top();
            setCursor(Qt::SizeVerCursor);
        } else if (point.y() < scroll_bar_.thumb.top()) {
            scrollBy(std::max(1, rows_ - 1)); // click above the thumb: older
        } else {
            scrollBy(-std::max(1, rows_ - 1)); // below it: newer
        }
        return true;
    }

    void scrollBarDragTo(const QPointF& where) {
        scroll_bar_ = scrollIndicator();
        const int top = where.toPoint().y() - scrollbar_grab_;
        const int offset = scrollOffsetForThumbY(top, scroll_bar_, scrollRange(), rows_);
        scrollBy(offset - scroll_offset_);
        // The thumb follows the pointer even at either end of the scrollback,
        // where the offset itself cannot move.
        scroll_bar_ = scrollBarWithThumbTop(scroll_bar_, top);
        update();
    }

    void endScrollBarDrag() {
        if (!scrollbar_drag_)
            return;
        scrollbar_drag_ = false;
        unsetCursor();
        update();
    }

    std::unique_ptr<TerminalBackend> backend_;
    // The places drawn beside the one this window looks at, while the session shows
    // them side by side: one neighbour per place that is not the one being looked at.
    // Empty when there is nothing beside it — one place, or tabs, where the window is
    // the only thing on the screen. The place being looked at is not among them: it is
    // the window's own screen, and typing, the cursor and the highlight all belong to it.
    std::vector<std::unique_ptr<ExtraPane>> neighbours_;
    // How to open a connection to a place, when the window is told to show one beside
    // the place it is looking at. The window does not know a daemon exists; the host
    // that does — a window onto a session — sets this so the window can ask for a
    // neighbour without knowing what it is opening. Left unset for a widget driving a
    // single backend, and for a check that hands neighbours in by hand.
    std::function<std::unique_ptr<TerminalBackend>(int pane)> pane_connector_;
    // How many paints this window has ever been asked to do, and how many it had done the
    // last time it was shown. A window asked to draw before it has a surface does the work
    // and nothing comes of it; these two say whether anything has been drawn since.
    qint64 paint_count_ = 0;
    qint64 shown_paint_count_ = 0;
    int repaint_timer_ = 0;
    bool synced_block_open = false;
    QElapsedTimer repaint_deadline_;
    static constexpr int kRepaintPollMs = 120;
    static constexpr int kRepaintAskMs = 3000;
    Screen screen_;
    QFont font_;
    // Declared in the order the constructor initialises them: members are built
    // in declaration order whatever the initialiser list says, so keeping the two
    // in step is what makes the list safe to read.
    int rows_;
    int cols_;
    std::thread reader_;
    std::atomic<bool> running_;
    std::atomic<bool> shell_alive_;
    // Which shell the reader thread is reading. Raised when the window is pointed at
    // another one, so events from the old shell can be recognised and dropped.
    std::atomic<int> generation_{0};
    // The neighbour's own number, separate because it comes and goes on its own
    // schedule: opening and closing a second place must not make the window stop
    // listening for bytes from the place it has been showing all along.
    int extra_generation_ = 0;
    std::string status_;
    QTimer* status_timer_ = nullptr;
    // Ctrl+K waits for one key. Armed means that key has not arrived yet.
    bool prefix_armed_ = false;
    bool show_key_hints_ = true;
    // Ctrl+K then R: the arrows move the divider of the place in hand instead of the
    // cursor, until Enter keeps it or Esc puts the arrangement back.
    bool resize_mode_ = false;
    Overlay overlay_;
    // The theme picker, when it is up: the model holds the decisions, and the
    // theme as it was on opening is kept so leaving with Esc puts the window
    // back exactly as it found it. `owns_screen_` is remembered from the last
    // `applyTheme` so the live preview colours the blank screen only when the
    // window is entitled to.
    std::unique_ptr<ThemePicker> picker_;
    Theme picker_before_;
    Screen::ColourLocks picker_saved_locks_; // the config's locks, restored on a cancel
    std::function<void(std::vector<ThemeField> now, bool saved, std::string name)> picker_finish_;
    // The name the colours would be saved under, as it stands in the field, and the one
    // they are wearing now. Editing the first into something else is how a new theme
    // gets its name, so the panel has to hold both: one is what to write over, the other
    // is what to call the file.
    QString picker_name_;
    std::string picker_loaded_name_;
    std::function<bool(const std::string& name)> picker_name_taken_;
    // The field is typed into only when it has been opened with `e`, and then the arrows
    // are a caret inside the word rather than a walk between the panel's stops. What the
    // name was when `e` was pressed is kept, so Esc puts the word back rather than
    // leaving half of it on the screen.
    bool picker_editing_ = false;
    int picker_caret_ = 0;
    QString picker_before_edit_;
    // True while the panel is asking whether to write over a theme that is already
    // there. The question lives in the same panel rather than in a window of its own,
    // because a question that appears somewhere else is a question about a panel you
    // have just stopped looking at.
    bool picker_overwrite_ = false;
    // The sentence the colour mixer's preview strip is painted with.
    inline static const QString kMixerSample = QStringLiteral("0123 #$%& Color Preview");
    // Recorded while painting, as the panels' hit rects are.
    mutable bool mixer_sample_fits_ = true;
    mutable PickerScroll picker_scroll_;
    std::unique_ptr<SettingsPicker> settings_;
    // The menu, still holding its rows and its unsaved values, while another panel is up.
    bool settings_suspended_ = false;
    std::function<void(const SettingRow&)> settings_open_;
    std::function<void(std::vector<SettingRow>, bool saved)> settings_finish_;
    std::function<void(const SettingRow&)> settings_preview_;
    // Which of an overlay's three keyboard stops the keys are on: the list, `save`,
    // or `leave`. Tab walks them, and Enter answers the one you are standing on.
    int picker_focus_ = 0;
    int settings_focus_ = 0;
    std::unique_ptr<ColorMixer> mixer_;
    std::function<void(Rgb, bool)> mixer_finish_;
    std::function<void(Rgb)> mixer_preview_;
    // Where the keys are in the mixer: 0 is a channel row, 1 and 2 are the two words
    // at the bottom, and 3 is the preview above the rows — a stop of its own because
    // what sits under the sentence is a choice with three answers, and a choice you
    // can only reach with a pointer is a choice the keys do not have.
    int mixer_focus_ = 0;
    // What the preview sentence sits on, walked by the switch at the end of the
    // strip. Kept here rather than in the mixer so the choice outlives one mixing: a
    // ground picked because the window's own was unreadable is picked for the window,
    // not for the colour.
    int mixer_ground_ = 0;
    // The name of the colour being mixed, taken from the row the mixer was opened
    // on, for the title. Empty only if the mixer was ever opened from somewhere
    // that is not a named colour, which it is not.
    QString mixer_title_;
    int mixer_drag_ = -1; // the bar a mouse is currently pushing along
    // Where the line of keys at the bottom of a panel landed, and how big it is set
    // next to the rows above it. Recorded while painting, like every other hit rect,
    // because a layout rule nobody can measure is a layout rule that quietly lapses.
    mutable QRectF hint_hit_;
    mutable int panel_hint_lines_ = 0;
    mutable int panel_hint_px_ = 0;
    mutable int panel_rows_px_ = 0;
    mutable QString panel_hint_text_;
    mutable QRectF title_hit_;
    mutable OverlayHit mixer_hit_;
    mutable OverlayHit picker_hit_;
    mutable OverlayHit settings_hit_;
    bool owns_screen_ = true;
    ColourLocks colour_locks_;
    std::size_t history_limit_for_new_screen_ = Screen::kMaxHistory;
    Screen::CursorShape default_cursor_shape_for_new_screen_ = Screen::CursorShape::Block;
    // The shape the config named, held by the window as well as by the screen. A lock
    // that lives only on the screen is a lock that goes away when a session is switched,
    // because switching builds a new screen; the colours have kept theirs in
    // `colour_locks_` for the same reason since 2026-11-20.
    std::optional<CursorStyle> cursor_style_lock_;
    // The cursor's blink: the half-period the file asked for, and which half we are in.
    // 0 is no blink at all, and then the timer never runs — a terminal that wakes twice
    // a second to paint nothing is a laptop that runs down.
    QTimer cursor_blink_timer_;
    // The state diary described at `writeStateDiary`: the timer, and the screen mode as
    // of the last line written, so a change of hands is worth a line of its own.
    QTimer state_diary_timer_;
    bool diary_alt_ = false;
    int cursor_blink_ms_ = 0;
    bool cursor_lit_ = true;
    bool closing_on_close_ = false;
    double padding_ = 10.0;
    // The line between two places, in pixels. One, because a divider's job is to say "here
    // one place stops and another starts" and a single line says that; the setting exists
    // because a person on a scaling display may want two.
    double divider_width_ = 1.0;
    // The colours this window wears. Defaults until a config file says
    // otherwise; see `applyTheme()`.
    Theme theme_;
    // The two alphas `ground()` and `ink()` paint with, and whether the surface
    // currently has an alpha channel at all. See `setTransparency()`.
    double background_opacity_ = 1.0;
    double foreground_opacity_ = 1.0;
    bool translucent_ = false;
    // The padding the current rows/cols were computed with. It is the memory of
    // whose screen this is — the shell's, inset, or a full-screen application's,
    // edge to edge — and the thing `syncGridToScreen()` compares against to
    // notice that the screen changed hands.
    double geometry_padding_ = padding_;
    // The session's tab bar. Zero panes is a window holding a shell started here, with
    // nothing to switch between; `tab_chosen_` says this window picked a place to look at,
    // after which the session's own focus no longer moves the lit tab.
    int tab_panes_ = 0;
    int tab_focused_ = 0;
    bool tab_chosen_ = false;
    // The place number this window asked the session to make and is waiting to be told is
    // there, or -1 when nothing is owed. -1 is also what a place chosen by hand leaves
    // behind: nobody is owed a move then.
    int pending_new_place_ = -1;
    // Whether the session's places are shown one at a time with tabs, or side by side.
    bool view_tabs_ = false;
    // Where each place sits, as the session last said. Empty until it says.
    std::vector<PaneRect> tab_cells_;
    // Which number each place is called by, in the order the places are listed; empty when
    // the session has not said, which means numbering them by position.
    std::vector<int> tab_numbers_;
    // The refusal currently on screen, and which one it is: a notice that has been replaced
    // must not be cleared by the timer belonging to the one before it.
    QString notice_;
    int notice_revision_ = 0;

    // Rows moved by one wheel notch, and how far the view is scrolled back from
    // the live bottom in lines. Zero means "following the shell".
    static constexpr int kWheelRows = 3;
    // Presses landing on the same cell inside this window are one gesture, the
    // same span Qt's double-click interval uses by default.
    static constexpr qint64 kDoubleClickMs = 400;
    int scroll_offset_ = 0;
    // How far each place is scrolled, kept across the window looking at another. The
    // place being looked at holds its offset in `scroll_offset_`; a place looked at from
    // a neighbour holds it in the neighbour's own view. This map is the memory both
    // write, so scrolling a place and then looking away, or looking away and back, keeps
    // it where it was rather than snapping to the prompt.
    std::map<int, int> pane_scroll_;
    // The scroll the place being looked at was left at, waiting for its snapshot. Moving
    // to a place feeds a fresh snapshot that lands at its live bottom; the offset saved
    // for that place cannot be applied until the history behind it is on the screen, so it
    // waits here and is applied by the first output chunk that fills the screen.
    int pending_view_scroll_ = 0;
    void clearSavedScroll(int pane) { pane_scroll_.erase(pane); }
    // The live place returns to the bottom: what it shows now is its own business, and
    // a saved scroll for it would be a memory of a screen that has moved on.
    void returnToBottom() {
        scroll_offset_ = 0;
        pane_scroll_.erase(tab_focused_);
    }

    // The indicator's geometry, recomputed whenever the screen is painted, and
    // the state of a drag on its thumb.
    ScrollBar scroll_bar_;
    bool scrollbar_drag_ = false;
    int scrollbar_grab_ = 0;

    // Auto-scroll while a selection drag is held against an edge: the timer that
    // crawls the view, which way it is crawling, and the column the pointer is
    // over so the selection grows sideways as well as down.
    static constexpr int kEdgeScrollMs = 60;
    QTimer* edge_timer_ = nullptr;
    int edge_direction_ = 0;
    int edge_col_ = 0;

    // Text selection in display coordinates, plus the drag state that builds it.
    enum class DragMode { Char, Word, Line };

    Selection selection_{};
    // Where the panes are, when a host has said. Empty means the screen is one pane.
    std::vector<PaneRect> panes_;
    bool dragging_ = false;
    DragMode drag_mode_ = DragMode::Char;
    CellPos anchor_{};
    // Repeated presses on one cell, for word and line selection.
    QElapsedTimer click_clock_;
    qint64 last_click_ms_ = 0;
    CellPos last_click_pos_{};
    int click_count_ = 0;
    // Last cell reported to the application, so motion is only sent when the
    // pointer moves onto a different character.
    CellPos last_reported_{};
    // Repaint pacing for ?2026, measured from the widget's own clock.
    QElapsedTimer repaint_clock_;
    qint64 last_repaint_ms_ = 0;

    qint64 repaintClockElapsed() {
        if (!repaint_clock_.isValid()) {
            repaint_clock_.start();
            return 0;
        }
        return repaint_clock_.elapsed();
    }

    void scrollBy(int lines) {
        const int view_top_before = viewTop();
        scroll_offset_ += lines;
        clampScroll();
        pane_scroll_[tab_focused_] = scroll_offset_;
        keepSelectionOnScreen(view_top_before);
        update();
    }

    // The absolute buffer line shown at the top of the window. Selections are
    // kept on their text by watching how far this moves.
    int viewTop() const { return screen_.historySize() - scroll_offset_; }

    // Slide a selection by however far the text underneath it moved, and drop
    // it once any part of it leaves the window. Called after output and after
    // scrolling, which are the two things that move content on screen.
    void keepSelectionOnScreen(int view_top_before) {
        const int shift = view_top_before - viewTop();
        if (shift == 0)
            return;
        if (selection_.empty()) {
            // A drag that has not produced a range yet still has an anchor, and it
            // has to travel with its text too — otherwise the moment the view
            // moves, the drag is left pointing at whatever moved into the cell it
            // started on.
            if (dragging_)
                anchor_.row = std::clamp(anchor_.row + shift, 0, rows_ - 1);
            return;
        }
        const int anchor_row = selection_.anchor.row + shift;
        const int head_row = selection_.head.row + shift;
        if (anchor_row < 0 || head_row < 0 || anchor_row >= rows_ || head_row >= rows_) {
            selection_ = Selection{};
            dragging_ = false;
            return;
        }
        selection_.anchor.row = anchor_row;
        selection_.head.row = head_row;
        // A drag that resumes after output has to extend the same text, so the
        // end it grew from has to move with it.
        anchor_.row += shift;
    }

    void clampScroll() {
        // Against what this view may scroll, not against what the model is holding: a
        // full-screen application's rows are not in this view's reach, and the offset
        // has to come back to zero when one takes the screen over.
        scroll_offset_ = std::clamp(scroll_offset_, 0, scrollRange());
    }

    // Hold a scrolled-back view on the same lines while new output pushes
    // history, the way xterm keeps the display still instead of jumping it.
    void followScroll(int history_before) {
        if (scroll_offset_ > 0)
            scroll_offset_ += screen_.historySize() - history_before;
        clampScroll();
    }

    // Display row r of a given screen, scrolled back by `scroll` lines, which may be
    // a scrollback line rather than a live screen row. Rows past the end of history
    // resolve to live screen rows so a partially scrolled view still shows the bottom.
    // Shared by the window's own view and every neighbour, so a place scrolled up in
    // one half shows the same kind of row as one scrolled up in the other.
    static const Row& rowToShow(const Screen& screen, int scroll, int r) {
        if (scroll == 0)
            return screen.row(r);
        const int idx = screen.historySize() - scroll + r;
        if (const Row* h = screen.historyRow(idx))
            return *h;
        const int live = idx - screen.historySize();
        if (live >= 0 && live < screen.rows())
            return screen.row(live);
        static const Row kEmptyRow;
        return kEmptyRow;
    }

    // Display row r of the window's own view, which may be a scrollback line rather
    // than a live screen row.
    const Row& displayRow(int r) const { return rowToShow(screen_, scroll_offset_, r); }

    // The place this window is looking at, in grid coordinates: the whole grid while
    // there is one place to look at, and the rect the session gave that place once
    // there are several beside each other.
    PaneRect focusedPane() const {
        if (tab_panes_ > 1 && tab_focused_ >= 0 &&
            tab_focused_ < static_cast<int>(tab_cells_.size()) &&
            tab_cells_[static_cast<size_t>(tab_focused_)].valid())
            return tab_cells_[static_cast<size_t>(tab_focused_)];
        return wholeGridPane(rows_, cols_);
    }

    // The row shown at a row of the grid. A drag, a copy and a double-click all ask
    // this rather than reading the screen directly: side by side, the place being looked
    // at owns a corner of the grid, so a row of the grid is a row of that place counted
    // from the rows above it — and its rows are its own width, not the window's.
    const Row& focusedRow(int grid_row) const {
        if (!splitActive())
            return displayRow(grid_row);
        const PaneRect pane = focusedPane();
        const int shown = std::min(pane.rows, screen_.rows());
        const int top = screen_.rows() - shown;
        return rowToShow(screen_, scroll_offset_, grid_row - pane.firstRow() + top);
    }

    // The cell covering a cell of the grid, in the coordinates a press reports. Reading
    // a place's own row at a grid column would read past its end for every place that
    // does not start at the left edge, which is how a drag in the right-hand place came
    // to find nothing there at all.
    Cell focusedCellAt(int grid_row, int grid_col) const {
        const Row& line = focusedRow(grid_row);
        const int col = grid_col - focusedPane().firstCol();
        if (col < 0 || col >= static_cast<int>(line.size()))
            return Cell{};
        return line[static_cast<std::size_t>(col)];
    }

    // How far a neighbour's own screen may be scrolled back, and the clamp that holds
    // it there. A neighbour in a full-screen program cannot scroll, exactly as the
    // window's own view cannot.
    static int neighbourScrollRange(const Screen& screen) {
        return screen.inAlternateScreen() ? 0 : screen.historySize();
    }
    static void clampNeighbourScroll(ExtraPane& view) {
        view.scroll = std::clamp(view.scroll, 0, neighbourScrollRange(view.screen));
    }

    // Cell size, derived the way the painter derives it, so hit testing and
    // painting agree about which character is under the cursor.
    //
    // One function answers "how wide is a cell in this font", because the grid,
    // the hit testing and the three panels all measure the same cell, and two
    // answers is how a panel gets painted at a width its own rows deny.
    static double cellAdvance(const QFontMetricsF& fm) {
        double cw = fm.horizontalAdvance(QChar('M'));
        if (cw <= 0.0)
            cw = fm.maxWidth();
        return cw;
    }

    double cellWidth() const { return cellAdvance(QFontMetricsF(font_)); }
    double cellHeight() const { return QFontMetricsF(font_).height(); }

    // Pixel position to grid cell, in display coordinates: the same row index
    // the painter used, so a selection made while scrolled up stays on the text
    // under the pointer.
    // Which place a grid position falls in, by the cells the session laid out. The
    // answer is a place's number, so a click says the same thing the tab bar says —
    // there is one arrangement and both the painter and the click read it.
    int paneAtGrid(int row, int col) const {
        for (int pane = 0; pane < static_cast<int>(tab_cells_.size()); ++pane) {
            const PaneRect& cell = tab_cells_[static_cast<size_t>(pane)];
            if (row >= cell.row && row <= cell.lastRow() && col >= cell.col && col < cell.endCol())
                return pane;
        }
        return -1;
    }

    CellPos cellAt(const QPointF& pos) const {
        const double cw = cellWidth();
        const double ch = cellHeight();
        if (cw <= 0.0 || ch <= 0.0)
            return {};
        // The same inset the painter used, so a click at the edge of a
        // full-screen application's frame lands on the character it is on.
        const double pad = activePadding();
        CellPos out;
        out.row = std::clamp(static_cast<int>((pos.y() - gridTop()) / ch), 0, rows_ - 1);
        // A click in the right-hand margin selects up to the end of the row,
        // which is column count, one past the last cell.
        out.col = std::clamp(static_cast<int>((pos.x() - pad) / cw), 0, cols_);
        return out;
    }

    static MouseKey mouseKey(Qt::MouseButton button) {
        switch (button) {
            case Qt::LeftButton: return MouseKey::Left;
            case Qt::MiddleButton: return MouseKey::Middle;
            case Qt::RightButton: return MouseKey::Right;
            default: return MouseKey::None;
        }
    }

    // Qt modifiers to the protocol's modifier bits.
    static int mouseMods(Qt::KeyboardModifiers mods) {
        int bits = 0;
        if (mods & Qt::ShiftModifier) bits |= MouseShift;
        if (mods & Qt::AltModifier) bits |= MouseAlt;
        if (mods & Qt::ControlModifier) bits |= MouseCtrl;
        return bits;
    }

    static unsigned heldBits(Qt::MouseButtons buttons) {
        unsigned bits = 0;
        if (buttons & Qt::LeftButton) bits |= mouseKeyBit(MouseKey::Left);
        if (buttons & Qt::MiddleButton) bits |= mouseKeyBit(MouseKey::Middle);
        if (buttons & Qt::RightButton) bits |= mouseKeyBit(MouseKey::Right);
        return bits;
    }

    std::string mouseBytes(const MouseReport& rep) const {
        return encodeMouse(rep, screen_.mouseMode(), screen_.mouseCoord());
    }

    // Write to the place this window is looking at — its own backend, and nobody
    // else's. A neighbour is a pair of eyes only: to type into another place you look
    // at it, which moves the focus here. So there is one target, and it is `backend_`.
    void writeToPty(const std::string& bytes) {
        if (bytes.empty())
            return;
        if (!shell_alive_ || !backend_->alive())
            return;
        static_cast<void>(backend_->write(bytes));
    }

    // Classification for word-wise selection. Word characters are ASCII letters,
    // digits and underscore, matching what shells use for word-wise movement;
    // anything else visible is selected one cell at a time.
    CellKind kindAt(int row, int col) const {
        const Cell cell = focusedCellAt(row, col);
        const std::string& text = cell.text;
        if (text.empty())
            return CellKind::Blank;
        const unsigned char c = static_cast<unsigned char>(text[0]);
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_';
        return word ? CellKind::Word : CellKind::Other;
    }

    // Count a press that lands on the same cell shortly after the previous one
    // as another click of the same gesture. Returns 1 for a fresh press.
    int countClick(const CellPos& pos) {
        if (!click_clock_.isValid()) {
            click_clock_.start();
            click_count_ = 1;
        } else {
            const bool same_cell =
                last_click_pos_.row == pos.row && last_click_pos_.col == pos.col;
            const bool quick = click_clock_.elapsed() - last_click_ms_ <= kDoubleClickMs;
            click_count_ = (same_cell && quick) ? click_count_ + 1 : 1;
        }
        last_click_pos_ = pos;
        last_click_ms_ = click_clock_.elapsed();
        return click_count_;
    }

    // Grow the selection to `to` the way the gesture that started it wants:
    // character by character, whole words, or whole rows. Wherever it wants to go, it
    // stops at the edge of the pane the drag started in.
    void extendSelection(const CellPos& to) {
        const PaneRect pane = paneForDrag(panes_, anchor_, rows_, cols_);
        const CellPos inside{pane.clampRow(to.row), pane.clampEndCol(to.col)};
        switch (drag_mode_) {
            case DragMode::Char:
                selection_ = Selection{anchor_, inside};
                break;
            case DragMode::Word:
                selectWord(anchor_, inside, pane);
                break;
            case DragMode::Line:
                // A whole row means the row of the pane, not of the screen: the line
                // next door belongs to a different shell.
                selection_ = Selection{CellPos{anchor_.row, pane.firstCol()},
                                       CellPos{inside.row, pane.endCol()}};
                break;
        }
    }

    // Snap both ends of a drag to word boundaries so dragging across a path
    // selects whole words at both ends.
    void selectWord(const CellPos& from, const CellPos& to, const PaneRect& pane) {
        int from_start = from.col, from_end = from.col;
        int to_start = to.col, to_end = to.col;
        expandToWord(cols_, from.col, [&](int c) { return kindAt(from.row, c); },
                     from_start, from_end);
        expandToWord(cols_, to.col, [&](int c) { return kindAt(to.row, c); }, to_start, to_end);

        // A word that touches the boundary is still only ours up to the boundary:
        // where a neighbour's text begins is its business.
        from_start = std::max(pane.clampEndCol(from_start), pane.firstCol());
        from_end = pane.clampEndCol(from_end);
        to_start = std::max(pane.clampEndCol(to_start), pane.firstCol());
        to_end = pane.clampEndCol(to_end);

        if (from.row <= to.row)
            selection_ = Selection{CellPos{from.row, from_start}, CellPos{to.row, to_end}};
        else
            selection_ = Selection{CellPos{from.row, from_end}, CellPos{to.row, to_start}};
    }

    void clearSelection() {
        if (selection_.empty() && !dragging_)
            return;
        selection_ = Selection{};
        dragging_ = false;
        update();
    }

    // Copy on demand: Ctrl+Shift+C. Both modes are set so a middle click in
    // another application and a Ctrl+Shift+V in this one both find the same text.
    void copySelection() {
        const std::string text = selectedText();
        if (text.empty())
            return;
        QClipboard* clip = QGuiApplication::clipboard();
        if (!clip)
            return;
        const QString qtext = QString::fromStdString(text);
        clip->setText(qtext, QClipboard::Clipboard);
        if (clip->supportsSelection())
            clip->setText(qtext, QClipboard::Selection);
    }

    void pasteFromClipboard() {
        QClipboard* clip = QGuiApplication::clipboard();
        if (!clip)
            return;
        QString text;
        if (clip->supportsSelection())
            text = clip->text(QClipboard::Selection);
        if (text.isEmpty())
            text = clip->text(QClipboard::Clipboard);
        pasteText(text.toStdString());
    }
};

} // namespace kai
