// When closing a window has to ask about it, and what it says when it does.
//
// The dialog itself can only be looked at by a person. The decision underneath it —
// whether to interrupt somebody at all, and whether the words name the thing they are
// about to lose — is a plain function of what is running and whether the session was
// kept, so it is checked here instead.

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
               closeQuestion("", false).empty());
        expect("neither is a kept session with something running",
               closeQuestion("make", true).empty());
        expect("something running in a shell nobody kept is worth asking about",
               !closeQuestion("make", false).empty());
    }
    {
        const std::string question = closeQuestion("sleep", false);
        expect("the question says what is running", question.find("sleep") != std::string::npos);
        expect("it asks about quitting, not about the session",
               question.rfind("Quit while ", 0) == 0);
        expect("and it is a question", question.back() == '?');
        expect("an empty program name never produces one", closeQuestion("", false).empty());
    }
    {
        // The second line is where the two ways of quitting differ, so it has to say
        // which is which and what the kept one is called.
        const std::string with_session = closeDetail("w3");
        expect("a window holding a session names it", with_session.find("w3") != std::string::npos);
        expect("and says detaching keeps it running",
               with_session.find("Detach keeps it running") != std::string::npos);
        const std::string plain = closeDetail("");
        expect("a window with a shell of its own says what ending it loses",
               plain.find("ends the shell") != std::string::npos);
        expect("and does not mention a session it does not have",
               plain.find("Detach") == std::string::npos);
    }
    {
        expect("detaching is offered when there is a session to detach",
               closeOfferDetach("w3"));
        expect("and not when the window owns its shell", !closeOfferDetach(""));
        expect("nor when there is no name to come back to", !closeOfferDetach(""));
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " close prompt checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " close prompt checks failed\n";
    return 1;
}
