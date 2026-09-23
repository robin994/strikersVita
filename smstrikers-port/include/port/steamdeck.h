
#ifndef PORT_STEAMDECK_H
#define PORT_STEAMDECK_H

#ifdef __cplusplus
extern "C" {
#endif

// The Steam Deck's panel, LCD and OLED alike.
#define PORT_STEAM_DECK_WIDTH 1280
#define PORT_STEAM_DECK_ROWS 800

// Whether this machine is a Steam Deck, from the vendor and product name its firmware reports.
int PortIsSteamDeck(void);

// Whether the game runs under gamescope, as it does in Steam's Game Mode.
int PortUnderGamescope(void);

#ifdef __cplusplus
}
#endif

#endif // PORT_STEAMDECK_H
