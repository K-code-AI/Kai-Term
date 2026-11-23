#pragma once

// A window's view of a session owned by the daemon.
//
// To the terminal widget this is indistinguishable from a shell started here: bytes
// come in, keystrokes go out, the size follows the window. What is different is what
// happens at the edges. The first screen is not empty — the daemon sends what is on it
// now, so joining a session that has been running since this morning shows this
// morning. The program inside is not a child of this process, so its leaving arrives
// as a message rather than a wait status. And queries about the terminal are answered
// by the daemon, which is the one place that has a model whether or not anyone is
// looking; answering here as well would give the program two replies.

#include <poll.h>
#include <cstdio>

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "../ipc/protocol.hpp"
#include "../terminal/backend.hpp"
#include "daemon.hpp"

namespace kai {

class SessionBackend final : public TerminalBackend {
public:
    SessionBackend(std::string name, int rows, int cols) : name_(std::move(name)) {
        std::string error;
        if (!client_.connect(error))
            throw std::runtime_error(error);
        if (!client_.request({{"command", "attach"},
                              {"name", name_},
                              {"rows", std::to_string(rows)},
                              {"cols", std::to_string(cols)}})) {
            throw std::runtime_error("the daemon would not listen");
        }
        Frame welcome;
        if (!client_.nextFrame(welcome, error))
            throw std::runtime_error(error.empty() ? "the daemon said nothing" : error);
        if (welcome.kind != MsgKind::Welcome) {
            throw std::runtime_error(fieldValue(decodeFields(welcome.body), "message",
                                                "the daemon did not welcome us"));
        }
        // The screen itself, taken here rather than left for the reader thread. The
        // daemon answers an attach with the Welcome frame and the screen back to back, so
        // waiting for the second one is not a round trip but a look at what is already in
        // the socket — and taking it now means a window has the session's screen *in hand*
        // before anybody tries to draw it. Left in the socket it waits on a reader thread
        // posting events to a widget that may not have been shown yet, which is the blank
        // window: content arriving into a view that was never asked to paint it, filling in
        // on the first keystroke. A kept session whose screen is empty gets no frame at
        // all, so the wait is bounded rather than endless.
        constexpr int kSnapshotWaitMs = 250;
        constexpr int kHandshakeFrames = 64;
        for (int i = 0; i < kHandshakeFrames; ++i) {
            // The welcome read can leave the screen in the reader's own buffer; polling
            // the socket first would then see nothing and return before taking it.
            Frame frame;
            // The welcome read can leave the screen in the client's own buffer; polling
            // the socket first would then see nothing and return before taking it.
            if (!client_.pop(frame)) {
                pollfd ready{client_.descriptor(), POLLIN, 0};
                if (::poll(&ready, 1, kSnapshotWaitMs) <= 0 || (ready.revents & POLLIN) == 0)
                    return;
                if (!client_.nextFrame(frame, error))
                    return;
            }
            // A program that never stops printing puts its own output between the Welcome
            // and the screen, so the wait is for the screen and not for one frame — and
            // what arrives first is kept, not thrown away to make the count come out.
            if (frame.kind == MsgKind::Data)
                pending_ += frame.body;
            if (frame.kind == MsgKind::Snapshot) {
                pending_ += frame.body;
                return;
            }
        }
    }

    int descriptor() const override { return client_.descriptor(); }

    // Blocks until there is something to show, which is what the widget's reader
    // thread expects of a terminal. A frame that is not output — a copy request, say —
    // is not the end of the session, and returning nothing here would be read as one.
    // The attach handshake leaves the session's current screen here, and no further
    // frame has to arrive for it to be wanted.
    bool hasBuffered() const override { return !pending_.empty(); }
    std::string heldBytes() const override { return pending_; }
    void clearHeldBytes() override { pending_.clear(); }

    ssize_t read(char* buf, std::size_t n) override {
        for (;;) {
            if (!pending_.empty()) {
                const std::size_t take = std::min(pending_.size(), n);
                std::memcpy(buf, pending_.data(), take);
                pending_.erase(0, take);
                return static_cast<ssize_t>(take);
            }
            if (ended_)
                return 0;
            // Something the window asks about has arrived, and there is nothing to read.
            // Sitting here waiting for bytes would keep it from the window until the shell
            // next talked — and a session at an idle prompt never talks. A refusal that
            // appears only once something else happens is a refusal that appears never.
            if (!refusal_.empty() || tabs_changed_)
                return kNothingRightNow;
            bool hung_up = false;
            if (!client_.readNow(hung_up)) {
                ended_ = true;
                return -1;
            }
            Frame frame;
            while (client_.pop(frame))
                ingest(frame);
            if (hung_up) {
                ended_ = true;
                return 0;
            }
        }
    }

    bool write(std::string_view bytes) override {
        return client_.send(MsgKind::Data, std::string(bytes));
    }

    void resize(int rows, int cols) override {
        static_cast<void>(client_.send(MsgKind::Resize,
                                       encodeFields({{"rows", std::to_string(rows)},
                                                     {"cols", std::to_string(cols)}})));
    }

    bool alive() const override { return !ended_; }
    bool answersQueries() const override { return false; }

    bool takeTabBar(TabBar& bar) override {
        if (!tabs_changed_)
            return false;
        tabs_changed_ = false;
        bar = tabs_;
        return true;
    }

