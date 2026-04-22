#include <iostream>
#include <string>

void cmd_greet() {
    std::cout << "Hello, World!" << std::endl;
}

void cmd_version() {
    std::cout << "CppExample v1.0.0" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <command>\n"
                  << "Commands:\n"
                  << "  greet    Print a greeting\n"
                  << "  version  Print version info\n";
        return 1;
    }

    std::string command = argv[1];

    if (command == "greet") {
        cmd_greet();
    } else if (command == "version") {
        cmd_version();
    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        return 1;
    }

    return 0;
}
