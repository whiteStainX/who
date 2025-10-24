#include <clocale>
#include <iostream>
#include <thread>
#include <chrono>

#include <notcurses/notcurses.h>

int main() {
    std::setlocale(LC_ALL, "");

    notcurses_options opts{};
    notcurses* nc = notcurses_init(&opts, nullptr);
    if (!nc) {
        std::cerr << "Failed to initialize notcurses" << std::endl;
        return 1;
    }

    if (notcurses_render(nc) != 0) {
        std::cerr << "Failed to render" << std::endl;
        notcurses_stop(nc);
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (notcurses_stop(nc) != 0) {
        std::cerr << "Failed to stop notcurses cleanly" << std::endl;
        return 1;
    }

    return 0;
}
