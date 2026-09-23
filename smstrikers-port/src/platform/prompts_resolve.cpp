#include "port/prompts_resolve.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace prompts
{

const char* familyName(Family f)
{
    static const char* names[] = { "gamecube",  "xbox",    "playstation", "nintendo",
                                   "steamdeck", "generic", "keyboard" };
    return f >= Gamecube && f <= Keyboard ? names[f] : "auto";
}

Family parseFamily(const char* s)
{
    for (int i = Auto; i <= Keyboard; ++i)
        if (s && strcmp(s, familyName(static_cast<Family>(i))) == 0)
            return static_cast<Family>(i);
    return static_cast<Family>(-2);
}

Input defaults(Family f)
{
    Input in = {};
    in.family = f;
    in.emulateTriggers = true;
    const char* keys[] = { "X", "Z", "C", "V", "Q", "E" };
    for (int i = 0; i < Count; ++i)
    {
        in.bindings[i] = { i < L ? i : -1, i < L ? -1 : i, -1, 1 };
        in.overrides[i] = Unset;
        in.keys[i] = keys[i];
    }
    const Label normal[] = { LabelA, LabelB, LabelX, LabelY };
    const Label sony[] = { Cross, Circle, Square, Triangle };
    const Label nin[] = { LabelB, LabelA, LabelY, LabelX };
    for (int i = 0; i < 4; ++i)
        in.labels[i] = f == Playstation ? sony[i]
                       : f == Nintendo  ? nin[i]
                       : f == Generic   ? Unknown
                                        : normal[i];
    return in;
}

static void copy(char* out, size_t n, const char* s) { snprintf(out, n, "%s", s); }

static void keyNames(Icon& out, const char* name)
{
    if (!name || !*name)
    {
        out.unbound = true;
        name = "-";
    }
    struct Key
    {
        const char* name;
        const char* face;
        const char* wide;
    };
    static const Key named[] = { { "Space", "_", "Space" },
                                 { "Return", "RETURN", "Enter" },
                                 { "Enter", "RETURN", "Enter" },
                                 { "Left Shift", "SHIFT", "Shift" },
                                 { "Right Shift", "SHIFT", "Shift" },
                                 { "Tab", "TAB", "Tab" },
                                 { "Backspace", "BACK", "Bksp" },
                                 { "Escape", "Es", "Esc" },
                                 { "Left Ctrl", "Ct", "Ctrl" },
                                 { "Right Ctrl", "Ct", "Ctrl" },
                                 { "Left Alt", "Al", "Alt" },
                                 { "Right Alt", "Al", "Alt" },
                                 { "Up", "UP", "UP" },
                                 { "Down", "DOWN", "DOWN" },
                                 { "Left", "LEFT", "LEFT" },
                                 { "Right", "RIGHT", "RIGHT" } };
    for (const Key& k : named)
        if (strcmp(name, k.name) == 0)
        {
            copy(out.faceKey, sizeof out.faceKey, k.face);
            copy(out.wideKey, sizeof out.wideKey, k.wide);
            return;
        }
    if (strncmp(name, "Keypad ", 7) == 0 && name[7] >= '0' && name[7] <= '9' && !name[8])
    {
        snprintf(out.faceKey, sizeof out.faceKey, "K%c", name[7]);
        snprintf(out.wideKey, sizeof out.wideKey, "Num%c", name[7]);
        return;
    }
    const bool function = name[0] == 'F' && isdigit(static_cast<unsigned char>(name[1]));
    size_t bytes = strlen(name);
    bool single =
        bytes && (bytes == 1 ||
                  (static_cast<unsigned char>(name[0]) >= 0xc0 &&
                   ((bytes == 2 && static_cast<unsigned char>(name[0]) < 0xe0) ||
                    (bytes == 3 && static_cast<unsigned char>(name[0]) < 0xf0) || bytes == 4)));
    snprintf(out.faceKey, sizeof out.faceKey, "%.*s", int(single ? bytes : function ? 3 : 2), name);
    snprintf(out.wideKey, sizeof out.wideKey, "%.*s", int(single ? bytes : 5), name);
}

Binding bindingFor(const Input& in, Control c)
{
    Binding b = in.bindings[c];
    if (!in.connected)
    {
        b = defaults(in.family).bindings[c];
        const int setting = in.overrides[c];
        if (setting >= 0)
            b.button = setting;
        else if (c >= L && (setting == LeftTrigger || setting == RightTrigger))
        {
            b.button = -1;
            b.axis = setting == LeftTrigger ? 4 : 5;
            b.axisButton = -1;
            b.sign = 1;
        }
    }
    return b;
}

Icon resolve(const Input& in, Control c)
{
    Icon out = {};
    out.family = in.family;
    out.original = -1;
    copy(out.token, sizeof out.token, "unbound");
    if (in.family == Keyboard)
    {
        copy(out.token, sizeof out.token, "key");
        keyNames(out, in.keys[c]);
        return out;
    }
    Binding b = bindingFor(in, c);
    int native = b.button;
    if (native < 0 && c >= L && in.emulateTriggers)
    {
        if (b.axis >= 0)
        {
            if (b.axis == 4 || b.axis == 5)
                native = b.axis == 4 ? LeftTrigger : RightTrigger;
            else
            {
                const char* axisNames[] = { "ls_right", "ls_down", "rs_right", "rs_down" };
                const char* negNames[] = { "ls_left", "ls_up", "rs_left", "rs_up" };
                if (b.axis < 4)
                {
                    copy(out.token, sizeof out.token,
                         b.sign < 0 ? negNames[b.axis] : axisNames[b.axis]);
                    return out;
                }
            }
        }
        else
            native = b.axisButton;
    }
    if (native == LeftTrigger || native == RightTrigger)
    {
        copy(out.token, sizeof out.token, native == LeftTrigger ? "lt" : "rt");
        if (in.family == Gamecube)
            out.original = native == LeftTrigger ? L : R;
    }
    else if (native >= South && native <= North)
    {
        static const char* tokens[] = { "south", "a",      "b",      "x",       "y",
                                        "cross", "circle", "square", "triangle" };
        Label label = in.labels[native];
        if (label == Unknown)
        {
            static const char* positions[] = { "south", "east", "west", "north" };
            copy(out.token, sizeof out.token, positions[native]);
        }
        else
        {
            copy(out.token, sizeof out.token, tokens[label]);
            if (in.family == Gamecube && label >= LabelA && label <= LabelY)
                out.original = label - LabelA;
        }
    }
    else
    {
        static const char* names[] = { "back",  "guide", "start", "ls",   "rs",    "lb",
                                       "rb",    "up",    "down",  "left", "right", "misc1",
                                       "p1",    "p2",    "p3",    "p4",   "touch", "misc2",
                                       "misc3", "misc4", "misc5", "misc6" };
        if (native >= Back && native <= Misc6)
            copy(out.token, sizeof out.token, names[native - Back]);
        if (in.family == Gamecube && (native == Misc3 || native == Misc4))
        {
            out.original = native == Misc3 ? L : R;
            copy(out.token, sizeof out.token, native == Misc3 ? "lt" : "rt");
        }
    }
    out.unbound = strcmp(out.token, "unbound") == 0;
    return out;
}

bool originalFits(const Icon& icon, Control target)
{
    return icon.original >= 0 && ((icon.original < L) == (target < L));
}

int originalLegend(const Icon& icon, const bool loaded[4], int targetSlot)
{
    if (targetSlot == 3 && icon.original == B && loaded[3])
        return 3;
    const int i = icon.original == A ? 0 : icon.original == B ? 1 : icon.original == Y ? 2 : -1;
    return i >= 0 && loaded[i] ? i : -1;
}

size_t c8Size(unsigned w, unsigned h) { return size_t((w + 7) / 8) * ((h + 3) / 4) * 32; }

void c8Tile(const uint8_t* rows, unsigned w, unsigned h, uint8_t* out)
{
    for (unsigned by = 0; by < h; by += 4)
        for (unsigned bx = 0; bx < w; bx += 8)
            for (unsigned y = 0; y < 4; ++y)
                for (unsigned x = 0; x < 8; ++x)
                    *out++ = bx + x < w && by + y < h ? rows[(by + y) * w + bx + x] : 0;
}

Image trim(const Image& image)
{
    unsigned x0 = image.width, y0 = image.height, x1 = 0, y1 = 0;
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x)
            if (image.rgba[(size_t(y) * image.width + x) * 4 + 3])
            {
                x0 = std::min(x0, x);
                y0 = std::min(y0, y);
                x1 = std::max(x1, x + 1);
                y1 = std::max(y1, y + 1);
            }
    Image out = {};
    if (x1 == 0)
    {
        out.width = out.height = 1;
        out.rgba.assign(4, 0);
        return out;
    }
    out.width = x1 - x0;
    out.height = y1 - y0;
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (unsigned y = 0; y < out.height; ++y)
        memcpy(&out.rgba[size_t(y) * out.width * 4],
               &image.rgba[(size_t(y0 + y) * image.width + x0) * 4], size_t(out.width) * 4);
    return out;
}

