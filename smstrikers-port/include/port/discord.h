
#ifndef PORT_DISCORD_H
#define PORT_DISCORD_H

#ifdef __cplusplus
extern "C" {
#endif

// Discord rich presence; every call is a no-op while it is off.
void PortDiscordInit(void);

// Once a frame; `inMenuState` is the pause menu, which stops cGame::Update but not the match.
void PortDiscordUpdate(int inMenuState);

typedef struct PortDiscordMatch
{
    int mode;          // GameInfoManager::eGameModes, or -1 for Strikers 101
    int hasRound;      // a cup or custom battle, so `round` is BaseCup::mRoundNumber
    int round;
    int team[2];       // eTeamID, home then away
    int sidekick[2];   // eSidekickID
    int stadium;       // eStadiumID
    int userSide;      // the side a pad plays, named first
    int score[2];
    float clock;       // counts up to `duration`
    float duration;
    int suddenDeath;
} PortDiscordMatch;

// Every cGame::Update; sends are held to one per 15 seconds.
void PortDiscordSetMatch(const PortDiscordMatch* match);

void PortDiscordShutdown(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_DISCORD_H
