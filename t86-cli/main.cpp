#include <fstream>
#include <iostream>

#include "../common/config.h"
#include "../t86/utils/stats_logger.h"
#include "parser.h"

using namespace tiny::t86;
using tiny::t86::StatsLogger;
using tiny::config;

const char* usage_str = R"(
Usage: t86-cli command
commands:
    run [-stats] input - Parses input, which must be valid T86 assembly file, and runs it on the VM.
)";

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string(argv[1]) != "run") {
        std::cerr << usage_str;
        return 1;
    }

    config.parse(argc - 1, argv + 1); // skip 'run'

    std::string fname;
    try {
        fname = config.input();
    } catch(std::runtime_error &err) {
        std::cerr << err.what() << "`\n";
        return 2;
    }

    std::fstream f(fname);
    if (!f) {
        std::cerr << "Unable to open file `" << fname << "`\n";
        return 3;
    }

    bool enableStats = !config.setDefaultIfMissing("-stats", "");

    if(enableStats)
        StatsLogger::instance().enableLoggingAndReset();
    
    Parser parser(f);
    tiny::t86::Program program;
    try {
        program = parser.Parse();
#ifdef LOGGER
        program.dump();
#endif
    } catch (ParserError &err) {
        std::cerr << err.what() << std::endl;
        return 2;
    }

    tiny::t86::Cpu cpu{};

    cpu.start(std::move(program));
    try {
        while (!cpu.halted()) {
            cpu.tick();
        }
    } catch(std::exception &ex) {
        utils::output(std::cerr, "Exception {} while ticking CPU: {}", typeid(ex).name(), ex.what());
        std::cerr << std::endl;
        cpu.dumpState(std::cerr);
        throw ex; // rethrow for debugger
    }

    if(enableStats)
        StatsLogger::instance().processBasicStats(std::cerr);
}
