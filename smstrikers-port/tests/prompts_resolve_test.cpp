#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "port/prompts_resolve.h"

using namespace prompts;

static int cases;

static void check(bool result, const char* name)
{
    ++cases;
    if (!result)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void token(const Input& in, Control c, const char* expected)
{
    check(!strcmp(resolve(in, c).token, expected), expected);
}

int main()
{
    for (int f = Gamecube; f <= Generic; ++f)
    {
        Input in = defaults(static_cast<Family>(f));
        token(in, L, "lt");
        token(in, R, "rt");
        const char* expected[] = { "a", "a", "cross", "b", "a", "south" };
        token(in, A, expected[f]);
        in.overrides[A] = RightShoulder;
        in.overrides[L] = LeftShoulder;
        check(bindingFor(in, A).button == RightShoulder, "fallback probe shares resolved binding");
        token(in, A, "rb");
        token(in, L, "lb");
        in.overrides[L] = RightTrigger;
        token(in, L, "rt");
        in.overrides[A] = LeftTrigger;
        token(in, A, expected[f]);
        in.connected = true;
        in.bindings[A].button = LeftShoulder;
        token(in, A, "lb");
    }

    Input in = defaults(Gamecube);
    in.connected = true;
    in.labels[East] = LabelX;
    in.labels[West] = LabelB;
    in.bindings[L] = { Misc3, -1, -1, 1 };
    in.bindings[R] = { Misc4, -1, -1, 1 };
    in.emulateTriggers = false;
    check(resolve(in, L).original == L, "adapter digital L without emulation");
    check(resolve(in, R).original == R, "adapter digital R without emulation");
    in.bindings[A].button = West;
    check(resolve(in, A).original == B, "adapter west is labelled B");
    bool loaded[] = { true, true, true, true };
    check(originalLegend(resolve(in, A), loaded) == 1, "A menu slot uses B original");
    check(originalLegend(resolve(in, A), loaded, 3) == 3, "alternate B keeps its own original");
    in.bindings[L].button = South;
    check(!originalFits(resolve(in, L), L), "face button in shoulder cell needs Kenney art");
    in.bindings[Y].button = RightShoulder;
    token(in, Y, "rb");
    check(originalLegend(resolve(in, Y), loaded) == -1, "Z has no original menu texture");

    in = defaults(Gamecube);
    loaded[2] = false;
    check(originalLegend(resolve(in, Y), loaded) == -1, "BootUI has no Y original");
    check(originalFits(resolve(in, A), A), "default GC restores original glyph");
    in.connected = true;
    in.bindings[L] = { -1, 4, RightShoulder, 1 };
    token(in, L, "lt");
    in.bindings[L].axis = -1;
    token(in, L, "rb");
    in.bindings[L].button = South;
    token(in, L, "a");
    in.bindings[L].button = -1;
    in.emulateTriggers = false;
    token(in, L, "unbound");
    in.emulateTriggers = true;
    in.bindings[L] = { -1, 0, -1, -1 };
    token(in, L, "ls_left");
    in.bindings[A].button = -1;
    check(resolve(in, A).unbound, "missing face binding");

    in = defaults(Keyboard);
    check(!strcmp(resolve(in, A).faceKey, "X"), "default keyboard X");
    in.keys[A] = "Left Shift";
    check(!strcmp(resolve(in, A).faceKey, "SHIFT"), "shift symbol");
    in.keys[L] = "Space";
    check(!strcmp(resolve(in, L).wideKey, "Space"), "shoulder Space");
    in.keys[A] = "F12";
    check(!strcmp(resolve(in, A).faceKey, "F12"), "F12 not truncated");
    in.keys[A] = "Keypad 1";
    check(!strcmp(resolve(in, A).faceKey, "K1"), "keypad distinction");
    in.keys[A] = "é";
    check(!strcmp(resolve(in, A).faceKey, "é"), "UTF-8 kept intact");
    in.keys[B] = nullptr;
    check(resolve(in, B).unbound, "duplicate key left unbound");

    check(parseFamily("wrong") < Auto, "invalid family");
    for (int f = Auto; f <= Keyboard; ++f)
        check(parseFamily(familyName(static_cast<Family>(f))) == f, "family round trip");

    const unsigned dimensions[][2] = { { 32, 32 }, { 20, 12 }, { 13, 7 }, { 1, 1 } };
    for (const auto& dim : dimensions)
    {
        unsigned w = dim[0], h = dim[1];
        for (int plane = 0; plane < 4; ++plane)
        {
            std::vector<uint8_t> rows(w * h), tiled(c8Size(w, h)), decoded(w * h);
            for (unsigned i = 0; i < w * h; ++i)
                rows[i] = (i * 2654435761u) >> (plane * 8);
            c8Tile(rows.data(), w, h, tiled.data());
            const uint8_t* src = tiled.data();
            const unsigned bw = (w + 7) / 8, bh = (h + 3) / 4;
            for (unsigned by = 0; by < bh; ++by)
            {
                unsigned y0 = by * 4, nrows = h - y0 < 4 ? h - y0 : 4;
                for (unsigned bx = 0; bx < bw; ++bx)
                {
                    unsigned x0 = bx * 8, ncols = w - x0 < 8 ? w - x0 : 8;
                    for (unsigned y = 0; y < nrows; ++y)
                    {
                        for (unsigned x = 0; x < ncols; ++x)
                            decoded[(y0 + y) * w + x0 + x] = src[x];
                        src += 8;
                    }
                    src += 8 * (4 - nrows);
                }
            }
            check(rows == decoded, "CI8 tiles round trip");
            check(size_t(src - tiled.data()) == tiled.size(), "CI8 padding consumed");
        }
    }

    Image sheet = { 10, 8, std::vector<uint8_t>(10 * 8 * 4, 0) };
    for (unsigned y = 5; y < 7; ++y)
        for (unsigned x = 4; x < 7; ++x)
            sheet.rgba[(y * 10 + x) * 4 + 3] = 255;
    const Image trimmed = trim(sheet);
    check(trimmed.width == 3 && trimmed.height == 2, "trim crops to the opaque pixels");
    check(trim(Image{ 4, 4, std::vector<uint8_t>(64, 0) }).width == 1, "trim of nothing is 1x1");

    Image button = { 110, 110, std::vector<uint8_t>(110 * 110 * 4, 255) };
    struct
    {
        Form form;
        unsigned width, height, cx, cy, floor;
    } cells[] = { { Face, 172, 180, 104, 64, 128 },
                  { Wide, 292, 180, 158, 64, 128 },
                  { Menu, 32, 32, 16, 16, 32 } };
    for (const auto& cell : cells)
    {
        const Image placed = place(button, Xbox, "a", cell.form);
        check(placed.width == cell.width && placed.height == cell.height, "fixed cell size");
        unsigned x0 = placed.width, y0 = placed.height, x1 = 0, y1 = 0;
        for (unsigned y = 0; y < placed.height; ++y)
            for (unsigned x = 0; x < placed.width; ++x)
                if (placed.rgba[(y * placed.width + x) * 4 + 3] > 128)
                {
                    x0 = x < x0 ? x : x0;
                    y0 = y < y0 ? y : y0;
                    x1 = x + 1 > x1 ? x + 1 : x1;
                    y1 = y + 1 > y1 ? y + 1 : y1;
                }
        check(abs(int(x0 + x1) - int(cell.cx * 2)) <= 2 &&
                  abs(int(y0 + y1) - int(cell.cy * 2)) <= 2,
              "art centred in its cell");
        check(y1 <= cell.floor, "art clears the baseline");
    }

    const uint8_t white[] = { 255, 255, 255, 255 }, clear[] = { 90, 10, 200, 20 };
    check(rgb5a3(white) == 0xffff, "opaque RGB5A3");
    check(rgb5a3(clear) == 0, "transparent RGB5A3 whatever the colour");

    for (unsigned distinct : { 200u, 1024u })
    {
        std::vector<uint8_t> pixels(1024 * 4);
        for (unsigned i = 0; i < 1024; ++i)
        {
            const unsigned v = i % distinct;
            const uint8_t p[4] = { uint8_t(v * 8), uint8_t(v * 5 + 7), uint8_t(v * 3), 255 };
            memcpy(&pixels[i * 4], i % 7 ? p : clear, 4);
        }
        uint8_t indices[1024];
        uint16_t palette[256];
        quantizeCI8(pixels.data(), 1024, indices, palette);
        bool exact = true, transparent = palette[0] == 0;
        for (unsigned i = 0; i < 1024; ++i)
        {
            exact = exact && palette[indices[i]] == rgb5a3(&pixels[i * 4]);
            transparent = transparent && (rgb5a3(&pixels[i * 4]) == 0) == (indices[i] == 0);
        }
        check(transparent, "CI8 transparency is entry 0 and nothing else");
        if (distinct == 200)
            check(exact, "CI8 keeps a small palette exact");
    }

    printf("ok: %d cases\n", cases);
}
