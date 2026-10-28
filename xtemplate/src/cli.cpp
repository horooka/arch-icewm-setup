#include "xtemplate/cli.hpp"
#include "xtemplate/nodes.hpp"
#include "xtemplate/xtemplate_hardcoded.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void print_usage(std::ostream &out) {
    out << "Usage: xtemplate [OPTIONS]\n\n"
           "Options:\n"
           "  -h, --help            Show this help message and exit\n"
           "  --xfile PATH          Xtemplate file to use\n"
           "  --xtemplate NAME      Xtemplate to render\n"
           "  --indent INT          Additional indent for nested xtemplate "
           "bodies (0..255)\n"
           "  -v, --var NAME=VALUE  Set a template variable (repeatable)\n"
           "  --NAME VALUE          Legacy form: set variable NAME\n"
           "  --                    Treat all remaining arguments as "
           "NAME=VALUE assignments\n"
           "  --check               Parse and validate without running "
           "##COMMAND; prints OK on success\n"
           "  --list-xtemplates     List xtemplates\n"
           "  --list-variables      List vars for the chosen xtemplate\n";
}

int usage_error(const std::string &message) {
    std::cerr << "xtemplate: " << message << "\n"
              << "Try 'xtemplate -h' for usage.\n";
    return 2;
}

bool assign_var(const std::string &spec,
                std::unordered_map<std::string, std::string> &vars,
                std::string &error) {
    const size_t eq = spec.find('=');
    if (eq == std::string::npos || eq == 0) {
        error = "expected NAME=VALUE, got: " + spec;
        return false;
    }
    vars[spec.substr(0, eq)] = spec.substr(eq + 1);
    return true;
}

} // namespace

