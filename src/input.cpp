#include <array>
#include <string_view>
#include <tuple>

#include <SDL.h>

#include "types.hpp"

namespace ung::input {

struct Gamepad {
    SDL_GameController* controller;
    int device_index;
    int32_t instance_id;
    bool connected;
    uint8_t type;
    ung_gamepad_info info;
    float deadzone_inner;
    float deadzone_outer;
    float last_active;
    std::array<uint32_t, SDL_CONTROLLER_BUTTON_MAX> button_pressed;
    std::array<uint32_t, SDL_CONTROLLER_BUTTON_MAX> button_released;
};

constexpr usize MaxMouseButtons = 16;
struct State {
    std::array<bool, SDL_NUM_SCANCODES> key_down;
    std::array<u8, SDL_NUM_SCANCODES> key_pressed;
    std::array<u8, SDL_NUM_SCANCODES> key_released;
    int mouse_x, mouse_y, mouse_dx, mouse_dy;
    int mouse_scroll_left, mouse_scroll_right, mouse_scroll_y_pos, mouse_scroll_y_neg;
    std::array<bool, MaxMouseButtons> mouse_button_down;
    std::array<u8, MaxMouseButtons> mouse_button_pressed;
    std::array<u8, MaxMouseButtons> mouse_button_released;
    Pool<Gamepad> gamepads;
    u64 last_active_gamepad;
};

State* state;

void init(ung_init_params params)
{
    assert(!state);
    state = allocate<State>();
    std::memset(state, 0, sizeof(State));

    state->gamepads.init(params.max_num_gamepads ? params.max_num_gamepads : 8);
}

void shutdown()
{
    if (!state) {
        return;
    }

    state->gamepads.free();

    deallocate(state, 1);
    state = nullptr;
}

void reset()
{
    std::memset(state->key_pressed.data(), 0, sizeof(state->key_pressed));
    std::memset(state->key_released.data(), 0, sizeof(state->key_released));
    state->mouse_dx = 0;
    state->mouse_dy = 0;
    state->mouse_scroll_left = 0;
    state->mouse_scroll_right = 0;
    state->mouse_scroll_y_pos = 0;
    state->mouse_scroll_y_neg = 0;
    std::memset(state->mouse_button_pressed.data(), 0, sizeof(state->mouse_button_pressed));
    std::memset(state->mouse_button_released.data(), 0, sizeof(state->mouse_button_released));
    for (uint32_t i = 0; i < state->gamepads.sm.capacity; ++i) {
        state->gamepads.data[i].button_pressed.fill(0);
        state->gamepads.data[i].button_released.fill(0);
    }
}

Gamepad* get_gamepad(u64 key)
{
    return get(state->gamepads, key);
}

static void get_gamepad_info(
    ung_gamepad_info* info, int device_index, SDL_GameController* controller)
{
    std::memset(info, 0, sizeof(ung_gamepad_info));

    const auto name = SDL_JoystickNameForIndex(device_index);
    const auto name_len = name ? std::strlen(name) : 0;
    assert(name_len < sizeof(info->name));
    std::memcpy(info->name, name, name_len);

    info->vendor_id = SDL_GameControllerGetVendor(controller);
    info->product_id = SDL_GameControllerGetProduct(controller);

    const auto guid = SDL_JoystickGetDeviceGUID(device_index);
    std::memcpy(info->guid, guid.data, sizeof(info->guid));

    const auto serial = SDL_GameControllerGetSerial(controller);
    const auto serial_len = serial ? std::strlen(serial) : 0;
    assert(serial_len < sizeof(info->serial));
    std::memcpy(info->serial, serial, serial_len);
}

void process_event(SDL_Event* event)
{
    switch (event->type) {
    // Keyboard
    case SDL_KEYDOWN:
        state->key_down[event->key.keysym.scancode] = true;
        state->key_pressed[event->key.keysym.scancode]++;
        break;
    case SDL_KEYUP:
        state->key_down[event->key.keysym.scancode] = false;
        state->key_released[event->key.keysym.scancode]++;
        break;
    // Mouse
    case SDL_MOUSEBUTTONDOWN:
        if (event->button.button < MaxMouseButtons) {
            state->mouse_button_down[event->button.button] = true;
            state->mouse_button_pressed[event->button.button]++;
        }
        break;
    case SDL_MOUSEBUTTONUP:
        if (event->button.button < MaxMouseButtons) {
            state->mouse_button_down[event->button.button] = false;
            state->mouse_button_released[event->button.button]++;
        }
        break;
    case SDL_MOUSEMOTION:
        state->mouse_x = event->motion.x;
        state->mouse_y = event->motion.y;
        state->mouse_dx = event->motion.xrel;
        state->mouse_dy = event->motion.yrel;
        break;
    case SDL_MOUSEWHEEL:
        if (event->wheel.x < 0) {
            state->mouse_scroll_left += event->wheel.x;
        } else if (event->wheel.x > 0) {
            state->mouse_scroll_right += event->wheel.x;
        }
        if (event->wheel.y < 0) {
            state->mouse_scroll_y_neg += event->wheel.y;
        } else if (event->wheel.y > 0) {
            state->mouse_scroll_y_pos += event->wheel.y;
        }
        break;
    // Gamepads
    case SDL_CONTROLLERDEVICEADDED: {
        const auto device_index = event->cdevice.which;
        if (!SDL_IsGameController(device_index)) {
            break;
        }

        auto controller = SDL_GameControllerOpen(device_index);
        assert(controller); // TODO: handle NULL return (SDL_GetError)

        ung_gamepad_info info;
        get_gamepad_info(&info, device_index, controller);

        u64 id = 0;
        Gamepad* gamepad = nullptr;
        auto& gps = state->gamepads;
        for (u32 i = 0; i < gps.capacity(); ++i) {
            const auto key = gps.get_key(i);
            if (!key) {
                continue;
            }
            if (std::memcmp(&info, &gps.data[i].info, sizeof(ung_gamepad_info)) == 0) {
                id = gps.keys[i];
                gamepad = &gps.data[i];
            }
        }

        if (gamepad) {
            SDL_GameControllerClose(gamepad->controller);
        } else {
            std::tie(id, gamepad) = state->gamepads.insert();
            gamepad->deadzone_inner = 0.1f;
            gamepad->deadzone_outer = 0.9f;
            std::memcpy(&gamepad->info, &info, sizeof(ung_gamepad_info));
        }

        gamepad->controller = controller;
        gamepad->device_index = device_index;
        gamepad->instance_id = SDL_JoystickGetDeviceInstanceID(device_index);
        gamepad->connected = true;
        gamepad->type = SDL_GameControllerGetType(gamepad->controller);

        gamepad->last_active = ung_get_time();
        if (!state->last_active_gamepad) {
            state->last_active_gamepad = id;
        }
        break;
    }
    case SDL_CONTROLLERDEVICEREMOVED: {
        const auto key
            = ung_get_gamepad_from_event(SDL_CONTROLLERDEVICEREMOVED, event->cdevice.which);
        assert(key.id);
        auto gamepad = get_gamepad(key.id);

        gamepad->connected = false;

        if (state->last_active_gamepad == key.id) {
            state->last_active_gamepad = 0;
            float max_last_active = 0.0f;
            auto& gps = state->gamepads;
            for (u32 i = 0; i < gps.capacity(); ++i) {
                const auto key = gps.get_key(i);
                if (!key) {
                    continue;
                }
                if (gps.data[i].last_active > max_last_active) {
                    state->last_active_gamepad = gps.keys[i];
                    max_last_active = gps.data[i].last_active;
                }
            }
        }
        break;
    }
    case SDL_CONTROLLERBUTTONDOWN: {
        const auto key
            = ung_get_gamepad_from_event(SDL_CONTROLLERDEVICEREMOVED, event->cdevice.which);
        assert(key.id);
        auto gamepad = get_gamepad(key.id);

        gamepad->button_pressed[event->cbutton.button]++;

        state->last_active_gamepad = key.id;
        gamepad->last_active = ung_get_time();
        break;
    }
    case SDL_CONTROLLERBUTTONUP: {
        const auto key
            = ung_get_gamepad_from_event(SDL_CONTROLLERDEVICEREMOVED, event->cdevice.which);
        assert(key.id);
        auto gamepad = get_gamepad(key.id);

        gamepad->button_released[event->cbutton.button]++;

        break;
    }
    }
}

EXPORT ung_key ung_key_from_name(const char* key)
{
    return (ung_key)SDL_GetScancodeFromName(key);
}

EXPORT ung_mouse_button ung_mouse_button_from_name(const char* button)
{
    static constexpr std::array<std::string_view, 5> names
        = { "left", "middle", "right", "side1", "side2" };
    const std::string_view button_sv = button;
    for (size_t i = 0; i < names.size(); ++i) {
        if (button_sv == names[i]) {
            return (ung_mouse_button)i + 1;
        }
    }
    ung_panicf("Invalid mouse button name: %s", button);
}

EXPORT ung_gamepad_axis ung_gamepad_axis_from_name(const char* axis)
{
    static constexpr std::array<std::string_view, 6> names
        = { "leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger" };
    const std::string_view axis_sv = axis;
    for (size_t i = 0; i < names.size(); ++i) {
        if (axis_sv == names[i]) {
            return (ung_gamepad_axis)i;
        }
    }
    ung_panicf("Invalid gamepad axis name: %s", axis);
}

EXPORT ung_gamepad_button ung_gamepad_button_from_name(const char* button)
{
    static constexpr std::array<std::string_view, 6> actions
        = { "confirm", "cancel", "primary", "secondary", "tertiary", "quaternary" };
    static constexpr std::array<std::string_view, 15> buttons
        = { "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
              "rightshoulder", "dpadup", "dpaddown", "dpadleft", "dpadright" };
    const std::string_view button_sv = button;
    for (size_t i = 0; i < actions.size(); ++i) {
        if (button_sv == actions[i]) {
            return UNG_GAMEPAD_ACTION_CONFIRM + (ung_gamepad_button)i;
        }
    }
    for (size_t i = 0; i < buttons.size(); ++i) {
        if (button_sv == buttons[i]) {
            return (ung_gamepad_button)i;
        }
    }
    ung_panicf("Invalid gamepad button name: %s", button);
}

EXPORT bool ung_key_down(ung_key key)
{
    return state->key_down.at(key);
}

EXPORT u8 ung_key_pressed(ung_key key)
{
    return state->key_pressed.at(key);
}

EXPORT u8 ung_key_released(ung_key key)
{
    return state->key_released.at(key);
}

EXPORT bool ung_key_down_s(const char* key)
{
    return ung_key_down(ung_key_from_name(key));
}

EXPORT u8 ung_key_pressed_s(const char* key)
{
    return ung_key_pressed(ung_key_from_name(key));
}

EXPORT u8 ung_key_released_s(const char* key)
{
    return ung_key_released(ung_key_from_name(key));
}

EXPORT bool ung_mouse_down(ung_mouse_button button)
{
    return state->mouse_button_down.at(button);
}

EXPORT u8 ung_mouse_pressed(ung_mouse_button button)
{
    return state->mouse_button_pressed.at(button);
}

EXPORT u8 ung_mouse_released(ung_mouse_button button)
{
    return state->mouse_button_released.at(button);
}

EXPORT bool ung_mouse_down_s(const char* button)
{
    return ung_mouse_down(ung_mouse_button_from_name(button));
}

EXPORT u8 ung_mouse_pressed_s(const char* button)
{
    return ung_mouse_pressed(ung_mouse_button_from_name(button));
}

EXPORT u8 ung_mouse_released_s(const char* button)
{
    return ung_mouse_released(ung_mouse_button_from_name(button));
}

EXPORT void ung_mouse_set_relative(bool relative)
{
    SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE);
}

