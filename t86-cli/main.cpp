#include <fstream>
#include <iostream>

#include "../common/config.h"
#include "parser.h"

using namespace tiny::t86;

const char* usage_str = R"(
Usage: t86-cli command
commands:
    run input - Parses input, which must be valid T86 assembly file, and runs it on the VM.
)";

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        std::cerr << usage_str;
        return 1;
    }
    std::fstream f(argv[2]);
    if (!f) {
        std::cerr << "Unable to open file `" << argv[2] << "`\n";
        return 3;
    }

    if(argc > 3)
        tiny::config.parse(argc - 2, argv + 2); // parse additional options after input file
    
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
}