    // Asking to be shown a pane is asking the daemon, which is where the panes are; this
    // window holds one screen and no notion of what the others contain.
    // Making, ending, and re-looking-at places: all asked of the daemon, which is where the
    // places are. The answer comes back on the tab bar, so this window finds out the same
    // way every other window on the session does.
    void newPane() override { static_cast<void>(client_.send(MsgKind::NewPane)); }
    void closePane() override { static_cast<void>(client_.send(MsgKind::ClosePane)); }
    void setViewMode(bool tabs) override {
        static_cast<void>(client_.send(
            MsgKind::SetViewMode, encodeFields({{"mode", tabs ? "tabs" : "side-by-side"}})));
    }

    // The refusal, once. Taken rather than read because a message somebody has already
    // seen should not be shown again on the next look.
    bool takeRefusal(std::string& out) override {
        if (refusal_.empty())
            return false;
        out = std::exchange(refusal_, std::string {});
        return true;
    }

    void showPane(int pane) override {
        static_cast<void>(client_.send(MsgKind::ShowPane,
                                       encodeFields({{"pane", std::to_string(pane)}})));
    }

    std::vector<Screen::ClipboardRequest> takeClipboardRequests() override {
        std::vector<Screen::ClipboardRequest> out;
        out.swap(copies_);
        return out;
    }

    // Hanging up on the socket is what wakes a thread blocked in read() here, so that
    // a window can be closed while its reader is waiting for output that the session
    // has no reason to send.
    void stopProcess(bool closing) override {
        // Detaching means "stop showing this to me"; detaching while closing means
        // "this window is going and the shell was not kept". The daemon keeps the
        // shell in the first case and ends it in the second, and only hears about the
        // difference when it is told.
        static_cast<void>(client_.send(MsgKind::Detach,
                                       closing ? encodeFields({{"discard", "1"}})
                                               : std::string()));
        ::shutdown(client_.descriptor(), SHUT_RDWR);
    }

    void release() override { client_.close(); }

    const std::string& sessionName() const { return name_; }
    // The session's own exit status, once it has said goodbye. -1 until then.
    int exitCode() const { return exit_code_; }

private:
    // A number off the wire, with anything that is not one read as zero: a tab bar that
    // cannot be counted is drawn as no tabs, which is the screen from before there were
    // any tabs to draw.
    static int number(const std::string& text) {
        if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
            return 0;
        return std::atoi(text.c_str());
    }

    void ingest(const Frame& frame) {
        switch (frame.kind) {
            case MsgKind::Data:
            case MsgKind::Snapshot:
                pending_ += frame.body;
                break;
            case MsgKind::Clipboard: {
                const Fields fields = decodeFields(frame.body);
                Screen::ClipboardRequest request;
                request.text = fieldValue(fields, "text");
                request.primary = fieldValue(fields, "primary") == "1";
                request.query = fieldValue(fields, "query") == "1";
                copies_.push_back(request);
                break;
            }
            case MsgKind::Tabs: {
                // The bar a window draws comes from here rather than from what the window
                // last saw, because a pane can appear while this window is open.
                const Fields fields = decodeFields(frame.body);
                tabs_.panes = number(fieldValue(fields, "panes", "0"));
                tabs_.focused = number(fieldValue(fields, "focused", "0"));
                tabs_.tabs = fieldValue(fields, "mode") == "tabs";
                tabs_.cells.clear();
                // `top,left,rows,cols` apart, in the order the places are listed. Anything
                // that does not parse is left out rather than guessed at: a bar drawn from
                // half-understood sizes is a map with rooms in the wrong places.
                {
                    const std::string text = fieldValue(fields, "cells");
                    std::size_t at = 0;
                    while (at < text.size()) {
                        const std::size_t next = text.find(';', at);
                        const std::string one =
                            text.substr(at, next == std::string::npos ? std::string::npos
                                                                      : next - at);
                        at = next == std::string::npos ? text.size() : next + 1;
                        std::size_t mark = 0;
                        PaneRect cell;
                        int* const parts[4] = {&cell.row, &cell.col, &cell.rows, &cell.cols};
                        bool read = true;
                        for (int* part : parts) {
                            const std::size_t comma = one.find(',', mark);
                            const std::string piece = one.substr(
                                mark, comma == std::string::npos ? std::string::npos
                                                                 : comma - mark);
                            if (comma == std::string::npos && mark + 1 < one.size())
                                read = false;
                            *part = number(piece);
                            mark = comma == std::string::npos ? one.size() : comma + 1;
                        }
                        if (read && cell.valid())
                            tabs_.cells.push_back(cell);
                    }
                }
                tabs_changed_ = true;
                break;
            }
            case MsgKind::Exit: {
                const Fields fields = decodeFields(frame.body);
                exit_code_ = std::stoi(fieldValue(fields, "code", "-1"));
                ended_ = true;
                break;
            }
            case MsgKind::Error:
                // A refusal, not an ending. The session is still there and everything
                // running in it is too; what failed was the asking. This used to say
                // "ended", which closed a window over a "no" — and a window closing because
                // it was refused takes the session down with it when nothing else is looking
                // at it. Kept aside to be said out loud instead.
                refusal_ = fieldValue(decodeFields(frame.body), "message",
                                      "the session said no");
                break;
            default:
                break;
        }
    }

    std::string name_;
    DaemonClient client_;
    std::string pending_;
    TabBar tabs_;
    bool tabs_changed_ = false;
    std::vector<Screen::ClipboardRequest> copies_;
    bool ended_ = false;
    // The last thing the session refused to do, in its own words.
    std::string refusal_;
    int exit_code_ = -1;
};

} // namespace kai
