/**
 * @file main.cpp
 * @brief Entry point of the `duckling` executable.
 *
 * Commands so far: `version` (task 0.2), `status` (task 1.5), `switch`
 * (task 1.6) and `sniff` (task 1.7). `run` and `gen-secret` arrive with M2,
 * see DEVPLAN.md.
 */
#include <cstdio>
#include <string>
#include <string_view>

#include "app/build_info.hpp"
#include "app/sniff.hpp"
#include "app/status.hpp"
#include "app/switch_host.hpp"

namespace {

/**
 * @brief Print usage to stderr.
 * @param argv0 Program name as invoked.
 */
void printUsage(const char* argv0) {
    std::fprintf(stderr,
                 "usage: %s <command>\n"
                 "\n"
                 "commands:\n"
                 "  status     show the receiver, paired devices, this host's channel\n"
                 "             on each device and the devices' host-name tables\n"
                 "  switch <mouse|keyboard|all> <channel|hostname>\n"
                 "             send devices connected here to another channel (1-3)\n"
                 "             or to the channel named after a host. The target\n"
                 "             isn't checked for being on: a device sent to a host\n"
                 "             that is off stays offline until you press its\n"
                 "             Easy-Switch button\n"
                 "  sniff [seconds]\n"
                 "             print every HID++ report from the receiver, labelled.\n"
                 "             Type a label + Enter to insert a marker. Runs until\n"
                 "             Ctrl+C, or for the given number of seconds\n"
                 "  version    print version and build information\n"
                 "  help       show this text\n",
                 argv0);
}

}  // namespace

/**
 * @brief Program entry point.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on success, 1 if the command failed, 2 on a usage error.
 */
int main(int argc, char* argv[]) {
    const char* argv0 = (argc > 0 && argv[0] != nullptr) ? argv[0] : "duckling";
    if (argc < 2) {
        printUsage(argv0);
        return 2;
    }
    const std::string_view command = argv[1];
    const int args = argc - 2;
    if (command == "switch") {
        // runSwitch prints the usage text for empty arguments.
        if (args != 2) return duckling::app::runSwitch("", "");
        return duckling::app::runSwitch(argv[2], argv[3]);
    }
    if (command == "sniff") {
        int seconds = 0;
        if (args == 1) {
            const std::string_view arg = argv[2];
            if (arg.empty() || arg.size() > 6 ||
                arg.find_first_not_of("0123456789") != std::string_view::npos) {
                std::fprintf(stderr, "usage: %s sniff [seconds]\n", argv0);
                return 2;
            }
            seconds = std::stoi(std::string(arg));
        } else if (args > 1) {
            std::fprintf(stderr, "usage: %s sniff [seconds]\n", argv0);
            return 2;
        }
        return duckling::app::runSniff(seconds);
    }
    if (args > 0) {
        std::fprintf(stderr, "duckling: '%.*s' takes no arguments\n",
                     static_cast<int>(command.size()), command.data());
        return 2;
    }

    if (command == "version" || command == "--version" || command == "-V") {
        std::puts(duckling::app::buildInfo().c_str());
        return 0;
    }
    if (command == "status") return duckling::app::runStatus();
    if (command == "help" || command == "--help" || command == "-h") {
        printUsage(argv0);
        return 0;
    }

    std::fprintf(stderr, "duckling: unknown command '%.*s'\n\n",
                 static_cast<int>(command.size()), command.data());
    printUsage(argv0);
    return 2;
}
