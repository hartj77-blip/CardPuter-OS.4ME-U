#include "ui/suggest.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ui/fileio.h"

namespace zs::suggest {
namespace {

struct Learned {
    char word[kMaxWord + 1];
    uint16_t count;
};

std::vector<Learned> g_learned;   // oldest first
std::string g_path;
bool g_dirty = false;

bool is_letter(char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); }
bool is_word_char(char ch) { return is_letter(ch) || ch == '\''; }
char lower(char ch) { return ch >= 'A' && ch <= 'Z' ? char(ch - 'A' + 'a') : ch; }
char upper(char ch) { return ch >= 'a' && ch <= 'z' ? char(ch - 'a' + 'A') : ch; }

std::string lowered(const std::string& s) {
    std::string out = s;
    for (char& ch : out) ch = lower(ch);
    return out;
}

// A plain word worth keeping: letters and apostrophes, starting with a letter.
bool learnable(const std::string& w) {
    if (w.size() < kMinLearn || w.size() > kMaxWord || !is_letter(w[0])) return false;
    for (char ch : w)
        if (!is_word_char(ch)) return false;
    return true;
}

Learned* find(const std::string& w) {
    for (Learned& l : g_learned)
        if (w == l.word) return &l;
    return nullptr;
}

bool has(const std::string out[], int n, const char* w, size_t len) {
    for (int i = 0; i < n; ++i)
        if (out[i].size() == len && std::memcmp(out[i].data(), w, len) == 0) return true;
    return false;
}

}  // namespace

std::string word_before(const char* text, size_t cursor) {
    if (!text || cursor == 0) return "";
    if (is_word_char(text[cursor])) return "";   // in the middle of a word
    size_t start = cursor;
    while (start > 0 && is_word_char(text[start - 1])) --start;
    // Only after a gap: "192.168.x", "user@host", "/home/pi" and "--help"
    // are not words to finish.
    if (start > 0) {
        char before = text[start - 1];
        if (before != ' ' && before != '\n' && before != '\t' && before != '(' && before != '"' && before != '[')
            return "";
    }
    while (start < cursor && !is_letter(text[start])) ++start;   // a quote before the word
    return std::string(text + start, cursor - start);
}

int lookup(const std::string& prefix_in, std::string out[kMaxResults]) {
    if (prefix_in.size() < kMinPrefix || prefix_in.size() >= kMaxWord) return 0;
    std::string prefix = lowered(prefix_in);
    size_t plen = prefix.size();
    int n = 0;

    // The owner's own words, most typed first (the newer of two equals wins).
    bool taken[kMaxLearned] = {};
    while (n < kMaxResults) {
        int best = -1;
        for (int i = int(g_learned.size()) - 1; i >= 0; --i) {
            const Learned& l = g_learned[size_t(i)];
            if (taken[i] || l.count < kLearnedShowAt) continue;
            if (std::strlen(l.word) <= plen || std::memcmp(l.word, prefix.data(), plen) != 0) continue;
            if (best < 0 || l.count > g_learned[size_t(best)].count) best = i;
        }
        if (best < 0) break;
        taken[best] = true;
        out[n++] = g_learned[size_t(best)].word;
    }

    // Then the built-in list, which is in order of how common each word is.
    for (const char* w = kBuiltinWords; *w && n < kMaxResults;) {
        const char* end = std::strchr(w, ' ');
        size_t len = size_t(end - w);
        if (len > plen && w[0] == prefix[0] && std::memcmp(w, prefix.data(), plen) == 0 && !has(out, n, w, len))
            out[n++].assign(w, len);
        w = end + 1;
    }
    return n;
}

std::string remainder(const std::string& prefix, const std::string& word) {
    if (word.size() <= prefix.size()) return "";
    std::string rest = word.substr(prefix.size());
    bool caps = prefix.size() >= 2;
    for (char ch : prefix)
        if (ch >= 'a' && ch <= 'z') caps = false;
    if (caps)
        for (char& ch : rest) ch = upper(ch);
    return rest;
}

void learn(const std::string& word_in) {
    if (!learnable(word_in)) return;
    std::string word = lowered(word_in);
    if (Learned* l = find(word)) {
        if (l->count < 9999) ++l->count;
        g_dirty = true;
        return;
    }
    if (g_learned.empty()) g_learned.reserve(kMaxLearned);
    if (int(g_learned.size()) >= kMaxLearned) {
        // Full: the least typed word goes, the oldest of them first.
        size_t drop = 0;
        for (size_t i = 1; i < g_learned.size(); ++i)
            if (g_learned[i].count < g_learned[drop].count) drop = i;
        g_learned.erase(g_learned.begin() + long(drop));
    }
    Learned l{};
    std::memcpy(l.word, word.data(), word.size());
    l.count = 1;
    g_learned.push_back(l);
    g_dirty = true;
}

void load(const std::string& path) {
    g_path = path;
    g_learned.clear();
    g_dirty = false;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return;
    char word[64];
    unsigned count = 0;
    g_learned.reserve(kMaxLearned);
    while (int(g_learned.size()) < kMaxLearned && std::fscanf(f, "%63s %u", word, &count) == 2) {
        std::string w = word;
        if (!learnable(w) || w != lowered(w) || count == 0 || find(w)) continue;
        Learned l{};
        std::memcpy(l.word, w.data(), w.size());
        l.count = uint16_t(count > 9999 ? 9999 : count);
        g_learned.push_back(l);
    }
    std::fclose(f);
}

bool dirty() { return g_dirty; }

bool save() {
    if (!g_dirty || g_path.empty()) return false;
    g_dirty = false;   // a card that can't be written isn't retried every tick
    std::string text;
    text.reserve(g_learned.size() * 12);
    for (const Learned& l : g_learned) {
        text += l.word;
        text += ' ';
        text += std::to_string(l.count);
        text += '\n';
    }
    // 0600: it is a list of what the owner types.
    return write_file_atomic(g_path, text, 0600);
}

void forget() {
    g_dirty = g_dirty || !g_learned.empty();
    g_learned.clear();
}

int learned_count() { return int(g_learned.size()); }

}  // namespace zs::suggest
