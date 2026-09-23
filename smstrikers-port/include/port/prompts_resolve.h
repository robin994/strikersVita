#ifndef PORT_PROMPTS_RESOLVE_H
#define PORT_PROMPTS_RESOLVE_H

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace prompts
{

enum Family
{
    Auto = -1,
    Gamecube,
    Xbox,
    Playstation,
    Nintendo,
    Steamdeck,
    Generic,
    Keyboard
};

enum Control
{
    A,
    B,
    X,
    Y,
    L,
    R,
    Count
};

enum Native
{
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    Up,
    Down,
    Left,
    Right,
    Misc1,
    RightPaddle1,
    LeftPaddle1,
    RightPaddle2,
    LeftPaddle2,
    Touchpad,
    Misc2,
    Misc3,
    Misc4,
    Misc5,
    Misc6
};

enum Label
{
    Unknown,
    LabelA,
    LabelB,
    LabelX,
    LabelY,
    Cross,
    Circle,
    Square,
    Triangle
};

enum
{
    Unset = -1,
    LeftTrigger = -2,
    RightTrigger = -3
};

struct Binding
{
    int button;
    int axis;
    int axisButton;
    int sign;
};

struct Input
{
    Family family;
    Binding bindings[Count];
    bool connected;
    bool emulateTriggers;
    int overrides[Count];
    Label labels[4];
    const char* keys[Count];
};

struct Icon
{
    Family family;
    char token[32];
    char faceKey[16];
    char wideKey[24];
    int original;
    bool unbound;
};

enum Form
{
    Face,
    Wide,
    Menu
};

struct Image
{
    unsigned width;
    unsigned height;
    std::vector<uint8_t> rgba;
};

Family parseFamily(const char* text);
const char* familyName(Family family);
Input defaults(Family family);
Binding bindingFor(const Input& input, Control control);
Icon resolve(const Input& input, Control control);
bool originalFits(const Icon& icon, Control target);
int originalLegend(const Icon& icon, const bool loaded[4], int targetSlot = -1);
size_t c8Size(unsigned w, unsigned h);
void c8Tile(const uint8_t* rows, unsigned w, unsigned h, uint8_t* out);

Image trim(const Image& image);

Image place(const Image& art, Family family, const char* token, Form form);

uint16_t rgb5a3(const uint8_t* rgba);

void quantizeCI8(const uint8_t* rgba, unsigned count, uint8_t* indices, uint16_t* palette);

}   // namespace prompts

#endif // PORT_PROMPTS_RESOLVE_H
