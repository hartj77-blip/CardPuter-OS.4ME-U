// The welcome tour (docs/KEYS.md): a few pages that show a new owner how to
// get around ZeroOS from the keyboard. ZeroShell opens it instead of Home the
// first time it starts for a user; after that, "tour" in the home search
// opens it again.
//
//   Enter, Right, Space   next page       Esc, Left   previous page (on the first: skip)
//   last page             1 Wi-Fi, 2 Date & time, 3 Apps; Enter goes home
//
// The page with the arrows is a practice page: there the arrows light their
// keys instead of turning the page.
#pragma once

#include <memory>

#include "ui/app.h"

namespace zs {

// The id the shell opens it by. It isn't in the app list (ui/app.h).
constexpr const char* kWelcomeId = "welcome";

std::unique_ptr<App> make_welcome();

// True when the tour should open instead of Home: it has never been seen or
// skipped, and this isn't someone who used ZeroOS before the tour existed.
// ZS_WELCOME=0 in the environment turns it off (the tests).
bool welcome_due();

}  // namespace zs
