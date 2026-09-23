// Discord has no client on the Switch.

#include "port/discord.h"

void PortDiscordInit(void) {}

void PortDiscordUpdate(int inMenuState)
{
    (void)inMenuState;
}

void PortDiscordSetMatch(const PortDiscordMatch* match)
{
    (void)match;
}

void PortDiscordShutdown(void) {}
