// Portable by default, and the command-line options: almost every setting in
// the menu (General, Graphics, Sound, Cheats and Hacks), plus --rom, --play,
// --save-settings and --help. A setting's option is its id with dashes
// (--screen-filter crt); the ones whose ids are unclear have plainer names
// too (--resolution for res_option). The list --help shows is made from the
// settings themselves, so new settings show up in it on their own.
#include "bz_cli.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#ifdef _WIN32
#include <Windows.h>
#include <shellapi.h>
#include "SDL.h"
#else
#include "SDL2/SDL.h"
#endif

#include "librecomp/config.hpp"
#include "librecomp/game.hpp"
#include "recompui/config.h"
#include "ultramodern/config.hpp"
#include "util/file.h"

namespace {
    const std::u8string game_id = u8"bz.n64.us.1.0";
    const std::string mod_game_id = "bz";

    // The tabs whose settings can be given, in the order --help lists them.
    const std::vector<std::string> config_ids = { "general", "graphics", "sound", "cheats", "hacks" };

    // Plainer names for the settings whose ids aren't.
    const std::unordered_map<std::string, std::string> aliases = {
        { "resolution", "res_option" },
        { "window_mode", "wm_option" },
        { "hud_ratio", "hr_option" },
        { "graphics_api", "api_option" },
        { "aspect_ratio", "ar_option" },
        { "msaa", "msaa_option" },
        { "antialiasing", "msaa_option" },
        { "framerate", "rr_option" },
        { "framerate_limit", "rr_manual_value" },
        { "high_precision_framebuffer", "hpfb_option" },
        { "downsampling", "ds_option" },
        { "volume", "main_volume" },
    };

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // For comparing values: lower case, without spaces, dashes and underscores.
    std::string squash(const std::string& s) {
        std::string out;
        for (unsigned char c : s) {
            if (c != ' ' && c != '-' && c != '_') {
                out += (char)std::tolower(c);
            }
        }
        return out;
    }

    std::string dashed(std::string id) {
        std::replace(id.begin(), id.end(), '_', '-');
        return id;
    }

