#include "xtemplate/nodes.hpp"
#include "xtemplate/xtemplate.hpp"
#include "xtemplate/xtemplate_hardcoded.h"
#include <iostream>
#include <unordered_map>

int main(int argc, char *argv[]) {
    if (argc == 1) {
        // GUI mode
        return run(argc, argv);
    } else {
        // Cli mode
        int i = 1;
        std::string xfile;
        std::string xtemplate_name;
        bool list_xtemplates = false;
        bool list_vars = false;
        std::unordered_map<std::string, std::string> vars;
        unsigned char indent = 0;
        while (i < argc) {
            std::string arg = argv[i];
            if (arg.compare("-h") == 0) {
                std::cout << "Usage: xtemplate [OPTIONS]\n\n"
                             "Options:\n"
                             "  -h, --help            Show this help message "
                             "and exit\n"
                             "  --xfile PATH          Xtemplate file to use\n"
                             "  --xtemplate TEXT      Xtemplate to use, "
                             "(default: last cached or stored default from "
                             "~/.config/xtemplate.ini accordignly to their "
                             "priorities)\n"
                             "  --indent INT          Additional indent for "
                             "xtemplate's bodies\n"
                             "  --list-xtemplates     List xtemplates\n"
                             "  --list-variables      List vars for the "
                             "choosen xtempalte\n"
                             "  --<var> <value>       Set variable <var> to "
                             "<value>. Vars tagged [REQUIRED] must be set "
                             "(non-empty) for rendering\n";
                return 0;
            } else if ((arg.compare("--xfile") == 0) && (i + 1 < argc)) {
                xfile = std::string(argv[++i]);
            } else if ((arg.compare("--xtemplate") == 0) && (i + 1 < argc)) {
                xtemplate_name = std::string(argv[++i]);
            } else if ((arg.compare("--indent") == 0) && (i + 1 < argc)) {
                indent = static_cast<unsigned char>(std::stoi(argv[++i]));
            } else if ((arg.compare("--list-xtemplates") == 0)) {
                list_xtemplates = true;
            } else if ((arg.compare("--list-variables") == 0)) {
                list_vars = true;
            } else if ((arg.find("--") == 0) && (i + 1 < argc)) {
                vars.insert(std::make_pair(arg.substr(2), argv[++i]));
            }
            i++;
        }
        if (xtemplate_name.empty() && !list_xtemplates) {
            std::cerr << "Use --xtemplate arg for selecting concrete xtemplate "
                         "from xtemplates file"
                      << std::endl;
            return 1;
        }
        XTemplateConfig config;
        std::string config_path =
            std::string(getenv("HOME")) + "/.config/xtemplate.ini";
        config.default_path =
            std::string(getenv("HOME")) + "/.config/xtemplate.txt";
        std::string last_path;
        std::string errors;
        int ret = parse_and_apply_config(config_path, config, errors);
        if (!errors.empty()) {
            std::cerr << "Error parsing config file: " << errors << std::endl;
            return ret;
        }
        if (!xfile.empty())
            config.active_path = xfile;
        errors.clear();
        std::vector<XTemplateColsCLI> xtemplates;
        ret = parse_xtemplate_content_hardcoded_cli(XTEMPLATE_CONTENT_HARDCODED,
                                                    xtemplates);
        ret = parse_xfile_cli(config.active_path, xtemplates, errors);
        if (!errors.empty()) {
            std::cerr << "Error parsing xtemplate file: " << errors
                      << std::endl;
            return ret;
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
            const std::vector<std::string> &types =
                matched_xtemplate.vars_types;
            const std::vector<std::string> &names =
                matched_xtemplate.vars_names;
            const std::vector<std::vector<std::string>> &tags =
                matched_xtemplate.vars_tags;
            for (size_t i = 0; i < types.size() && i < names.size(); i++) {
                std::cout << names[i] << ": " << types[i];
                if (i < tags.size() && !tags[i].empty()) {
                    std::cout << " [";
                    for (size_t t = 0; t < tags[i].size(); ++t) {
                        if (t)
                            std::cout << "-";
                        std::cout << tags[i][t];
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
        std::string required_errors;
        for (size_t i = 0; i < matched_xtemplate.vars_names.size(); ++i) {
            const std::string &name = matched_xtemplate.vars_names[i];
            for (const std::string &tag : matched_xtemplate.vars_tags[i]) {
                if (tag != "REQUIRED")
                    continue;
                auto it = vars.find(name);
                if (it == vars.end() || it->second.empty()) {
                    required_errors +=
                        "- Variable " + name + " is REQUIRED but empty\n";
                }
            }
        }
        if (!required_errors.empty()) {
            std::cerr << "Cannot render — required vars missing:\n"
                      << required_errors;
            return 1;
        }
        std::string result;
        std::string render_error;
        ParseDiagnostics diag;
        std::unordered_map<std::string,
                           std::unordered_map<std::string, std::string>>
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

        if (!render_xtemplate(matched_xtemplate.body, vars, result,
                              config.render_empty_vals, &diag, &render_error,
                              0 /*top-level indent*/, &xctx)) {
            std::cerr << "Template ##ERROR: " << render_error << std::endl;
            return 1;
        }
        if (!diag.empty()) {
            std::cerr << "Template parse warnings:\n" << diag.join();
        }
        std::cout << result;
        return 0;
    }
}
