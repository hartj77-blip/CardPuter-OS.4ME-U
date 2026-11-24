// Word suggestions: what the word being typed could be. A small built-in
// word list plus the words this device's owner types (learned as they go).
// No LVGL here, so it builds and is tested on any machine
// (shell/tests/suggest_test.cpp). ui/suggest_bar.cpp shows the result.
//
// Cost: the built-in list is about 6 KB of read-only data, scanned once per
// key (a few microseconds). Learned words take 28 bytes each, 200 at most,
// allocated on first use. Nothing runs in the background.
#pragma once

#include <cstddef>
#include <string>

namespace zs::suggest {

constexpr int kMaxResults = 3;
constexpr size_t kMinPrefix = 2;     // letters typed before anything is offered
constexpr size_t kMinLearn = 4;      // shorter words save no typing
constexpr size_t kMaxWord = 23;
constexpr int kMaxLearned = 200;
constexpr int kLearnedShowAt = 2;    // a word typed once could be a typo

extern const char kBuiltinWords[];   // suggest_words.cpp

// The word being typed just before `cursor` (a byte offset into text), or ""
// when nothing should be offered: the cursor is inside a word, or what is
// being typed is no plain word (a path, an address, a number, a command
// option). A word is letters and apostrophes, starting with a letter, after
// the start of the text, white space or an opening bracket or quote.
std::string word_before(const char* text, size_t cursor);

// Up to kMaxResults words that start with prefix (any case) and are longer
// than it, best first: the owner's own words by how often they typed them,
// then the built-in list. All lower case. Returns how many.
int lookup(const std::string& prefix, std::string out[kMaxResults]);

// What accepting `word` types after `prefix`: the rest of the word, in
// capitals if the prefix is all capitals.
std::string remainder(const std::string& prefix, const std::string& word);

// A whole word was typed or accepted: count it. Ignores anything that is not
// a plain word of kMinLearn to kMaxWord letters.
void learn(const std::string& word);

// Learned words are kept in `path` as "word count" lines. load() reads them
// (once, at start); save() writes them if any changed, and says if it wrote.
void load(const std::string& path);
bool dirty();
bool save();
// Forget every learned word (and empty the file at the next save()).
void forget();
int learned_count();

}  // namespace zs::suggest