static void scale(const Image& src, unsigned w, unsigned h, uint8_t* out)
{
    const double sw = src.width, sh = src.height;
    for (unsigned y = 0; y < h; ++y)
    {
        const double y0 = y * sh / h, y1 = (y + 1) * sh / h;
        for (unsigned x = 0; x < w; ++x)
        {
            const double x0 = x * sw / w, x1 = (x + 1) * sw / w;
            double total[4] = {};
            for (unsigned sy = unsigned(y0); sy < src.height && sy <= unsigned(y1); ++sy)
            {
                const double wy = std::max(0.0, std::min(y1, sy + 1.0) - std::max(y0, double(sy)));
                for (unsigned sx = unsigned(x0); sx < src.width && sx <= unsigned(x1); ++sx)
                {
                    const double wx =
                        std::max(0.0, std::min(x1, sx + 1.0) - std::max(x0, double(sx)));
                    const uint8_t* p = &src.rgba[(size_t(sy) * src.width + sx) * 4];
                    const double a = p[3] * wx * wy;
                    for (int c = 0; c < 3; ++c)
                        total[c] += p[c] * a;
                    total[3] += a;
                }
            }
            uint8_t* o = out + (size_t(y) * w + x) * 4;
            for (int c = 0; c < 3; ++c)
                o[c] = total[3] > 0 ? uint8_t(std::lround(total[c] / total[3])) : 0;
            o[3] = uint8_t(std::min(255L, std::lround(total[3] / ((x1 - x0) * (y1 - y0)))));
        }
    }
}