    void show(const std::string& title, const std::string& text, bool error) {
        fprintf(error ? stderr : stdout, "%s\n", text.c_str());
        fflush(error ? stderr : stdout);
        SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION,
            title.c_str(), text.c_str(), nullptr);
    }

    std::filesystem::path exe_folder() {
#ifdef _WIN32
        std::wstring buf(MAX_PATH, L'\0');
        for (;;) {
            const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
            if (n == 0) {
                return {};
            }
            if (n < buf.size()) {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path{ buf }.parent_path();
#else
        std::error_code ec;
        const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path{} : exe.parent_path();
#endif
    }

    bool is_bool_word(const std::string& s, bool& value) {
        const std::string v = lower(s);
        if (v == "on" || v == "true" || v == "yes" || v == "1") {
            value = true;
            return true;
        }
        if (v == "off" || v == "false" || v == "no" || v == "0") {
            value = false;
            return true;
        }
        return false;
    }

    std::string values_text(const recomp::config::ConfigOption& option) {
        switch (option.type) {
        case recomp::config::ConfigOptionType::Bool:
            return "on|off";
        case recomp::config::ConfigOptionType::Enum: {
            std::string out;
            for (const auto& e : std::get<recomp::config::ConfigOptionEnum>(option.variant).options) {
                if (!out.empty()) {
                    out += "|";
                }
                out += e.key;
            }
            return out;
        }
        case recomp::config::ConfigOptionType::Number: {
            const auto& n = std::get<recomp::config::ConfigOptionNumber>(option.variant);
            char buf[64];
            snprintf(buf, sizeof(buf), "%g-%g%s", n.min, n.max, n.percent ? "%" : "");
            return buf;
        }
        default:
            return "<text>";
        }
    }

    std::string help_text() {
        std::string out =
            "Battlezone: Rise of the Black Dogs Recompiled\n"
            "\n"
            "  --play                Skip the launcher and start the game\n"
            "  --rom <file>          Use this ROM (like Select ROM in the launcher)\n"
            "  --fullscreen          Same as --window-mode fullscreen\n"
            "  --windowed            Same as --window-mode windowed\n"
            "  --save-settings       Keep the settings given here (otherwise they're\n"
            "                        for this launch only)\n"
            "  --help                This list\n"
            "\n"
            "Settings: --<setting> <value> or --<setting>=<value>. On/off settings\n"
            "can also be given alone (on) or as --no-<setting> (off).\n";

        std::unordered_map<std::string, std::string> alias_of;
        for (const auto& [alias, id] : aliases) {
            if (!alias_of.contains(id)) {
                alias_of[id] = alias;
            }
        }
        // Preferred plain names, where an id has two.
        alias_of["msaa_option"] = "msaa";

        for (const std::string& config_id : config_ids) {
            const recomp::config::Config& config = recompui::config::get_config(config_id);
            out += "\n" + config.name + ":\n";
            for (const auto& option : config.get_config_schema().options) {
                if (option.hidden) {
                    continue;
                }
                auto it = alias_of.find(option.id);
                const std::string flag = "--" + dashed(it != alias_of.end() ? it->second : option.id);
                const std::string label = option.name.empty() ? "Manual Framerate" : option.name;
                out += "  " + flag + " " + values_text(option) + "   (" + label + ")\n";
            }
        }
        return out;
    }

    struct Found {
        recomp::config::Config* config = nullptr;
        const recomp::config::ConfigOption* option = nullptr;
    };

    Found find_setting(std::string name) {
        std::replace(name.begin(), name.end(), '-', '_');
        name = lower(name);
        if (auto it = aliases.find(name); it != aliases.end()) {
            name = it->second;
        }
        for (const std::string& config_id : config_ids) {
            recomp::config::Config& config = recompui::config::get_config(config_id);
            if (config.has_option(name)) {
                const auto& option = config.get_option(name);
                if (!option.hidden) {
                    return { &config, &option };
                }
            }
        }
        return {};
    }

    // The value text to the option's value, or an error message.
    bool parse_value(const recomp::config::ConfigOption& option, const std::string& flag, const std::string& text,
                     recomp::config::ConfigValueVariant& value, std::string& error) {
        switch (option.type) {
        case recomp::config::ConfigOptionType::Bool: {
            bool b;
            if (is_bool_word(text, b)) {
                value = b;
                return true;
            }
            break;
        }
        case recomp::config::ConfigOptionType::Enum:
            for (const auto& e : std::get<recomp::config::ConfigOptionEnum>(option.variant).options) {
                if (squash(text) == squash(e.key) || squash(text) == squash(e.name)) {
                    value = e.value;
                    return true;
                }
            }
            break;
        case recomp::config::ConfigOptionType::Number: {
            const auto& n = std::get<recomp::config::ConfigOptionNumber>(option.variant);
            std::string t = text;
            if (!t.empty() && t.back() == '%') {
                t.pop_back();
            }
            char* end = nullptr;
            const double d = std::strtod(t.c_str(), &end);
            if (!t.empty() && end != nullptr && *end == '\0' && d >= n.min && d <= n.max) {
                value = d;
                return true;
            }
            break;
        }
        case recomp::config::ConfigOptionType::String:
            value = text;
            return true;
        default:
            break;
        }
        error = "\"" + text + "\" isn't a value for --" + flag + " (" + option.name + "). It takes: " + values_text(option);
        return false;
    }

    void apply_graphics() {
        recomp::config::Config& g = recompui::config::get_graphics_config();
        namespace opt = recompui::config::graphics::options;
        auto e = [&](const std::string& id) { return std::get<uint32_t>(g.get_option_value(id)); };
        ultramodern::renderer::GraphicsConfig c = ultramodern::renderer::get_graphics_config();
        c.developer_mode = std::get<bool>(g.get_option_value(opt::developer_mode));
        c.res_option = (ultramodern::renderer::Resolution)e(opt::res_option);
        c.wm_option = (ultramodern::renderer::WindowMode)e(opt::wm_option);
        c.hr_option = (ultramodern::renderer::HUDRatioMode)e(opt::hr_option);
        c.api_option = (ultramodern::renderer::GraphicsApi)e(opt::api_option);
        c.ar_option = (ultramodern::renderer::AspectRatio)e(opt::ar_option);
        c.msaa_option = (ultramodern::renderer::Antialiasing)e(opt::msaa_option);
        c.rr_option = (ultramodern::renderer::RefreshRate)e(opt::rr_option);
        c.hpfb_option = (ultramodern::renderer::HighPrecisionFramebuffer)e(opt::hpfb_option);
        c.rr_manual_value = (int)std::get<double>(g.get_option_value(opt::rr_manual_value));
        c.ds_option = (int)e(opt::ds_option);
        ultramodern::renderer::set_graphics_config(c);
    }
}

void bz::cli::make_portable_by_default() {
    std::error_code ec;
    if (std::filesystem::exists("portable.txt", ec)) {
        return;
    }
    // An AppImage runs from a read-only copy of itself.
    if (std::getenv("APPIMAGE") != nullptr) {
        return;
    }
    // Only when started in the game's own folder (as from a shortcut or by
    // double-clicking), not from somewhere else on a command line.
    const auto exe = exe_folder();
    if (exe.empty() || !std::filesystem::equivalent(exe, std::filesystem::current_path(ec), ec) || ec) {
        return;
    }
    // Settings in the usual place: an existing player, left as they are.
    const auto usual = recompui::file::get_app_folder_path();
    if (usual.empty() || std::filesystem::exists(usual, ec)) {
        return;
    }
    std::ofstream file("portable.txt");  // fails quietly in a read-only folder
}

bool bz::cli::apply(int argc, char** argv, std::vector<std::string>& out_args) {
    out_args.clear();
    out_args.emplace_back(argc > 0 ? argv[0] : "BattlezoneRecompiled");

#ifdef _WIN32
    // The ROM path as Windows has it, so names in any language work.
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
#endif

    std::vector<std::string> errors;
    std::vector<recomp::config::Config*> changed;
    bool play = false, save = false, graphics = false;

    auto set = [&](Found found, const recomp::config::ConfigValueVariant& value) {
        found.config->set_option_value(found.option->id, value);
        if (found.config->requires_confirmation) {
            found.config->apply_option_value(found.option->id);
        }
        if (std::find(changed.begin(), changed.end(), found.config) == changed.end()) {
            changed.push_back(found.config);
        }
        if (found.config == &recompui::config::get_graphics_config()) {
            graphics = true;
        }
    };

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg.rfind("--", 0) != 0) {
            errors.push_back("Not an option: " + arg);
            continue;
        }
        std::string name = arg.substr(2), value;
        bool has_value = false;
        if (const size_t eq = name.find('='); eq != std::string::npos) {
            value = name.substr(eq + 1);
            name = name.substr(0, eq);
            has_value = true;
        }
        const std::string key = lower(name);
        auto next_value = [&]() {
            if (!has_value && i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                value = argv[++i];
                has_value = true;
            }
            return has_value;
        };

        if (key == "help" || key == "h") {
            show("Command-line options", help_text(), false);
            return false;
        }
        if (key == "play") {
            play = true;
            continue;
        }
        if (key == "save-settings" || key == "save_settings") {
            save = true;
            continue;
        }
        // The runtime's own (recomp::start reads them).
        if (key == "game" || key == "game-mode") {
            out_args.push_back(arg);
            if (!has_value && i + 1 < argc) {
                out_args.emplace_back(argv[++i]);
            }
            continue;
        }
        if (key == "rom") {
            const bool inline_value = has_value;  // --rom=<file>
            const int at = i + (inline_value ? 0 : 1);
            if (!next_value()) {
                errors.push_back("--rom needs a file: --rom \"path to the ROM\"");
                continue;
            }
            std::filesystem::path path = value;
#ifdef _WIN32
            if (wargv != nullptr && at < wargc) {
                std::wstring w = wargv[at];
                if (const size_t eq = w.find(L'='); inline_value && eq != std::wstring::npos) {
                    w = w.substr(eq + 1);
                }
                path = w;
            }
#else
            (void)at;
            (void)inline_value;
#endif
            switch (recomp::select_rom(path, game_id)) {
            case recomp::RomValidationError::Good:
                break;
            case recomp::RomValidationError::FailedToOpen:
                errors.push_back("Couldn't open the ROM: " + value);
                break;
            case recomp::RomValidationError::IncorrectVersion:
                errors.push_back("That's a different version of Battlezone: Rise of the Black Dogs. The USA version is needed.");
                break;
            default:
                errors.push_back("That's not a Battlezone: Rise of the Black Dogs (USA) ROM: " + value);
                break;
            }
            continue;
        }
        if (key == "fullscreen" || key == "windowed") {
            set(find_setting("window_mode"), (uint32_t)(key == "fullscreen"
                ? ultramodern::renderer::WindowMode::Fullscreen : ultramodern::renderer::WindowMode::Windowed));
            continue;
        }

        Found found = find_setting(name);
        bool negated = false;
        if (found.option == nullptr && key.rfind("no-", 0) == 0) {
            found = find_setting(name.substr(3));
            negated = found.option != nullptr;
        }
        if (found.option == nullptr) {
            errors.push_back("Unknown option: " + arg);
            continue;
        }
        const bool is_bool = found.option->type == recomp::config::ConfigOptionType::Bool;
        if (negated) {
            if (!is_bool || has_value) {
                errors.push_back("Unknown option: " + arg);
            }
            else {
                set(found, false);
            }
            continue;
        }
        if (is_bool && !has_value) {
            // On by itself; a following on/off word is its value.
            bool b = true;
            if (i + 1 < argc && is_bool_word(argv[i + 1], b)) {
                i++;
            }
            set(found, b);
            continue;
        }
        if (!next_value()) {
            errors.push_back("--" + name + " needs a value: " + values_text(*found.option));
            continue;
        }
        recomp::config::ConfigValueVariant parsed;
        std::string error;
        if (parse_value(*found.option, name, value, parsed, error)) {
            set(found, parsed);
        }
        else {
            errors.push_back(error);
        }
    }

#ifdef _WIN32
    if (wargv != nullptr) {
        LocalFree(wargv);
    }
#endif

    if (!errors.empty()) {
        std::string text;
        for (const auto& e : errors) {
            text += e + "\n";
        }
        text += "\nRun with --help for the list of options.";
        show("Command-line options", text, true);
        return false;
    }

    if (graphics) {
        apply_graphics();
    }
    if (save) {
        for (auto* config : changed) {
            config->save_config();
        }
    }
    if (play) {
        out_args.emplace_back("--game");
        out_args.emplace_back(mod_game_id);
    }
    return true;
}
