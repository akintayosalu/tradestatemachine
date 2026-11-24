// The engine as a program: run one input log, write the output log.
//
//   ./build/debug/app input.log > output.log
//
// Formats: docs/io_format.md. Engine output goes to stdout and nothing else
// does (diagnostics go to stderr), so the output can be compared byte for byte.
// On a Bug the output ends with the BUG line, then the program aborts
// (design.md section 6): fail-stop, after everything written so far is out.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "engine/engine.hpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <input.log>\n";
        return 2;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "error: can't open " << argv[1] << "\n";
        return 1;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    RunResult result = runLog(contents.str());

    std::cout << result.output;
    std::cout.flush();  // the output, including the BUG line, must be out before aborting

    if (result.bug) {
        std::cerr << "engine stopped on a Bug (see the BUG line); aborting\n";
        std::abort();
    }
    return 0;
}
