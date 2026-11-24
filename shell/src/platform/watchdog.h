// The systemd watchdog (zeroshell.service: WatchdogSec=10).
//
// The main loop calls watchdog_kick() on every pass. Every few seconds that
// sends "WATCHDOG=1" to systemd. If the loop stops coming round (a bug spins
// or deadlocks the UI thread), the pings stop, and after WatchdogSec systemd
// kills ZeroShell and Restart=always starts it again, so the screen can't
// stay frozen.
//
// Some work on the UI thread is slow on purpose: a shell command run to the
// end (bluetoothctl pair can take 25 s). Wrap it in a WatchdogGrace and the
// limit is raised for as long as it runs, then put back:
//
//   { WatchdogGrace g; out = run("bluetoothctl --timeout 25 pair ..."); }
//
// Outside systemd (the simulator, the headless build, running zeroshell by
// hand) there is no NOTIFY_SOCKET and all of this does nothing.
#pragma once

namespace zs {

// Once, at the start of main(): read what systemd passed and send the first ping.
void watchdog_init();
// From the main loop, as often as it likes; it sends at most one ping per
// quarter of the limit.
void watchdog_kick();

class WatchdogGrace {
public:
    explicit WatchdogGrace(int seconds = 90);
    ~WatchdogGrace();
    WatchdogGrace(const WatchdogGrace&) = delete;
    WatchdogGrace& operator=(const WatchdogGrace&) = delete;

private:
    bool active_ = false;
};

}  // namespace zs
