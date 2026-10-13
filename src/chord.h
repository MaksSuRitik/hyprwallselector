// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <bitset>
#include <linux/input-event-codes.h>
#include <vector>
struct Chord {
    std::bitset<KEY_MAX + 1> down;
    std::vector<unsigned> codes{KEY_LEFTALT, KEY_T};
    bool latched = false;
    static bool modifier(unsigned c) {
        return c == KEY_LEFTALT || c == KEY_RIGHTALT || c == KEY_LEFTCTRL || c == KEY_RIGHTCTRL ||
               c == KEY_LEFTSHIFT || c == KEY_RIGHTSHIFT || c == KEY_LEFTMETA || c == KEY_RIGHTMETA;
    }
    static unsigned counterpart(unsigned c) {
        switch (c) {
        case KEY_LEFTALT:
            return KEY_RIGHTALT;
        case KEY_RIGHTALT:
            return KEY_LEFTALT;
        case KEY_LEFTCTRL:
            return KEY_RIGHTCTRL;
        case KEY_RIGHTCTRL:
            return KEY_LEFTCTRL;
        case KEY_LEFTSHIFT:
            return KEY_RIGHTSHIFT;
        case KEY_RIGHTSHIFT:
            return KEY_LEFTSHIFT;
        case KEY_LEFTMETA:
            return KEY_RIGHTMETA;
        case KEY_RIGHTMETA:
            return KEY_LEFTMETA;
        default:
            return c;
        }
    }
    bool configure(const std::vector<unsigned> &next) {
        if (next.size() < 2 || next.size() > 5)
            return false;
        unsigned keys = 0;
        std::bitset<KEY_MAX + 1> seen;
        for (auto c : next) {
            if (!c || c > KEY_MAX || seen[c])
                return false;
            seen[c] = true;
            if (!modifier(c))
                ++keys;
        }
        if (keys != 1)
            return false;
        codes = next;
        reset();
        return true;
    }
    void reset() {
        down.reset();
        latched = false;
    }
    bool event(unsigned code, int value) {
        if (code > KEY_MAX || value == 2)
            return false;
        down[code] = value == 1;
        bool match = std::all_of(codes.begin(), codes.end(),
                                 [this](unsigned c) { return down[c] || down[counterpart(c)]; });
        for (unsigned c : {KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTCTRL, KEY_RIGHTCTRL, KEY_LEFTSHIFT,
                           KEY_RIGHTSHIFT, KEY_LEFTMETA, KEY_RIGHTMETA})
            if (down[c] && (std::find(codes.begin(), codes.end(), c) == codes.end() &&
                            std::find(codes.begin(), codes.end(), counterpart(c)) == codes.end()))
                match = false;
        const bool fire = match && !latched && value == 1;
        latched = match;
        return fire;
    }
};
