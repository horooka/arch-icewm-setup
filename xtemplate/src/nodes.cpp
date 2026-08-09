#include "xtemplate/nodes.hpp"
#include "xtemplate/tokenstream.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Conditional directives syntax example:
//
// ##IF_ON use_stdout
// std::cout << "stdout path" << std::endl;
// ##ELIF_EQ level error
// std::cerr << "error path" << std::endl;
// ##ELSE
// std::clog << "fallback" << std::endl;
// ##END
//
// One chain shares a single trailing ##END. ELIF_*/ELSE link via
// Node::cond_chain_next_sibling; render takes the first matching branch.
//
// Soft parse errors go to TokenStream::diag (ParseDiagnostics); invalid
// pieces get safe defaults so parsing continues.

static Node *parse_block(TokenStream &ts, bool stop_at_branch_boundary);
static void parse_condition_chain(TokenStream &ts, Node *if_node);

static Node *new_node(NodeType type) {
    Node *n = new Node;
    n->type = type;
    n->next_sibling = nullptr;
    n->cond_chain_next_sibling = nullptr;
    n->first_child = nullptr;
    return n;
}

static const char *intern_string(const std::string &s) {
    char *p = new char[s.size() + 1];
    std::copy(s.begin(), s.end(), p);
    p[s.size()] = '\0';
    return p;
}

static Node *parse_text(TokenStream &ts) {
    std::string text = ts.read_until_next_directive();
    if (text.empty())
        return nullptr;
    Node *n = new_node(NODE_TEXT);
    n->as.text.content = intern_string(text);
    n->as.text.len = text.size();
    return n;
}

// CLI-like tokenizer: whitespace splits tokens; single quotes keep spaces
// ('hardcoded conditional stdout' → one token). Quotes are stripped.
static std::vector<std::string> split_cli_args(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
        if (i >= s.size())
            break;
        if (s[i] == '\'') {
            ++i;
            size_t j = i;
            while (j < s.size() && s[j] != '\'')
                ++j;
            out.emplace_back(s.substr(i, j - i));
            i = (j < s.size()) ? j + 1 : j;
            continue;
        }
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\n' &&
               s[j] != '\r')
            ++j;
        out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// Empty var → condition never meaningfully matches; empty EQ value → "".
static std::string read_required_cond_var(TokenStream &ts,
                                          const char *directive) {
    std::string var = ts.read_cond_var();
    if (var.empty())
        ts.report(std::string(directive) + " is missing variable name");
    return var;
}

static std::string read_required_cond_value(TokenStream &ts,
                                            const char *directive) {
    std::string val;
    if (!ts.read_value(val))
        ts.report(std::string(directive) +
                  " requires a value after the variable (e.g. ##IF_EQ level "
                  "error)");
    return val;
}

static Node *make_cond_node(CondType type, const std::string &var,
                            const std::string *val) {
    Node *n = new_node(NODE_COND);
    auto *cond = new Cond;
    cond->type = type;
    cond->arg1 = var.empty() ? nullptr : intern_string(var);
    cond->arg2 = val ? intern_string(*val) : nullptr;
    n->as.cond.cond = cond;
    return n;
}

static Node *parse_if_header(TokenStream &ts, CondType type, bool has_value,
                             const char *directive) {
    std::string var = read_required_cond_var(ts, directive);
    std::string val;
    const std::string *pval = nullptr;
    if (has_value) {
        val = read_required_cond_value(ts, directive);
        pval = &val;
    }
    ts.consume_line_end();

    Node *n = make_cond_node(type, var, pval);
    n->first_child = parse_block(ts, true);
    parse_condition_chain(ts, n);
    return n;
}