int run_cli(int argc, char *argv[]) {
    if (argc <= 1) {
        print_usage(std::cerr);
        return 2;
    }

    int i = 1;
    std::string xfile;
    std::string xtemplate_name;
    bool list_xtemplates = false;
    bool list_vars = false;
    bool check_only = false;
    std::unordered_map<std::string, std::string> vars;
    unsigned char indent = 0;

    while (i < argc) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(std::cout);
            return 0;
        } else if (arg == "--xfile") {
            if (i + 1 >= argc)
                return usage_error("--xfile requires PATH");
            xfile = argv[++i];
        } else if (arg == "--xtemplate") {
            if (i + 1 >= argc)
                return usage_error("--xtemplate requires NAME");
            xtemplate_name = argv[++i];
        } else if (arg == "--indent") {
            if (i + 1 >= argc)
                return usage_error("--indent requires INT");
            try {
                const std::string value_arg = argv[++i];
                size_t consumed = 0;
                const int value = std::stoi(value_arg, &consumed);
                if (consumed != value_arg.size())
                    return usage_error("--indent must be an integer");
                if (value < 0 || value > 255)
                    return usage_error("--indent must be between 0 and 255");
                indent = static_cast<unsigned char>(value);
            } catch (...) {
                return usage_error("--indent must be an integer");
            }
        } else if (arg == "--list-xtemplates") {
            list_xtemplates = true;
        } else if (arg == "--list-variables") {
            list_vars = true;
        } else if (arg == "--check") {
            check_only = true;
        } else if (arg == "-v" || arg == "--var") {
            if (i + 1 >= argc)
                return usage_error("--var requires NAME=VALUE");
            std::string error;
            if (!assign_var(argv[++i], vars, error))
                return usage_error(error);
        } else if (arg.rfind("--var=", 0) == 0) {
            std::string error;
            if (!assign_var(arg.substr(6), vars, error))
                return usage_error(error);
        } else if (arg == "--") {
            // Everything after "--" is a NAME=VALUE assignment; this keeps
            // values starting with '-' unambiguous.
            for (++i; i < argc; ++i) {
                std::string error;
                if (!assign_var(argv[i], vars, error))
                    return usage_error(error);
            }
            break;
        } else if (arg.rfind("--", 0) == 0 && arg.size() > 2) {
            // Backward-compatible form: --NAME VALUE.
            if (i + 1 >= argc)
                return usage_error("option " + arg + " requires a value");
            vars[arg.substr(2)] = argv[++i];
        } else {
            return usage_error("unknown option: " + arg);
        }
        ++i;
    }

    if (check_only && (list_xtemplates || list_vars))
        return usage_error("--check cannot be combined with --list-*");
    if (list_vars && xtemplate_name.empty())
        return usage_error("--list-variables requires --xtemplate NAME");
    if (check_only && xtemplate_name.empty())
        return usage_error("--check requires --xtemplate NAME");
    if (xtemplate_name.empty() && !list_xtemplates)
        return usage_error("use --xtemplate NAME (or --list-xtemplates)");

    XTemplateConfig config;
    const std::string home = getenv("HOME") ? getenv("HOME") : "";
    const std::string config_path = home + "/.config/xtemplate.d/xtemplate.ini";
    config.default_path = home + "/.config/xtemplate.d/xtemplate.txt";

    std::string errors;
    int ret = parse_and_apply_config(config_path, config, errors);
    if (ret != 0 || !errors.empty()) {
        // A config file is optional when the caller supplies --xfile.
        if (ret == ENOENT && !xfile.empty()) {
            errors.clear();
        } else {
            std::cerr << "Error parsing config file:\n" << errors;
            return 1;
        }
    }
    if (!xfile.empty())
        config.active_path = xfile;
    if (config.active_path.empty()) {
        std::cerr << "No xtemplate file selected; use --xfile PATH\n";
        return 1;
    }

    errors.clear();
    std::vector<XTemplateColsCLI> xtemplates;
    parse_xtemplate_content_hardcoded_cli(XTEMPLATE_CONTENT_HARDCODED,
                                         xtemplates);
    parse_xfile_cli(config.active_path, xtemplates, errors);
    if (!errors.empty()) {
        std::cerr << "Error parsing xtemplate file:\n" << errors;
        return 1;
    }

    if (list_xtemplates) {
        for (auto &xtemplate : xtemplates) {
            std::cout << xtemplate.name
                      << (xtemplate.is_hardcoded ? " (hardcoded)" : "")
                      << std::endl;
        }
        return 0;
    }

    XTemplateColsCLI matched_xtemplate;
    for (auto &xtemplate : xtemplates) {
        if (xtemplate_name == xtemplate.name) {
            matched_xtemplate = xtemplate;
            break;
        }
    }
    if (matched_xtemplate.name.empty()) {
        std::cerr << "No xtemplate with name " << xtemplate_name
                  << " found in xtemplates file" << std::endl;
        return 1;
    }

    if (list_vars) {
        const std::vector<std::string> &types = matched_xtemplate.vars_types;
        const std::vector<std::string> &names = matched_xtemplate.vars_names;
        const std::vector<std::vector<std::string>> &tags =
            matched_xtemplate.vars_tags;
        for (size_t vi = 0; vi < types.size() && vi < names.size(); vi++) {
            std::cout << names[vi] << ": " << types[vi];
            if (vi < tags.size() && !tags[vi].empty()) {
                std::cout << " [";
                for (size_t ti = 0; ti < tags[vi].size(); ++ti) {
                    if (ti)
                        std::cout << "-";
                    std::cout << tags[vi][ti];
                }
                std::cout << "]";
            }
            std::cout << std::endl;
        }
        return 0;
    }

    if (matched_xtemplate.vars_tags.size() <
        matched_xtemplate.vars_names.size())
        matched_xtemplate.vars_tags.resize(
            matched_xtemplate.vars_names.size());

    if (!check_only) {
        std::string required_errors;
        for (size_t vi = 0; vi < matched_xtemplate.vars_names.size(); ++vi) {
            const std::string &name = matched_xtemplate.vars_names[vi];
            for (const std::string &tag : matched_xtemplate.vars_tags[vi]) {
                if (tag != "REQUIRED")
                    continue;
                auto it = vars.find(name);
                if (it == vars.end() || it->second.empty())
                    required_errors +=
                        "- Variable " + name + " is REQUIRED but empty\n";
            }
        }
        if (!required_errors.empty()) {
            std::cerr << "Cannot render — required vars missing:\n"
                      << required_errors;
            return 1;
        }
    }

    std::string result;
    std::string render_error;
    ParseDiagnostics diag;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
        bodies_by_xfile;
    // Seed caches from already loaded templates.
    for (const auto &t : xtemplates) {
        const std::string xf =
            t.is_hardcoded ? "__hardcoded__" : config.active_path;
        bodies_by_xfile[xf][t.name] = t.body;
    }

    auto load_xfile = [&](const std::string &xf) -> bool {
        if (xf == "__hardcoded__")
            return !bodies_by_xfile["__hardcoded__"].empty();
        if (!bodies_by_xfile[xf].empty())
            return true;
        // Load-on-demand for directive-specified -xfile.
        std::vector<XTemplateColsCLI> tmp;
        std::string errs;
        int r = parse_xfile_cli(xf, tmp, errs);
        if (r != 0 || !errs.empty())
            return false;
        for (const auto &t : tmp)
            bodies_by_xfile[xf][t.name] = t.body;
        return !bodies_by_xfile[xf].empty();
    };

    auto resolve = [&](const std::string &name, const std::string &xf,
                       std::string &out_body) -> bool {
        if (xf.empty())
            return false;
        if (bodies_by_xfile[xf].empty() && !load_xfile(xf))
            return false;
        auto it = bodies_by_xfile[xf].find(name);
        if (it == bodies_by_xfile[xf].end())
            return false;
        out_body = it->second;
        return true;
    };

    XTemplateRenderContext xctx;
    xctx.sub_indent = indent;
    xctx.current_xfile = matched_xtemplate.is_hardcoded
                             ? "__hardcoded__"
                             : config.active_path;
    xctx.depth = 0;
    xctx.max_depth = 12;
    xctx.diag = &diag;
    xctx.resolve = resolve;
    xctx.execute_commands = !check_only;

    if (!render_xtemplate(matched_xtemplate.body, vars, result,
                          config.render_empty_vals, &diag, &render_error,
                          0 /*top-level indent*/, &xctx)) {
        std::cerr << "Template ##ERROR: " << render_error << std::endl;
        return 1;
    }
    if (!diag.empty()) {
        std::cerr << "Template parse warnings:\n" << diag.join();
    }
    if (check_only) {
        std::cout << "OK: " << xtemplate_name << std::endl;
        return 0;
    }
    std::cout << result;
    return 0;
}