Image place(const Image& art, Family family, const char* token, Form form)
{
    const bool menu = form == Menu, wide = form == Wide;
    const unsigned cellScale = menu ? 1 : 4;
    const unsigned cw = (menu ? 32 : wide ? 73 : 43) * cellScale;
    const unsigned ch = (menu ? 32 : 45) * cellScale;

    static const char* round[] = { "a",      "b",        "x",     "y",    "cross", "circle",
                                   "square", "triangle", "south", "east", "west",  "north" };
    double maxw = menu ? 28 : family == Keyboard && wide ? 54 : wide ? 45 : 32;
    const double maxh = family == Keyboard ? 27 : 28;
    for (const char* r : round)
        if (strcmp(token, r) == 0)
            maxw = 28;

    const double ratio = std::min(maxw / art.width, maxh / art.height) * cellScale;
    const unsigned w = unsigned(std::max(1L, std::lround(art.width * ratio)));
    const unsigned h = unsigned(std::max(1L, std::lround(art.height * ratio)));
    std::vector<uint8_t> scaled(size_t(w) * h * 4);
    scale(art, w, h, scaled.data());

    Image out = { cw, ch, std::vector<uint8_t>(size_t(cw) * ch * 4, 0) };
    const long x0 = std::lround((menu ? 16 : wide ? 39.5 : 26) * cellScale - w / 2.0);
    const long y0 = std::lround(16.0 * cellScale - h / 2.0);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x)
        {
            const long tx = x0 + long(x), ty = y0 + long(y);
            if (tx >= 0 && ty >= 0 && tx < long(cw) && ty < long(ch))
                memcpy(&out.rgba[(size_t(ty) * cw + size_t(tx)) * 4],
                       &scaled[(size_t(y) * w + x) * 4], 4);
        }
    return out;
}

