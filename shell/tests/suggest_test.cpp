// Word suggestions: which word is being typed, what is offered for it, and
// what is learned. Build and run: shell/tests/run.sh
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>

#include <unistd.h>

#include "ui/suggest.h"

using namespace zs;

namespace {
int g_failed = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failed;
    }
}

std::string before(const char* text) { return suggest::word_before(text, std::strlen(text)); }

// The first suggestion for a prefix, or "".
std::string first(const char* prefix) {
    std::string out[suggest::kMaxResults];
    return suggest::lookup(prefix, out) ? out[0] : "";
}
}  // namespace

int main() {
    // The built-in list: lower-case words of 4 to kMaxWord letters, no repeats.
    {
        std::set<std::string> seen;
        bool ok = true;
        for (const char* w = suggest::kBuiltinWords; *w;) {
            const char* end = std::strchr(w, ' ');
            if (!end) { ok = false; break; }
            std::string word(w, size_t(end - w));
            if (word.size() < suggest::kMinLearn || word.size() > suggest::kMaxWord) ok = false;
            for (char ch : word)
                if (!((ch >= 'a' && ch <= 'z') || ch == '\'')) ok = false;
            if (!seen.insert(word).second) ok = false;
            w = end + 1;
        }
        expect(ok, "built-in list is well formed");
        expect(seen.size() > 500, "built-in list has words");
        expect(std::strlen(suggest::kBuiltinWords) < 16 * 1024, "built-in list stays small");
    }

    // Which word is being typed.
    expect(before("hel") == "hel", "a word at the start");
    expect(before("say hel") == "hel", "a word after a space");
    expect(before("line\nhel") == "hel", "a word after a new line");
    expect(before("(hel") == "hel", "a word after a bracket");
    expect(before("don'") == "don'", "an apostrophe inside a word");
    expect(before("say 'hel") == "hel", "a quote before a word");
    expect(before("hello ") == "", "nothing after a space");
    expect(before("") == "", "nothing in an empty box");
    expect(before("192.168.we") == "", "not in an address");
    expect(before("me@exam") == "", "not in an email address");
    expect(before("/home/pi/Doc") == "", "not in a path");
    expect(before("--verb") == "", "not in a command option");
    expect(before("wifi5gh") == "", "not after a digit");
    expect(suggest::word_before("hello", 3) == "", "not in the middle of a word");
    expect(suggest::word_before("hel lo", 3) == "hel", "at the end of a word, text after it");

    // What is offered.
    {
        std::string out[suggest::kMaxResults];
        expect(suggest::lookup("t", out) == 0, "one letter offers nothing");
        expect(suggest::lookup("zzq", out) == 0, "no word, nothing offered");
        int n = suggest::lookup("th", out);
        expect(n == suggest::kMaxResults, "a common start fills the bar");
        expect(out[0] == "that", "the most common word is first");
        expect(out[0] != out[1] && out[1] != out[2] && out[0] != out[2], "no word twice");
        expect(suggest::lookup("that", out) >= 1 && out[0] == "that's", "a whole word offers only longer ones");
        expect(first("TH") == "that" && first("Th") == "that", "capitals find the same words");
    }

    // What Tab types.
    expect(suggest::remainder("th", "that") == "at", "the rest of the word");
    expect(suggest::remainder("Th", "that") == "at", "a capital first letter stays as typed");
    expect(suggest::remainder("TH", "that") == "AT", "all capitals stay all capitals");
    expect(suggest::remainder("that", "that") == "", "nothing left to type");

    // Learning.
    suggest::load("/nonexistent/zeroos-suggest-test");
    expect(suggest::learned_count() == 0 && !suggest::dirty(), "no file, nothing learned");
    suggest::learn("the");          // too short
    suggest::learn("r2d2");         // not a word
    suggest::learn("hunter2");
    expect(suggest::learned_count() == 0, "short words and non-words are not learned");
    suggest::learn("Meshtastic");
    expect(suggest::learned_count() == 1 && suggest::dirty(), "a new word is kept");
    expect(first("mesh") == "", "a word typed once is not offered yet");
    suggest::learn("meshtastic");
    expect(first("mesh") == "meshtastic", "typed twice, it is offered");
    expect(first("me") == "meshtastic", "the owner's words come before the built-in ones");
    suggest::learn("thermal");
    suggest::learn("thermal");
    suggest::learn("thermal");
    suggest::learn("these");
    suggest::learn("these");
    {
        std::string out[suggest::kMaxResults];
        int n = suggest::lookup("th", out);
        expect(n == 3 && out[0] == "thermal" && out[1] == "these" && out[2] == "that",
               "most typed first, then the built-in list, no repeats");
    }

    // Saved and read back.
    char tmpl[] = "/tmp/zs-suggest-XXXXXX";
    expect(mkdtemp(tmpl) != nullptr, "temp dir");
    std::string path = std::string(tmpl) + "/words.learned";
    suggest::load(path);
    suggest::learn("solder");
    suggest::learn("solder");
    expect(suggest::save() && !suggest::dirty(), "saved");
    expect(!suggest::save(), "nothing changed, nothing written");
    suggest::load(path);
    expect(suggest::learned_count() == 1 && first("so") == "solder", "read back");
    // A damaged or hand-edited file can't put rubbish in the bar.
    { std::ofstream(path) << "solder 3\nBAD 4\nab 9\nsolder 7\npass123 5\nresistor 2\n"; }
    suggest::load(path);
    expect(suggest::learned_count() == 2 && first("res") == "resistor", "bad lines are skipped");

    // Full: the least typed, oldest word makes room.
    suggest::load(path);
    for (int i = 0; i < suggest::kMaxLearned + 20; ++i) {
        std::string w = "word";
        w += char('a' + i / 26 % 26);
        w += char('a' + i % 26);
        suggest::learn(w);
    }
    expect(suggest::learned_count() == suggest::kMaxLearned, "the learned list is capped");
    expect(first("res") == "resistor" && first("so") == "solder", "words typed often stay");

    suggest::forget();
    expect(suggest::learned_count() == 0 && first("res") == "rest", "forgotten: built-in words only");
    expect(suggest::save(), "forgetting empties the file");
    suggest::load(path);
    expect(suggest::learned_count() == 0, "and it stays forgotten");
    std::remove(path.c_str());
    rmdir(tmpl);

    if (g_failed) return 1;
    std::printf("suggest_test: ok\n");
    return 0;
}
