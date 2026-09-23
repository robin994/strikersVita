#include "port/discord.h"

#include <borealis/discord.hpp>

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;
using borealis::discord::Presence;

// Discord's documented limit on presence updates; borealis does not enforce it.
constexpr auto kMinInterval = std::chrono::seconds(15);

// cGame::Update runs each fixed step; this long without it, outside the menu, is the match over.
constexpr auto kMatchStale = std::chrono::seconds(2);

// Seconds of countdown drift worth a send; goals stop the match clock but not Discord's.
constexpr int64_t kClockSlack = 2;

bool s_initialized = false;
int64_t s_startTime = 0;

Presence s_wanted;
Presence s_sent;
bool s_sentAny = false;
Clock::time_point s_lastSent;

bool s_inMatch = false;
bool s_paused = false;
Presence s_match;
int64_t s_suddenDeathSince = 0;
Clock::time_point s_lastMatch;

// The US disc's own names, in the order of the game's enums.
constexpr const char* kCaptains[] = {"Daisy", "Donkey Kong", "Luigi", "Mario", "Peach",
                                     "Waluigi", "Wario", "Yoshi", "Super Team"};
constexpr const char* kSidekicks[] = {"Toad", "Koopa", "Hammer Bros.", "Birdo"};
constexpr const char* kModes[] = {"Grudge Match", "Mushroom Cup", "Flower Cup", "Star Cup",
                                  "Bowser Cup", "Super Mushroom Cup", "Super Flower Cup",
                                  "Super Star Cup", "Super Bowser Cup", "Custom Battle"};
constexpr const char* kStadiums[] = {"Pipeline Central", "The Palace", "Konga Coliseum",
                                     "The Underground", "Crater Field", "Bowser Stadium",
                                     "The Battle Dome"};

// Art asset keys uploaded to kApplicationId, in eTeamID order.
constexpr const char* kCaptainImages[] = {"daisy", "donkeykong", "luigi", "mario", "peach",
                                          "waluigi", "wario", "yoshi", "superteam"};

template <size_t N>
const char* name_of(const char* const (&table)[N], int v) {
    return (v >= 0 && v < (int)N) ? table[v] : nullptr;
}

const char* or_unknown(const char* name) { return name != nullptr ? name : "?"; }

int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// The Super Team has no sidekick of its own.
std::string team_name(int team, int sidekick) {
    const char* kick = name_of(kSidekicks, sidekick);
    return kick != nullptr ? std::string(or_unknown(name_of(kCaptains, team))) + " (" + kick + ")"
                           : std::string(or_unknown(name_of(kCaptains, team)));
}

// BaseCup::mRoundNumber: league rounds count from 0, and the knockout stages are negative.
std::string round_name(int round) {
    if (round >= 0)
        return " round " + std::to_string(round + 1);
    if (round == -4)
        return " quarter-final";
    if (round == -3)
        return " semi-final";
    if (round == -2 || round == -1)
        return " final";
    return "";
}

// The settings window's spellings of on; anything else is off.
bool enabled() {
    const char* v = std::getenv("STRIKERS_DISCORD");
    if (v == nullptr)
        return false;
    std::string s;
    for (const char* p = v; *p != '\0'; ++p)
        if (*p != ' ' && *p != '\t')
            s += (char)std::tolower((unsigned char)*p);
    return s == "1" || s == "true" || s == "yes" || s == "on";
}

constexpr const char* kApplicationId = "1550132237826261123";

Presence menus() {
    Presence p{};
    p.details = "In the menus";
    p.startTimestamp = s_startTime;
    return p;
}

bool near(int64_t a, int64_t b) { return a - b <= kClockSlack && b - a <= kClockSlack; }

// Equal but for timestamps within the slack.
bool same(const Presence& a, const Presence& b) {
    Presence x = a;
    Presence y = b;
    x.startTimestamp = y.startTimestamp = 0;
    x.endTimestamp = y.endTimestamp = 0;
    return x == y && near(a.startTimestamp, b.startTimestamp) &&
           near(a.endTimestamp, b.endTimestamp) &&
           (a.startTimestamp == 0) == (b.startTimestamp == 0) &&
           (a.endTimestamp == 0) == (b.endTimestamp == 0);
}

