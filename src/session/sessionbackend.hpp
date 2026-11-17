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
    }

    int descriptor() const override { return client_.descriptor(); }

    // Blocks until there is something to show, which is what the widget's reader
    // thread expects of a terminal. A frame that is not output — a copy request, say —
    // is not the end of the session, and returning nothing here would be read as one.
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
                copies_.push_back(request);
                break;
            }
            case MsgKind::Exit: {
                const Fields fields = decodeFields(frame.body);
                exit_code_ = std::stoi(fieldValue(fields, "code", "-1"));
                ended_ = true;
                break;
            }
            case MsgKind::Error:
                ended_ = true;
                break;
            default:
                break;
        }
    }

    std::string name_;
    DaemonClient client_;
    std::string pending_;
    std::vector<Screen::ClipboardRequest> copies_;
    bool ended_ = false;
    int exit_code_ = -1;
};

} // namespace kai