// Parse ##ELIF_* / ##ELSE … until matching ##END for an IF chain.
static void parse_condition_chain(TokenStream &ts, Node *if_node) {
    Node *tail = if_node;

    while (!ts.eof()) {
        Node *n = nullptr;

        if (ts.peek_is_elif_on()) {
            ts.consume_elif_on();
            std::string var = read_required_cond_var(ts, "##ELIF_ON");
            ts.consume_line_end();
            n = make_cond_node(ELIF_ON, var, nullptr);
            n->first_child = parse_block(ts, true);
        } else if (ts.peek_is_elif_off()) {
            ts.consume_elif_off();
            std::string var = read_required_cond_var(ts, "##ELIF_OFF");
            ts.consume_line_end();
            n = make_cond_node(ELIF_OFF, var, nullptr);
            n->first_child = parse_block(ts, true);
        } else if (ts.peek_is_elif_eq()) {
            ts.consume_elif_eq();
            std::string var = read_required_cond_var(ts, "##ELIF_EQ");
            std::string val = read_required_cond_value(ts, "##ELIF_EQ");
            ts.consume_line_end();
            n = make_cond_node(ELIF_EQ, var, &val);
            n->first_child = parse_block(ts, true);
        } else if (ts.peek_is_elif_neq()) {
            ts.consume_elif_neq();
            std::string var = read_required_cond_var(ts, "##ELIF_NEQ");
            std::string val = read_required_cond_value(ts, "##ELIF_NEQ");
            ts.consume_line_end();
            n = make_cond_node(ELIF_NEQ, var, &val);
            n->first_child = parse_block(ts, true);
        } else if (ts.peek_is_else()) {
            ts.consume_else();
            n = make_cond_node(ELSE, {}, nullptr);
            n->first_child = parse_block(ts, true);
        } else if (ts.peek_is_elif_empty()) {
            ts.consume_elif_empty();
            std::string var = read_required_cond_var(ts, "##ELIF_EMPTY");
            ts.consume_line_end();
            n = make_cond_node(ELIF_EMPTY, var, nullptr);
            n->first_child = parse_block(ts, true);
        } else if (ts.peek_is_elif_nempty()) {
            ts.consume_elif_nempty();
            std::string var = read_required_cond_var(ts, "##ELIF_NEMPTY");
            ts.consume_line_end();
            n = make_cond_node(ELIF_NEMPTY, var, nullptr);
            n->first_child = parse_block(ts, true);
        } else {
            break;
        }

        tail->cond_chain_next_sibling = n;
        tail = n;

        // ELSE must be last in the chain
        if (n->as.cond.cond->type == ELSE)
            break;
    }

    if (ts.peek_is_end())
        ts.consume_end();
    else
        ts.report("##IF chain is missing ##END");
}

static Node *parse_block(TokenStream &ts, bool stop_at_branch_boundary) {
    Node *head = nullptr;
    Node *tail = nullptr;

    auto append = [&](Node *n) {
        if (!n)
            return;
        if (!head)
            head = n;
        else
            tail->next_sibling = n;
        tail = n;
    };

    while (!ts.eof()) {
        // Do not consume: IF header / chain parser owns ELIF/ELSE/END
        if (stop_at_branch_boundary && ts.peek_is_branch_boundary())
            break;

        Node *n = nullptr;

        try {
            if (ts.peek_is_if_on()) {
                ts.consume_if_on();
                n = parse_if_header(ts, ON, false, "##IF_ON");
            } else if (ts.peek_is_if_off()) {
                ts.consume_if_off();
                n = parse_if_header(ts, OFF, false, "##IF_OFF");
            } else if (ts.peek_is_if_eq()) {
                ts.consume_if_eq();
                n = parse_if_header(ts, EQ, true, "##IF_EQ");
            } else if (ts.peek_is_if_neq()) {
                ts.consume_if_neq();
                n = parse_if_header(ts, NEQ, true, "##IF_NEQ");
            } else if (ts.peek_is_if_empty()) {
                ts.consume_if_empty();
                n = parse_if_header(ts, EMPTY, false, "##IF_EMPTY");
            } else if (ts.peek_is_if_nempty()) {
                ts.consume_if_nempty();
                n = parse_if_header(ts, NEMPTY, false, "##IF_NEMPTY");
            } else if (ts.peek_is_set()) {
                ts.consume_set();
                std::string var = read_required_cond_var(ts, "##SET");
                std::string val = ts.read_rest_of_line();
                n = new_node(NODE_SET);
                n->as.assign.name = var.empty() ? nullptr : intern_string(var);
                n->as.assign.value = intern_string(val);
            } else if (ts.peek_is_clear()) {
                ts.consume_clear();
                std::string var = read_required_cond_var(ts, "##CLEAR");
                ts.consume_line_end();
                n = new_node(NODE_CLEAR);
                n->as.assign.name = var.empty() ? nullptr : intern_string(var);
                n->as.assign.value = nullptr;
            } else if (ts.peek_is_error()) {
                ts.consume_error();
                std::string msg = ts.read_rest_of_line();
                n = new_node(NODE_ERROR);
                n->as.text.content = intern_string(msg);
                n->as.text.len = msg.size();
            } else if (ts.peek_directive_keyword("XTEMPLATE")) {
                ts.consume_directive_keyword("XTEMPLATE");
                std::string rest = ts.read_rest_of_line();
                std::vector<std::string> toks = split_cli_args(rest);
                if (toks.empty()) {
                    ts.report("##XTEMPLATE expects template name");
                    n = nullptr;
                } else {
                    const std::string &tpl_name = toks[0];
                    std::string xfile;
                    std::vector<std::string> extra;
                    for (size_t ti = 1; ti < toks.size(); ++ti) {
                        if ((toks[ti] == "-xfile" || toks[ti] == "--xfile") &&
                            ti + 1 < toks.size()) {
                            xfile = toks[ti + 1];
                            ++ti;
                            continue;
                        }
                        extra.push_back(toks[ti]);
                    }
                    n = new_node(NODE_XTEMPLATE);
                    n->as.xt.name = intern_string(tpl_name);
                    n->as.xt.xfile =
                        xfile.empty() ? nullptr : intern_string(xfile);
                    n->as.xt.argc = extra.size();
                    n->as.xt.argv = nullptr;
                    if (!extra.empty()) {
                        n->as.xt.argv = new const char *[extra.size()];
                        for (size_t ai = 0; ai < extra.size(); ++ai)
                            n->as.xt.argv[ai] = intern_string(extra[ai]);
                    }
                }
            } else if (ts.peek_directive_start()) {
                // Unknown / stray ## — keep the directive line as literal text
                // (indent before ## is skipped so it does not leak into output)
                ts.skip_spaces_tabs();
                size_t start = ts.pos;
                while (!ts.eof() && ts.peek() != '\n' && ts.peek() != '\r')
                    ts.advance();
                std::string line(ts.data + start, ts.pos - start);
                ts.consume_line_end();
                if (!line.empty()) {
                    n = new_node(NODE_TEXT);
                    n->as.text.content = intern_string(line);
                    n->as.text.len = line.size();
                }
            } else {
                n = parse_text(ts);
            }
        } catch (const ParseError &ex) {
            // No diagnostics sink: recover by skipping the rest of the line
            // and recording via a local note if somehow reachable with diag.
            if (ts.diag)
                ts.diag->add(ex.what());
            else
                throw;
            ts.consume_line_end();
            n = nullptr;
        }

        append(n);
    }

    return head;
}

