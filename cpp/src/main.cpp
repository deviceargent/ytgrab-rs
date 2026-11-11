#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "error.hpp"
#include "runner.hpp"

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    ytgrab::cli::Cli cli;
    try {
        cli = ytgrab::cli::parse(args);
    } catch (const ytgrab::Error& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    if (cli.help) {
        std::cout << ytgrab::cli::help_text();
        return 0;
    }
    if (cli.version) {
        std::cout << ytgrab::cli::version_text() << "\n";
        return 0;
    }

    try {
        ytgrab::runner::run(cli);
        return 0;
    } catch (const ytgrab::Error& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
