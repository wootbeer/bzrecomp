#ifndef __BZ_CLI_H__
#define __BZ_CLI_H__

#include <string>
#include <vector>

// Portable by default, and the command-line options (src/main/cli.cpp).
namespace bz::cli {
    // A new install keeps its settings and saves in the game's folder: makes
    // portable.txt there unless settings already exist in the usual place
    // (those users are left as they are), the folder can't be written to, or
    // the game runs as an AppImage. Call before recomp::register_config_path.
    void make_portable_by_default();

    // Reads the command line. Call after recompui::config::finalize() and
    // after the games are registered. Settings given are used for this launch
    // (saved too with --save-settings); --rom copies in a ROM like the
    // launcher's Select ROM. The arguments for recomp::start go in out_args
    // (--play becomes the runtime's --game bz). Returns false to quit
    // (--help, or a mistake on the command line).
    bool apply(int argc, char** argv, std::vector<std::string>& out_args);
}

#endif