void flush() {
    const Clock::time_point now = Clock::now();
    if (s_sentAny && (same(s_wanted, s_sent) || now - s_lastSent < kMinInterval))
        return;
    s_sentAny = true;
    s_lastSent = now;
    s_sent = s_wanted;
    borealis::discord::update_presence(s_wanted);
}

} // namespace

void PortDiscordInit(void) {
    if (s_initialized)
        return;
    if (!enabled())
        return;

    s_startTime = unix_now();

    borealis::AppInfo info{};
    info.discordApplicationId = kApplicationId;

    borealis::discord::EventHandlers handlers{};
    handlers.ready = [](const borealis::discord::User&) {
        std::fprintf(stderr, "[port] discord: connected\n");
    };
    handlers.error = [](int code, std::string_view message) {
        std::fprintf(stderr, "[port] discord: error %d: %.*s\n", code, (int)message.size(),
                     message.data());
    };

    s_initialized = borealis::discord::initialize(info, std::move(handlers));
    s_wanted = menus();
}

void PortDiscordUpdate(int inMenuState) {
    if (!s_initialized)
        return;
    borealis::discord::run_callbacks();

    if (s_inMatch) {
        const Clock::time_point now = Clock::now();
        if (inMenuState) {
            s_lastMatch = now;
            if (!s_paused) {
                s_paused = true;
                s_wanted = s_match;
                s_wanted.state += ", paused";
                s_wanted.startTimestamp = s_wanted.endTimestamp = 0;
            }
        } else if (now - s_lastMatch > kMatchStale) {
            s_inMatch = false;
            s_paused = false;
            s_suddenDeathSince = 0;
            s_wanted = menus();
        }
    }
    flush();
}

void PortDiscordSetMatch(const PortDiscordMatch* match) {
    if (!s_initialized || match == nullptr)
        return;
    s_inMatch = true;
    s_paused = false;
    s_lastMatch = Clock::now();

    const int me = match->userSide == 1 ? 1 : 0;
    const int them = 1 - me;
    const char* stadium = name_of(kStadiums, match->stadium);

    Presence p{};
    p.details = std::string("Playing as ") + or_unknown(name_of(kCaptains, match->team[me]));
    if (stadium != nullptr)
        p.details += std::string(" at ") + stadium;

    if (match->mode == -1) {
        p.state = "Strikers 101";
        p.startTimestamp = s_startTime;
    } else {
        p.state = or_unknown(name_of(kModes, match->mode));
        if (match->hasRound)
            p.state += round_name(match->round);
        p.state += std::string(" vs ") + or_unknown(name_of(kCaptains, match->team[them])) + ", " +
                   std::to_string(match->score[me]) + " - " + std::to_string(match->score[them]);
        if (match->suddenDeath) {
            if (s_suddenDeathSince == 0)
                s_suddenDeathSince = unix_now();
            p.state += ", sudden death";
            p.startTimestamp = s_suddenDeathSince;
        } else {
            s_suddenDeathSince = 0;
            const float remaining = match->duration - match->clock;
            p.endTimestamp = unix_now() + (remaining > 0.0f ? (int64_t)remaining : 0);
        }
    }

    const char* myImage = name_of(kCaptainImages, match->team[me]);
    const char* theirImage = name_of(kCaptainImages, match->team[them]);
    p.largeImageKey = myImage != nullptr ? myImage : "";
    p.largeImageText = team_name(match->team[me], match->sidekick[me]);
    p.smallImageKey = theirImage != nullptr ? theirImage : "";
    p.smallImageText = team_name(match->team[them], match->sidekick[them]);

    s_match = p;
    s_wanted = std::move(p);
}

void PortDiscordShutdown(void) {
    if (!s_initialized)
        return;
    borealis::discord::clear_presence();
    borealis::discord::shutdown();
    s_initialized = false;
}