Node *Node::parse(const std::string &body, ParseDiagnostics *diag) {
    TokenStream ts(body, diag);
    try {
        return parse_block(ts, false);
    } catch (const ParseError &ex) {
        if (diag) {
            diag->add(ex.what());
            return nullptr;
        }
        throw;
    }
}

void Node::destroy(Node *n) {
    while (n) {
        Node *next_sib = n->next_sibling;
        Node *next_chain = n->cond_chain_next_sibling;
        n->next_sibling = nullptr;
        n->cond_chain_next_sibling = nullptr;
        destroy(n->first_child);
        if (n->type == NODE_TEXT || n->type == NODE_ERROR) {
            delete[] n->as.text.content;
        } else if (n->type == NODE_COND && n->as.cond.cond) {
            delete[] n->as.cond.cond->arg1;
            delete[] n->as.cond.cond->arg2;
            delete n->as.cond.cond;
        } else if (n->type == NODE_XTEMPLATE) {
            if (n->as.xt.name)
                delete[] n->as.xt.name;
            if (n->as.xt.xfile)
                delete[] n->as.xt.xfile;
            if (n->as.xt.argv) {
                for (size_t ai = 0; ai < n->as.xt.argc; ++ai)
                    delete[] n->as.xt.argv[ai];
                delete[] n->as.xt.argv;
            }
        } else if (n->type == NODE_SET || n->type == NODE_CLEAR) {
            if (n->as.assign.name)
                delete[] n->as.assign.name;
            if (n->as.assign.value)
                delete[] n->as.assign.value;
        }
        delete n;
        destroy(next_chain);
        n = next_sib;
    }
}

static std::string
lookup_var(const std::unordered_map<std::string, std::string> &vars,
           const char *name) {
    if (!name)
        return {};
    auto it = vars.find(name);
    if (it == vars.end())
        return {};
    return it->second;
}

