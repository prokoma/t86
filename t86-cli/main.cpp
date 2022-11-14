#include "parser.h"

using namespace tiny::t86;

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        std::cout << "Usage: t86-cli\n  File from stdin\n";
        return 1;
    }
    Parser parser(std::cin);
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
    while (!cpu.halted()) {
        cpu.tick();
    }
}