EXPORT void ung_mouse_get(int* x, int* y, int* dx, int* dy)
{
    *x = state->mouse_x;
    *y = state->mouse_y;
    *dx = state->mouse_dx;
    *dy = state->mouse_dy;
}

EXPORT void ung_mouse_get_scroll_x(int* left, int* right)
{
    *left = state->mouse_scroll_left;
    *right = state->mouse_scroll_right;
}

EXPORT void ung_mouse_get_scroll_y(int* pos, int* neg)
{
    *pos = state->mouse_scroll_y_pos;
    *neg = state->mouse_scroll_y_neg;
}

EXPORT size_t ung_get_gamepads(ung_gamepad_id* gamepads, size_t max_num_gamepads)
{
    size_t n = 0;
    const auto& gps = state->gamepads;
    for (uint32_t i = 0; i < gps.sm.capacity; ++i) {
        const auto key = ung_slotmap_get_key(&gps.sm, i);
        if (key) {
            gamepads[n++] = { key };
        }
    }
    return n;
}

EXPORT ung_gamepad_id ung_gamepad_get_any()
{
    return { state->last_active_gamepad };
}

EXPORT SDL_GameController* ung_gamepad_get_sdl(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->controller;
}

EXPORT ung_gamepad_id ung_get_gamepad_from_event(uint32_t type, int32_t which)
{
    assert(which >= 0);

    int32_t device_index = -1, instance_id = -1;
    if (type == SDL_CONTROLLERDEVICEADDED) {
        device_index = which;
    } else {
        instance_id = which;
    }

    const auto& gps = state->gamepads;
    for (u32 i = 0; i < gps.capacity(); ++i) {
        const auto key = gps.get_key(i);
        if (!key) {
            continue;
        }
        if (gps.data[i].device_index == device_index || gps.data[i].instance_id == instance_id) {
            return { key };
        }
    }
    return { 0 };
}

