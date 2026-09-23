#include "port/steamdeck.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <stdio.h>
#endif

#if defined(_WIN32)

static int ReadFirmwareString(const char* name, char* out, DWORD size)
{
    return RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", name,
                        RRF_RT_REG_SZ, NULL, out, &size) == ERROR_SUCCESS;
}

#define PORT_FIRMWARE_VENDOR "SystemManufacturer"
#define PORT_FIRMWARE_PRODUCT "SystemProductName"

#elif defined(__linux__)

static int ReadFirmwareString(const char* name, char* out, size_t size)
{
    char path[64];
    FILE* f;
    size_t n;

    snprintf(path, sizeof path, "/sys/devices/virtual/dmi/id/%s", name);
    f = fopen(path, "r");
    if (f == NULL)
        return 0;
    n = fread(out, 1, size - 1, f);
    fclose(f);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' '))
        n--;
    out[n] = '\0';
    return 1;
}

#define PORT_FIRMWARE_VENDOR "sys_vendor"
#define PORT_FIRMWARE_PRODUCT "product_name"

#endif

int PortIsSteamDeck(void)
{
    static int s_deck = -1;

    if (s_deck >= 0)
        return s_deck;

    s_deck = 0;
#if defined(_WIN32) || defined(__linux__)
    {
        char vendor[64];
        char product[64];

        // Jupiter is the LCD model and Galileo the OLED.
        if (ReadFirmwareString(PORT_FIRMWARE_VENDOR, vendor, sizeof vendor)
            && ReadFirmwareString(PORT_FIRMWARE_PRODUCT, product, sizeof product)
            && strcmp(vendor, "Valve") == 0
            && (strcmp(product, "Jupiter") == 0 || strcmp(product, "Galileo") == 0))
        {
            s_deck = 1;
        }
    }
#endif
    return s_deck;
}

int PortUnderGamescope(void)
{
    const char* v = getenv("GAMESCOPE_WAYLAND_DISPLAY");
    return v != NULL && *v != '\0';
}
