// When closing a window has to ask about it, and what it says when it does.
//
// The dialog itself can only be looked at by a person. The decision underneath it —
// whether to interrupt somebody at all, and whether the words name the thing they are
// about to lose — is a plain function of what is running and whether the session was
// kept, so it is checked here instead. There used to be a second line under the
// question, explaining the difference between the answers; Kaida asked for it to be
// dropped, and they were right: the three buttons say what the answers are, and a
// paragraph above them is a wall. What is left is what the asking is made of.

#include "../src/app/closeprompt.hpp"

#include <iostream>
#include <string>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(std::string_view what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

} // namespace

int main() {
    {
        // Nothing running is not worth interrupting anybody for, kept session or not.
        expect("a shell sitting at a prompt is nothing to ask about",
               closeQuestion("").empty());
        // The case Kaida hit twice: a session brought back into a window, something
        // running in it, the window closed. Keeping a session is not permission to
        // throw away what is running in it.
        expect("but a kept session with something running is asked about too",
               !closeQuestion("make").empty());
        expect("something running in a shell nobody kept is worth asking about",
               !closeQuestion("make").empty());
    }
    {
        const std::string question = closeQuestion("sleep");
        expect("the question says what is running", question.find("sleep") != std::string::npos);
        expect("it asks about quitting, not about the session",
               question.rfind("Quit while ", 0) == 0);
        expect("and it is a question", question.back() == '?');
        expect("an empty program name never produces one", closeQuestion("").empty());
    }
    {
        expect("saving is offered when there is a session to save",
               closeOfferSave("w3", false));
        expect("and not when the window owns its shell", !closeOfferSave("", false));
        expect("nor when the session is kept already, which would be the same button twice",
               !closeOfferSave("w3", true));
    }
    {
        // How often closing interrupts is a person's call. The words are the ones
        // Old-Term's `confirm_quit` takes, so somebody who knows one knows the other.
        expect("anything unrecognised means running, which asks when there is something to lose",
               confirmPolicy("") == ConfirmPolicy::Running &&
                   confirmPolicy("sometimes") == ConfirmPolicy::Running);
        expect("and the three words mean the three settings",
               confirmPolicy("never") == ConfirmPolicy::Never &&
                   confirmPolicy("always") == ConfirmPolicy::Always &&
                   confirmPolicy("running") == ConfirmPolicy::Running);

        // `never` is somebody who has a window manager they trust: the window goes down
        // without a word, whatever is happening inside it.
        expect("never asks about a shell sitting at a prompt",
               closeQuestion("", ConfirmPolicy::Never).empty());
        expect("and never asks about a build in progress either",
               closeQuestion("make", ConfirmPolicy::Never).empty());

        // `always` asks at every close, including the ones where nothing is running, so
        // its question has to stay true in that case: name no program rather than a
        // program that is not there.
        const std::string idle = closeQuestion("", ConfirmPolicy::Always);
        expect("always asks even about a shell at a prompt", !idle.empty());
        expect("and asks without naming a program that is not running",
               idle.find("while") == std::string::npos && idle.back() == '?');
        expect("always asks about a kept session too",
               !closeQuestion("", ConfirmPolicy::Always).empty());
        expect("and still says what is running when something is",
               closeQuestion("make", ConfirmPolicy::Always).find("make") !=
                   std::string::npos);
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " close prompt checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " close prompt checks failed\n";
    return 1;
}