EXPORT int32_t ung_gamepad_instance_id(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->instance_id;
}

EXPORT bool ung_gamepad_is_connected(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->connected;
}

EXPORT const ung_gamepad_info* ung_gamepad_get_info(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return &gp->info;
}

static float axis_to_float(int16_t v)
{
    return v > 0 ? (float)v / 32767.0f : (float)v / 32768.0f;
}

EXPORT float ung_gamepad_axis_get(ung_gamepad_id gamepad, ung_gamepad_axis axis)
{
    const auto gp = get_gamepad(gamepad.id);
    const auto v
        = axis_to_float(SDL_GameControllerGetAxis(gp->controller, (SDL_GameControllerAxis)axis));
    if (std::fabs(v) < gp->deadzone_inner) {
        return 0.0f;
    } else if (v > gp->deadzone_outer) {
        return 1.0f;
    } else if (v < -gp->deadzone_outer) {
        return -1.0f;
    }
    return v;
}

EXPORT float ung_gamepad_axis_get_s(ung_gamepad_id gamepad, const char* axis)
{
    return ung_gamepad_axis_get(gamepad, ung_gamepad_axis_from_name(axis));
}

static SDL_GameControllerButton map_button(uint8_t type, ung_gamepad_button button)
{
    if (button >= UNG_GAMEPAD_ACTION_CONFIRM) {
        assert(button <= UNG_GAMEPAD_ACTION_QUATERNARY);
        const size_t button_idx = button - UNG_GAMEPAD_ACTION_CONFIRM;
        // confirm, cancel, primary, secondary, tertiary, quaternary
        constexpr std::array<SDL_GameControllerButton, 6> xbox
            = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
                  SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_B };
        constexpr std::array<SDL_GameControllerButton, 6> ps
            = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
                  SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y };
        constexpr std::array<SDL_GameControllerButton, 6> nintendo
            = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A,
                  SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_B };

        switch (type) {
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
        case SDL_CONTROLLER_TYPE_PS5:
            return ps[button_idx];
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return nintendo[button_idx];
        default: // default is xbox
            return xbox[button_idx];
        }
    }
    return (SDL_GameControllerButton)button;
}

