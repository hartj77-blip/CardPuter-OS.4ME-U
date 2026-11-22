// The IR code database the IR Remote app browses (built by
// tools/irdb/build_irdb.py from Flipper-IRDB) and the user's own remotes.
//
//   /usr/share/zeroos/ir/            ZEROOS_IR_DB overrides it
//     index.tsv                      category, brand, model, path, buttons
//     <Category>/<Brand>/<Model>.ir  Flipper .ir files, as published
//     universal/<Kind>.ir            every Power (Vol +, ...) code for TVs, ...
//   ~/IR/*.ir                        the user's remotes: saved from the
//                                    database, learned, or copied from a Flipper
#pragma once

#include <string>
#include <vector>

#include "ir/ir.h"

namespace zs::ir {

struct RemoteRef {
    std::string category;   // "TVs"
    std::string brand;      // "LG"
    std::string model;      // "42LB5800"
    std::string path;       // relative to the database root
    int buttons = 0;
};

// One kind of universal remote ("TVs"): the buttons it has, in file order,
// and every code for each, the most common first.
struct Universal {
    std::string name;
    std::vector<std::string> buttons;
    std::vector<std::vector<Signal>> codes;   // parallel to buttons
};

std::string db_root();
std::string user_dir();

// The index; empty if no database is installed.
std::vector<RemoteRef> load_index(const std::string& root);
std::vector<std::string> universal_names(const std::string& root);
bool load_universal(const std::string& root, const std::string& name, Universal& out);

// The user's remotes: full paths of ~/IR/*.ir, sorted by name.
std::vector<std::string> user_remotes();
// A free file name in ~/IR for a remote called `name` (creates ~/IR).
std::string user_remote_path(const std::string& name);

// "TVs" -> "TVs", "LED_Lighting" -> "LED Lighting"
std::string display(const std::string& s);
// "/home/pi/IR/Living room.ir" -> "Living room"
std::string stem(const std::string& path);
// Brand from a "# src:" path ("TVs/LG/LG.ir" -> "LG").
std::string source_brand(const std::string& src);

// Case-insensitive "does haystack contain needle".
bool contains(const std::string& haystack, const std::string& needle);

}  // namespace zs::ir