static bool
cond_matches(const Cond *cond,
             const std::unordered_map<std::string, std::string> &vars) {
    if (!cond)
        return false;
    if (cond->type == ELSE)
        return true;
    // Soft-parse fallback: missing variable name → never match
    if (!cond->arg1)
        return false;
    std::string cmp_val = lookup_var(vars, cond->arg1);
    switch (cond->type) {
    case ON:
    case ELIF_ON:
        return cmp_val == "ON";
    case OFF:
    case ELIF_OFF:
        return cmp_val == "OFF";
    case EQ:
    case ELIF_EQ:
        return cond->arg2 && cmp_val == cond->arg2;
    case NEQ:
    case ELIF_NEQ:
        return cond->arg2 && cmp_val != cond->arg2;
    case ELSE:
        return true;
    case EMPTY:
    case ELIF_EMPTY:
        return cmp_val.empty();
    case NEMPTY:
    case ELIF_NEMPTY:
        return !cmp_val.empty();
    }
    return false;
}

static void
render_text_node(const std::unordered_map<std::string, std::string> &vars,
                 const char *content, size_t len, std::string &result,
                 bool render_empty_vals, unsigned char append_indent) {
    std::string body(content, len);
    std::string spaces;
    if (append_indent)
        spaces.assign(static_cast<size_t>(append_indent), ' ');
    bool at_line_start = append_indent > 0;

    auto append_raw = [&](const char *p, size_t n) {
        if (append_indent == 0) {
            result.append(p, n);
            return;
        }
        for (size_t k = 0; k < n; ++k) {
            const char c = p[k];
            if (at_line_start && !spaces.empty()) {
                result += spaces;
                at_line_start = false;
            }
            result.push_back(c);
            if (c == '\n')
                at_line_start = true;
        }
    };

    size_t i = 0;
    while (i < body.size()) {
        if (body[i] == '$' && i + 1 < body.size() &&
            (std::isalpha(static_cast<unsigned char>(body[i + 1])) ||
             body[i + 1] == '_')) {
            size_t start = i + 1;
            size_t end = start;
            while (end < body.size() &&
                   (std::isalnum(static_cast<unsigned char>(body[end])) ||
                    body[end] == '_')) {
                ++end;
            }
            std::string name = body.substr(start, end - start);
            auto it = vars.find(name);
            if (it != vars.end() &&
                (!it->second.empty() || render_empty_vals)) {
                append_raw(it->second.data(), it->second.size());
            } else
                append_raw(body.data() + i, end - i);
            i = end;
        } else {
            append_raw(body.data() + i, 1);
            ++i;
        }
    }
}

static std::string
expand_vars_text(const std::unordered_map<std::string, std::string> &vars,
                 const char *content) {
    if (!content)
        return {};
    std::string out;
    render_text_node(vars, content, std::strlen(content), out, true, 0);
    return out;
}

static std::string
resolve_xt_arg(const std::unordered_map<std::string, std::string> &vars,
               const char *token) {
    if (!token)
        return {};
    const std::string raw(token);
    const std::string expanded = expand_vars_text(vars, token);
    // If there was no `$var` expansion (expanded == raw) then allow the
    // directive to reference template vars by bare identifier as well.
    if (expanded == raw) {
        auto it = vars.find(raw);
        if (it != vars.end())
            return it->second;
    }
    return expanded;
}