EXPORT bool ung_gamepad_button_down(ung_gamepad_id gamepad, ung_gamepad_button button)
{
    const auto gp = get_gamepad(gamepad.id);
    return SDL_GameControllerGetButton(gp->controller, map_button(gp->type, button)) > 0;
}

EXPORT uint32_t ung_gamepad_button_pressed(ung_gamepad_id gamepad, ung_gamepad_button button)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->button_pressed.at((size_t)map_button(gp->type, button));
}

EXPORT uint32_t ung_gamepad_button_released(ung_gamepad_id gamepad, ung_gamepad_button button)
{
    const auto gp = get_gamepad(gamepad.id);
    return gp->button_released.at((size_t)map_button(gp->type, button));
}

EXPORT bool ung_gamepad_button_down_s(ung_gamepad_id gamepad, const char* button)
{
    return ung_gamepad_button_down(gamepad, ung_gamepad_button_from_name(button));
}

EXPORT uint32_t ung_gamepad_button_pressed_s(ung_gamepad_id gamepad, const char* button)
{
    return ung_gamepad_button_pressed(gamepad, ung_gamepad_button_from_name(button));
}

EXPORT uint32_t ung_gamepad_button_released_s(ung_gamepad_id gamepad, const char* button)
{
    return ung_gamepad_button_released(gamepad, ung_gamepad_button_from_name(button));
}

EXPORT int ung_gamepad_get_player_index(ung_gamepad_id gamepad)
{
    const auto gp = get_gamepad(gamepad.id);
    return SDL_GameControllerGetPlayerIndex(gp->controller);
}

EXPORT void ung_gamepad_set_player_index(ung_gamepad_id gamepad, int player_index)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerSetPlayerIndex(gp->controller, player_index);
}

EXPORT void ung_gamepad_rumble(
    ung_gamepad_id gamepad, uint16_t low_freq, uint16_t high_freq, uint32_t duration_ms)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerRumble(gp->controller, low_freq, high_freq, duration_ms);
}

EXPORT void ung_gamepad_rumble_triggers(
    ung_gamepad_id gamepad, uint16_t left, uint16_t right, uint32_t duration_ms)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerRumbleTriggers(gp->controller, left, right, duration_ms);
}

EXPORT void ung_gamepad_set_led(ung_gamepad_id gamepad, uint8_t red, uint8_t green, uint8_t blue)
{
    const auto gp = get_gamepad(gamepad.id);
    SDL_GameControllerSetLED(gp->controller, red, green, blue);
}

EXPORT void ung_gamepad_axis_deadzone(
    ung_gamepad_id gamepad, ung_gamepad_axis axis, float inner, float outer)
{
    const auto gp = get_gamepad(gamepad.id);
    (void)axis;
    gp->deadzone_inner = inner;
    gp->deadzone_outer = outer;
}
}
