/**
 * @file main.cpp
 * @brief Entry point of the `duckling` executable.
 *
 * Commands so far: `version` (task 0.2) and `status` (task 1.5). The others
 * (`run`, `switch`, `sniff`, `gen-secret`) arrive with M1 and M2, see
 * DEVPLAN.md.
 */
#include <cstdio>
#include <string_view>

#include "app/build_info.hpp"
#include "app/status.hpp"

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
    if (argc > 2) {
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