void Node::render(std::unordered_map<std::string, std::string> &vars,
                  std::string &result, bool render_empty_vals,
                  unsigned char append_indent,
                  const XTemplateRenderContext *xctx) const {
    for (const Node *n = this; n; n = n->next_sibling) {
        switch (n->type) {
        case NODE_TEXT:
            render_text_node(vars, n->as.text.content, n->as.text.len, result,
                             render_empty_vals, append_indent);
            break;
        case NODE_ERROR: {
            // Abort render; message is the ##ERROR line with $vars expanded
            std::string msg;
            render_text_node(vars, n->as.text.content, n->as.text.len, msg,
                             true, append_indent);
            throw RenderError(msg.empty() ? std::string("##ERROR") : msg);
        }
        case NODE_SET: {
            if (!n->as.assign.name)
                break;
            std::string val;
            if (n->as.assign.value)
                render_text_node(vars, n->as.assign.value,
                                 std::strlen(n->as.assign.value), val, true, 0);
            vars[n->as.assign.name] = val;
            break;
        }
        case NODE_CLEAR: {
            if (!n->as.assign.name)
                break;
            vars[n->as.assign.name] = std::string();
            break;
        }
        case NODE_XTEMPLATE: {
            if (!xctx || !xctx->resolve)
                throw RenderError("##XTEMPLATE render context is missing");
            if (xctx->depth >= xctx->max_depth)
                throw RenderError("##XTEMPLATE recursion limit reached");

            // Allow `$vars` / bare var names inside XTEMPLATE arguments.
            const std::string name = n->as.xt.name
                                         ? resolve_xt_arg(vars, n->as.xt.name)
                                         : std::string();
            const std::string xfile = n->as.xt.xfile
                                          ? resolve_xt_arg(vars, n->as.xt.xfile)
                                          : xctx->current_xfile;
            if (name.empty())
                throw RenderError("##XTEMPLATE: missing template name");

            // Apply CLI-style --var value overrides for the nested render.
            std::unordered_map<std::string, std::string> nested_vars = vars;
            for (size_t ai = 0; ai < n->as.xt.argc; ++ai) {
                const char *tok = n->as.xt.argv ? n->as.xt.argv[ai] : nullptr;
                if (!tok)
                    continue;
                std::string key(tok);
                if (key.size() >= 3 && key.compare(0, 2, "--") == 0) {
                    key = key.substr(2);
                    std::string val;
                    if (ai + 1 < n->as.xt.argc && n->as.xt.argv[ai + 1]) {
                        const std::string next(n->as.xt.argv[ai + 1]);
                        // Next token is another flag → treat as empty value
                        if (!(next.size() >= 2 &&
                              next.compare(0, 2, "--") == 0)) {
                            val = resolve_xt_arg(vars, n->as.xt.argv[ai + 1]);
                            ++ai;
                        }
                    }
                    nested_vars[key] = val;
                }
            }

            std::string sub_body;
            if (!xctx->resolve(name, xfile, sub_body))
                throw RenderError("##XTEMPLATE not found: " + name);

            std::string nested_err;
            std::string sub_out;
            XTemplateRenderContext subctx = *xctx;
            subctx.depth++;
            const unsigned char nested_indent = xctx->sub_indent;

            bool ok = render_xtemplate(sub_body, nested_vars, sub_out,
                                       render_empty_vals, xctx->diag,
                                       &nested_err, nested_indent, &subctx);
            if (!ok)
                throw RenderError(nested_err);
            result += sub_out;
            break;
        }
        case NODE_COND: {
            // First matching branch in IF / ELIF_* / ELSE chain
            for (const Node *branch = n; branch;
                 branch = branch->cond_chain_next_sibling) {
                if (!branch->as.cond.cond)
                    continue;
                if (cond_matches(branch->as.cond.cond, vars)) {
                    if (branch->first_child)
                        branch->first_child->render(vars, result,
                                                    render_empty_vals,
                                                    append_indent, xctx);
                    break;
                }
            }
            break;
        }
        }
    }
}

bool render_xtemplate(const std::string &body,
                      const std::unordered_map<std::string, std::string> &vars,
                      std::string &result, bool render_empty_vals,
                      ParseDiagnostics *diag, std::string *render_error,
                      unsigned char append_indent,
                      const XTemplateRenderContext *xctx) {
    result.clear();
    if (render_error)
        render_error->clear();
    Node *root = Node::parse(body, diag);
    if (!root)
        return true;
    try {
        // Mutable copy so ##SET / ##UNSET stay local to this render.
        std::unordered_map<std::string, std::string> mutable_vars = vars;
        root->render(mutable_vars, result, render_empty_vals, append_indent,
                     xctx);
        Node::destroy(root);
        return true;
    } catch (const RenderError &ex) {
        result.clear();
        if (render_error)
            *render_error = ex.what();
        Node::destroy(root);
        return false;
    }
}

bool is_xcheckbox(const std::string &var_type) {
    return var_type == "XCHECKBOX";
}

bool is_xvariant(const std::string &var_type) {
    return var_type.compare(0, 9, "XVARIANT:") == 0 || var_type == "XVARIANT";
}

std::vector<std::string> parse_xvariant_options(const std::string &var_type) {
    std::vector<std::string> options;
    if (var_type.compare(0, 9, "XVARIANT:") != 0)
        return options;
    std::string rest = var_type.substr(9);
    size_t start = 0;
    while (start <= rest.size()) {
        size_t dash = rest.find('-', start);
        if (dash == std::string::npos) {
            std::string opt = rest.substr(start);
            if (!opt.empty())
                options.push_back(opt);
            break;
        }
        std::string opt = rest.substr(start, dash - start);
        if (!opt.empty())
            options.push_back(opt);
        start = dash + 1;
    }
    return options;
}

std::string display_var_type(const std::string &var_type) {
    if (is_xvariant(var_type))
        return "XVARIANT";
    return var_type;
}
