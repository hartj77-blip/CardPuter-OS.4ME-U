// How typed text finds things: Home search and every list's filter
// (ui/list_view.h) use the same rule, so "bt" or "wi fi" find the same rows
// wherever they are typed. No LVGL here: tests/search_match_test.cpp.
#pragma once

#include <string>

namespace zs {

// Lower case, letters, digits and spaces only: "Wi-Fi" and "wifi" are the
// same word.
std::string search_norm(const std::string& s);

// How well `query` finds an item called `name`, with more words to match on
// in `keywords` (synonyms: "bt" for Bluetooth). 0: it doesn't. Otherwise
// higher is better:
//   100  the name is what was typed ("files" -> Files)
//    80  the name starts with it ("blu" -> Bluetooth)
//    60  every typed word starts a word of the name ("fi" -> Wi-Fi)
//    40  every typed word is somewhere in the name ("tooth" -> Bluetooth)
//    20  some typed word is only in the keywords ("bt" -> Bluetooth)
// Every typed word has to be found, in any order. An empty query finds nothing.
int search_score(const std::string& name, const std::string& keywords, const std::string& query);

// True when `query` is one word and a word of `keywords` starts with it
// ("dev" in "dev python", "bt" in "bt pair"). Home search ranks an app found
// this way above a row that only has the word inside its name (ui/launcher.cpp).
bool search_keyword_starts(const std::string& keywords, const std::string& query);

}  // namespace zs
