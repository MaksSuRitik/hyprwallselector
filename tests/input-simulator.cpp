// SPDX-License-Identifier: GPL-3.0-or-later
// Test utility, NEVER installed. Generates only a fixed Alt+F12 chord.
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <unistd.h>
int main() {
    int fd = open("/dev/uinput", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return 1;
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int k = KEY_ESC; k <= KEY_F12; ++k)
        ioctl(fd, UI_SET_KEYBIT, k);
    uinput_setup s{};
    s.id.bustype = BUS_VIRTUAL;
    s.id.vendor = 0x1209;
    s.id.product = 0x4857;
    strcpy(s.name, "Hyprwallselector test keyboard");
    if (ioctl(fd, UI_DEV_SETUP, &s) < 0 || ioctl(fd, UI_DEV_CREATE) < 0)
        return 2;
    usleep(600000);
    auto key = [&](int code, int value) {
        input_event e{};
        e.type = EV_KEY;
        e.code = code;
        e.value = value;
        write(fd, &e, sizeof(e));
        e.type = EV_SYN;
        e.code = SYN_REPORT;
        e.value = 0;
        write(fd, &e, sizeof(e));
        usleep(30000);
    };
    key(KEY_LEFTALT, 1);
    key(KEY_F12, 1);
    key(KEY_F12, 0);
    key(KEY_LEFTALT, 0);
    usleep(200000);
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
}