uint16_t rgb5a3(const uint8_t* p)
{
    if (p[3] >= 224)
        return 0x8000 | ((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3);
    if ((p[3] >> 5) == 0)
        return 0;
    return ((p[3] >> 5) << 12) | ((p[0] >> 4) << 8) | ((p[1] >> 4) << 4) | (p[2] >> 4);
}

static int channel(uint16_t c, int k)
{
    if (c & 0x8000)
    {
        const int v[4] = { (c >> 10) & 31, (c >> 5) & 31, c & 31, 31 };
        return v[k] * 255 / 31;
    }
    const int v[4] = { ((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17,
                       ((c >> 12) & 7) * 255 / 7 };
    return v[k];
}

void quantizeCI8(const uint8_t* rgba, unsigned count, uint8_t* indices, uint16_t* palette)
{
    typedef std::vector<std::pair<uint16_t, unsigned>> Box;

    std::map<uint16_t, unsigned> weights;
    for (unsigned i = 0; i < count; ++i)
        if (const uint16_t c = rgb5a3(rgba + i * 4))
            ++weights[c];

    std::vector<Box> boxes;
    if (weights.size() <= 255)
    {
        for (const auto& entry : weights)
            boxes.push_back(Box(1, entry));
    }
    else
        boxes.push_back(Box(weights.begin(), weights.end()));

    while (boxes.size() < 255)
    {
        size_t best = boxes.size();
        int bestChannel = 0;
        unsigned long long bestScore = 0;
        for (size_t b = 0; b < boxes.size(); ++b)
        {
            if (boxes[b].size() < 2)
                continue;
            unsigned long long weight = 0;
            for (const auto& entry : boxes[b])
                weight += entry.second;
            for (int k = 0; k < 4; ++k)
            {
                int lo = 255, hi = 0;
                for (const auto& entry : boxes[b])
                {
                    lo = std::min(lo, channel(entry.first, k));
                    hi = std::max(hi, channel(entry.first, k));
                }
                if (static_cast<unsigned long long>(hi - lo) * weight > bestScore)
                {
                    bestScore = static_cast<unsigned long long>(hi - lo) * weight;
                    best = b;
                    bestChannel = k;
                }
            }
        }
        if (best == boxes.size())
            break;

        Box& box = boxes[best];
        std::stable_sort(box.begin(), box.end(),
                         [bestChannel](const std::pair<uint16_t, unsigned>& a,
                                       const std::pair<uint16_t, unsigned>& b) {
                             return channel(a.first, bestChannel) < channel(b.first, bestChannel);
                         });
        unsigned long long total = 0;
        for (const auto& entry : box)
            total += entry.second;
        size_t cut = 1;
        unsigned long long running = box[0].second;
        while (cut + 1 < box.size() && running * 2 < total)
            running += box[cut++].second;
        Box upper(box.begin() + cut, box.end());
        box.resize(cut);
        boxes.push_back(std::move(upper));
    }

    std::fill(palette, palette + 256, 0);
    std::map<uint16_t, uint8_t> index;
    for (size_t b = 0; b < boxes.size(); ++b)
    {
        const Box& box = boxes[b];
        if (box.empty())
            continue;
        unsigned long long sum[4] = {}, total = 0;
        for (const auto& entry : box)
        {
            for (int k = 0; k < 4; ++k)
                sum[k] += static_cast<unsigned long long>(channel(entry.first, k)) * entry.second;
            total += entry.second;
        }
        uint8_t mean[4];
        for (int k = 0; k < 4; ++k)
            mean[k] = uint8_t((sum[k] + total / 2) / total);
        palette[b + 1] = box.size() == 1 ? box[0].first : rgb5a3(mean);
        for (const auto& entry : box)
            index[entry.first] = uint8_t(b + 1);
    }
    for (unsigned i = 0; i < count; ++i)
    {
        const uint16_t c = rgb5a3(rgba + i * 4);
        indices[i] = c ? index[c] : 0;
    }
}

}   // namespace prompts
