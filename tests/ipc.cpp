// Headless check of the daemon protocol: framing, field escaping, record lists.
// No sockets and no processes here — see tests/session.cpp for those.

#include "../src/ipc/protocol.hpp"

#include <cstdio>
#include <iostream>
#include <string>

using namespace kai;

namespace {

int checks = 0;
int failures = 0;

void expect(const char* what, bool value) {
    ++checks;
    if (value)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << "\n";
}

void expectStr(const char* what, const std::string& got, const std::string& want) {
    ++checks;
    if (got == want)
        return;
    ++failures;
    std::cerr << "FAIL: " << what << ": want [" << want << "] got [" << got << "]\n";
}

} // namespace

int main() {
    // A frame carries its kind and its body unchanged, including bytes that look
    // like framing to a naiver reader.
    {
        const std::string bytes = encodeFrame(MsgKind::Data, "hello \x1b[31mworld");
        FrameReader reader;
        reader.append(bytes);
        Frame frame;
        expect("one append yields one frame", reader.pop(frame));
        expect("with the kind intact", frame.kind == MsgKind::Data);
        expectStr("and the body intact", frame.body, "hello \x1b[31mworld");
        expect("and nothing left over", !reader.pop(frame));
    }

    // A socket delivers no respect for message boundaries. Feeding one byte at a
    // time is the worst case a reader can be put through.
    {
        const std::string a = encodeFrame(MsgKind::Hello, "name=work\n");
        const std::string b = encodeFrame(MsgKind::Resize, "rows=40\ncols=120\n");
        FrameReader reader;
        int frames = 0;
        Frame frame;
        const std::string both = a + b;
        for (char c : both) {
            reader.append(std::string_view(&c, 1));
            while (reader.pop(frame))
                ++frames;
        }
        expect("both frames survive a byte-at-a-time stream", frames == 2);
        expect("and the reader reports no error", !reader.error());
    }

    // Two frames in one read is the common case, not an edge: a daemon writes a
    // snapshot and then output, and the kernel hands both over together.
    {
        FrameReader reader;
        reader.append(encodeFrame(MsgKind::Ack, "what=list\n") +
                      encodeFrame(MsgKind::SessionList, ""));
        Frame frame;
        expect("first of two frames in one read", reader.pop(frame));
        expect("is an Ack", frame.kind == MsgKind::Ack);
        expect("second of two frames in one read", reader.pop(frame));
        expect("is a session list", frame.kind == MsgKind::SessionList);
    }

    // A length that cannot be a message means the stream is not aligned any more.
    // The reader has to say so once and keep saying it, rather than resynchronising
    // on bytes that happen to look like a header.
    {
        FrameReader reader;
        const std::string whole = encodeFrame(MsgKind::Data, std::string(64, 'x'));
        reader.append(whole.substr(0, 3));
        Frame frame;
        expect("a partial header is not a frame", !reader.pop(frame));
        expect("and the reader waits instead of failing", !reader.error());
        expect("with the bytes kept", reader.buffered() == 3);
        reader.append(std::string_view(whole).substr(3));
        expect("the completed frame arrives", reader.pop(frame));
        expect("still carrying the whole body", frame.body.size() == 64);
        expect("with nothing left buffered", reader.buffered() == 0);
    }
    {
        FrameReader reader;
        std::string bad;
        const std::uint32_t too_big = static_cast<std::uint32_t>(kMaxFrameBytes) + 1;
        bad.push_back(static_cast<char>((too_big >> 24) & 0xFF));
        bad.push_back(static_cast<char>((too_big >> 16) & 0xFF));
        bad.push_back(static_cast<char>((too_big >> 8) & 0xFF));
        bad.push_back(static_cast<char>(too_big & 0xFF));
        bad.push_back(static_cast<char>(MsgKind::Data));
        reader.append(bad);
        Frame frame;
        expect("an oversized length is refused", !reader.pop(frame));
        expect("and the reader knows the stream is broken", reader.error());
        // Even a complete good frame after it is not trusted.
        reader.append(encodeFrame(MsgKind::Ack, ""));
        expect("and stays broken", !reader.pop(frame));
    }
    {
        // A length of zero would ask the reader to read a kind byte from a body of
        // negative size.
        FrameReader reader;
        reader.append(std::string("\0\0\0\0\x02", 5));
        Frame frame;
        expect("a zero length is refused", !reader.pop(frame));
        expect("with the stream marked broken", reader.error());
    }

    // Field values are free text: a shell path, a working directory, a session name
    // with a space in it. None of them may be able to forge a second line.
    {
        Fields fields{
            {"shell", "/bin/bash"},
            {"cwd", "/home/kaida/Project/Kai-Term"},
            {"name", "work \x7ftab"},
            {"quoted", "a\\nb"},
            {"equals", "rows=24"},
            {"newline", "first\nsecond"},
            {"carriage", "tick\r"},
            {"utf8", "\xc3\xa9\xe2\x94\x82 \xf0\x9f\x96\xa5"},
            {"empty", ""},
        };
        const std::string encoded = encodeFields(fields);
        const Fields back = decodeFields(encoded);
        expect("every field comes back", back.size() == fields.size());
        for (std::size_t i = 0; i < fields.size() && i < back.size(); ++i) {
            if (back[i].first != fields[i].first || back[i].second != fields[i].second) {
                std::cerr << "FAIL: field " << i << " changed: [" << fields[i].first << "="
                          << fields[i].second << "] became [" << back[i].first << "="
                          << back[i].second << "]\n";
                ++failures;
            }
            ++checks;
        }
        expect("a forged line stays inside its value",
               encodeFields({{"a", "x\nb=evil"}}).find("\nb=evil") == std::string::npos);
    }

    {
        const Fields fields = decodeFields("name=work\n\nnovalue\n=novalue\nrows=40\n");
        expectStr("junk lines are skipped without losing the good ones",
                  fieldValue(fields, "rows"), "40");
        expectStr("and the first field is still there", fieldValue(fields, "name"), "work");
        expectStr("a missing field gives the fallback",
                  fieldValue(fields, "absent", "none"), "none");
    }

    // A session list is several records in one body, each with its own fields.
    {
        const std::vector<Fields> records = {
            {{"name", "work"}, {"rows", "40"}, {"cols", "120"}, {"state", "running"}},
            {{"name", "build"}, {"rows", "24"}, {"cols", "80"}, {"state", "exited"}},
            {{"name", "odd"}, {"note", "a\nb"}, {"state", "running"}},
        };
        const std::vector<Fields> back = decodeRecords(encodeRecords(records));
        expect("all three sessions come back", back.size() == 3);
        if (back.size() == 3) {
            expectStr("second session keeps its name", fieldValue(back[1], "name"), "build");
            expectStr("second session keeps its columns", fieldValue(back[1], "cols"), "80");
            expectStr("third session's newline survives", fieldValue(back[2], "note"), "a\nb");
        }
        expect("an empty list decodes as no records", decodeRecords(encodeRecords({})).empty());
        // The phantom trailing record this used to produce is worth its own check:
        // a session list with one entry would otherwise read as two.
        expect("one session decodes as one record",
               decodeRecords(encodeRecords({{{"name", "solo"}}})).size() == 1);
        {
            const std::vector<Fields> with_hole = {
                {{"name", "first"}}, Fields{}, {{"name", "last"}}
            };
            const std::vector<Fields> back = decodeRecords(encodeRecords(with_hole));
            expect("a record with no fields is still a record", back.size() == 3);
            expect("and the names land on the right records",
                   back.size() == 3 && fieldValue(back[0], "name") == "first" &&
                       back[1].empty() && fieldValue(back[2], "name") == "last");
        }
    }

    if (failures == 0) {
        std::cout << "PASS: " << checks << " protocol framing and field checks passed\n";
        return 0;
    }
    std::cerr << failures << " of " << checks << " protocol checks failed\n";
    return 1;
}
