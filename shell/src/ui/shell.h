#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lvgl.h"
#include "ui/app.h"

namespace zs {

class Shell {
public:
    // Builds the status bar and home screen on the active display, and hooks
    // the keypad so the shell sees every key before LVGL does.
    void start(lv_indev_t* keypad);

    void open_app(const std::string& id);
    void open_app(const std::string& id, const std::string& arg);
    std::string take_app_arg();
    void go_home();
    // Esc held down (keypad hook): close the panel and go home now.
    void hold_home();
    void toast(const std::string& msg);
    void add_focus(lv_obj_t* obj);

    // Called by the indev hook on each key press. True = consumed.
    bool handle_key(uint32_t key);
    // After handle_key: true if the press picked an item (3..9) and should
    // reach LVGL as Enter. Reading it clears it.
    bool take_press_as_enter() { bool b = press_as_enter_; press_as_enter_ = false; return b; }
    // The key handle_key acted on (after the Fn layer).
    uint32_t last_key() const { return last_key_; }
    // The open app needs no typing: plain letters are their Fn keys.
    void set_fn_layer(bool on) { fn_layer_ = on; }
    // The open screen's actions (ui/actions.h): soft-key bar, Tab menu.
    void set_actions(std::vector<Action> actions);

    static Shell& get();

    // Power manager: redraw the dim/blank overlay and pace the UI timer.
    void screen_changed();

    // Hand the screen to the Linux console terminal (see Hal::terminal_open)
    // and take it back when the terminal closes.
    bool open_terminal(const std::string& command);
    bool in_terminal() const { return handoff_ && !handoff_camera_; }
    // The same, for the camera app (see Hal::camera_open).
    bool open_camera();
    const std::string& current_app() const { return app_id_; }
    // Bumped each time an app opens or closes (key repeat stops at a switch).
    uint32_t app_generation() const { return app_generation_; }
    // Before a forced power-off: close the app (it saves) and the GPS track.
    void prepare_power_off();

private:
    void apply_pending();
    void system_key(uint32_t key);
    bool number_key(uint32_t key);
    void build_statusbar();
    void update_statusbar();
    void switch_to(std::unique_ptr<App> app, const std::string& title);
    void fit_app_root();
    static void tick_cb(lv_timer_t* t);

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* app_root_ = nullptr;   // the open app's area
    lv_obj_t* sb_time_ = nullptr;
    lv_obj_t* sb_title_ = nullptr;
    lv_obj_t* sb_home_ = nullptr;    // workspace pill, shown on the home screen
    lv_obj_t* sb_icons_ = nullptr;
    lv_obj_t* sb_ctrl_ = nullptr;    // "Ctrl": tapped, waiting for the next key
    lv_obj_t* toast_ = nullptr;
    lv_obj_t* blank_ = nullptr;     // simulator only: stands in for the backlight
    lv_timer_t* tick_timer_ = nullptr;
    bool handoff_ = false;
    bool handoff_camera_ = false;   // handed to the camera app, not the terminal
    uint32_t handoff_since_ = 0;
    uint32_t toast_since_ = 0;
    bool toast_shown_ = false;
    uint32_t app_generation_ = 0;
    uint32_t last_key_ = 0;
    bool press_as_enter_ = false;
    bool fn_layer_ = false;     // reset at each app switch
    lv_indev_t* keypad_ = nullptr;
    std::unique_ptr<App> app_;
    std::string app_id_;
    std::string pending_;
    std::string app_arg_;   // shell_open_app(id, arg): read once by the app
    lv_group_t* group_ = nullptr;
};

std::unique_ptr<App> make_launcher();

}  // namespace zs
