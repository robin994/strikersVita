// The game's rumble presets as HD rumble effects, each with its own envelope and pitch.

#include "port/switch/rumble.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <dolphin/pad.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <switch.h>

namespace
{

using Clock = std::chrono::steady_clock;

// An envelope point; values run straight to the next point, and the last point ends the effect.
struct Key
{
    int ms;
    float lowAmp;
    float lowHz;
    float highAmp;
    float highHz;
};

struct Effect
{
    const Key* keys;
    int count;
};

template <int N> constexpr Effect effect(const Key (&keys)[N]) { return { keys, N }; }

// Amplitudes are what the actuator gets at the default strength of 50.

// Ball receives and traps, jumps, turbo, dust and a missed Superstrike press: a tap.
const Key kSmall[] = {
    { 0, 0.04f, 160, 0.18f, 320 },
    { 10, 0.02f, 160, 0.06f, 300 },
    { 25, 0.00f, 160, 0.00f, 300 },
};

// Ordinary kicks and passes, slide tackles, landings and item hits: a knock.
const Key kMedium[] = {
    { 0, 0.30f, 130, 0.30f, 340 },
    { 15, 0.25f, 120, 0.08f, 280 },
    { 120, 0.00f, 110, 0.00f, 260 },
};

// Body checks, hit reactions and the fence: a crack, a thud, then the fall.
const Key kSolid[] = {
    { 0, 0.70f, 110, 0.70f, 400 },
    { 15, 0.70f, 100, 0.15f, 320 },
    { 80, 0.30f, 90, 0.00f, 320 },
    { 100, 0.45f, 85, 0.10f, 250 },
    { 240, 0.00f, 75, 0.00f, 250 },
};

// Perfect shots, one-timers, timed Superstrike presses, freezes, bombs and Chain Chomp: a strike.
const Key kShot[] = {
    { 0, 0.40f, 160, 0.75f, 380 },
    { 20, 0.40f, 140, 0.25f, 320 },
    { 160, 0.00f, 120, 0.00f, 320 },
};

// The hyper Superstrike: three surges, each higher, on the game's own rhythm.
const Key kHyper[] = {
    { 0, 0.55f, 90, 0.16f, 180 },
    { 250, 0.32f, 110, 0.08f, 200 },
    { 260, 0.00f, 110, 0.00f, 200 },
    { 450, 0.00f, 110, 0.00f, 200 },
    { 460, 0.55f, 110, 0.24f, 220 },
    { 950, 0.48f, 160, 0.48f, 320 },
    { 960, 0.00f, 160, 0.00f, 320 },
    { 1150, 0.00f, 160, 0.00f, 320 },
    { 1160, 0.80f, 120, 0.64f, 360 },
    { 1400, 0.55f, 100, 0.32f, 300 },
    { 2150, 0.00f, 80, 0.00f, 260 },
};

// Superstrikes, a perfect double press and a keeper's Superstrike catch: a heavy, ringing impact.
const Key kShootToScore[] = {
    { 0, 0.80f, 110, 0.80f, 360 },
    { 25, 0.72f, 100, 0.24f, 300 },
    { 300, 0.24f, 90, 0.00f, 260 },
    { 500, 0.00f, 80, 0.00f, 260 },
};

// In eRumbleActionPreset order.
const Effect kEffects[] = {
    effect(kSmall), effect(kMedium), effect(kSolid),
    effect(kShot), effect(kHyper), effect(kShootToScore),
};
const int kEffectCount = (int)(sizeof kEffects / sizeof kEffects[0]);

const auto kTick = std::chrono::milliseconds(5);

struct Voice
{
    const Effect* effect;
    Clock::time_point start;
};

struct Pad
{
    SDL_Gamepad* gamepad;
    SDL_JoystickID instance;
    float lowGain;
    float highGain;
    Voice voices[4];
    bool sounding;
};

struct Player
{
    std::mutex mutex;
    std::condition_variable wake;
    Pad pads[4];
};

// Never destroyed: its thread may still be running as the process exits.
Player* s_player;
Thread s_thread;

// False once the effect has ended.
bool sample(const Effect& fx, float ms, Key& out)
{
    const Key* k = fx.keys;
    if (ms >= (float)k[fx.count - 1].ms)
        return false;

    int i = 0;
    while (ms >= (float)k[i + 1].ms)
        i++;
    const float t = (ms - (float)k[i].ms) / (float)(k[i + 1].ms - k[i].ms);
    out.lowAmp = k[i].lowAmp + (k[i + 1].lowAmp - k[i].lowAmp) * t;
    out.lowHz = k[i].lowHz + (k[i + 1].lowHz - k[i].lowHz) * t;
    out.highAmp = k[i].highAmp + (k[i + 1].highAmp - k[i].highAmp) * t;
    out.highHz = k[i].highHz + (k[i + 1].highHz - k[i].highHz) * t;
    return true;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

bool send(const Pad& pad, const Key& mix)
{
    HidVibrationValue value;
    value.amp_low = clamp01(mix.lowAmp * pad.lowGain);
    value.freq_low = mix.lowHz;
    value.amp_high = clamp01(mix.highAmp * pad.highGain);
    value.freq_high = mix.highHz;
    // A Joy-Con that changes mode comes back under the same handle with a new ID.
    SDL_LockJoysticks();
    const bool sent = SDL_GetGamepadID(pad.gamepad) == pad.instance &&
                      SDL_SendGamepadEffect(pad.gamepad, &value, (int)sizeof value);
    SDL_UnlockJoysticks();
    return sent;
}

void run(void* arg)
{
    Player* player = static_cast<Player*>(arg);
    std::unique_lock<std::mutex> lock(player->mutex);
    for (;;)
    {
        const Clock::time_point now = Clock::now();
        bool playing = false;
        for (Pad& pad : player->pads)
        {
            // Overlapping effects add; each band takes its pitch from the loudest.
            Key mix = { 0, 0.0f, 160.0f, 0.0f, 320.0f };
            float lowLoudest = 0.0f;
            float highLoudest = 0.0f;
            bool active = false;
            for (Voice& voice : pad.voices)
            {
                if (voice.effect == nullptr)
                    continue;
                Key key;
                const float ms =
                    std::chrono::duration<float, std::milli>(now - voice.start).count();
                if (!sample(*voice.effect, ms, key))
                {
                    voice.effect = nullptr;
                    continue;
                }
                active = true;
                mix.lowAmp += key.lowAmp;
                mix.highAmp += key.highAmp;
                if (key.lowAmp > lowLoudest)
                {
                    lowLoudest = key.lowAmp;
                    mix.lowHz = key.lowHz;
                }
                if (key.highAmp > highLoudest)
                {
                    highLoudest = key.highAmp;
                    mix.highHz = key.highHz;
                }
            }

            // Effects stop for a controller that has gone or been replaced.
            if ((active || pad.sounding) && !send(pad, mix))
            {
                for (Voice& voice : pad.voices)
                    voice.effect = nullptr;
                active = false;
            }
            pad.sounding = active;
            playing = playing || active;
        }

        if (playing)
            player->wake.wait_for(lock, kTick);
        else
            player->wake.wait(lock);
    }
}

}   // namespace

int PortSwitchRumblePlay(unsigned int pad, int preset)
{
    if (pad >= 4 || preset < 0 || preset >= kEffectCount)
        return 0;

    const s32 index = PADGetIndexForPort(pad);
    SDL_Gamepad* gamepad = index >= 0 ? PADGetSDLGamepadForIndex((u32)index) : nullptr;
    u16 low = 0;
    u16 high = 0;
    if (gamepad == nullptr || PADGetForceDeviceRumble(pad) ||
        !PADGetRumbleIntensity(pad, &low, &high))
        return 0;

    if (s_player == nullptr)
    {
        // libnx has no pthread_detach, and std::thread::detach terminates on the error.
        Player* player = new Player();
        if (R_FAILED(threadCreate(&s_thread, run, player, nullptr, 0x10000, 0x2B, -2)) ||
            R_FAILED(threadStart(&s_thread)))
            return 0;
        s_player = player;
    }

    std::lock_guard<std::mutex> lock(s_player->mutex);
    Pad& state = s_player->pads[pad];
    state.gamepad = gamepad;
    state.instance = SDL_GetGamepadID(gamepad);
    state.lowGain = (float)low / 32767.5f;
    state.highGain = (float)high / 32767.5f;

    // A free voice, or else the oldest.
    Voice* slot = &state.voices[0];
    for (Voice& voice : state.voices)
    {
        if (voice.effect == nullptr)
        {
            slot = &voice;
            break;
        }
        if (voice.start < slot->start)
            slot = &voice;
    }
    slot->effect = &kEffects[preset];
    slot->start = Clock::now();
    s_player->wake.notify_one();
    return 1;
}

void PortSwitchRumbleStop(unsigned int pad)
{
    if (pad >= 4 || s_player == nullptr)
        return;

    std::lock_guard<std::mutex> lock(s_player->mutex);
    for (Voice& voice : s_player->pads[pad].voices)
        voice.effect = nullptr;
    s_player->wake.notify_one();
}
