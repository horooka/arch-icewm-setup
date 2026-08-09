#pragma once

#include <stdexcept>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

// Soft parse issues collected during body/VARS parsing. Prefer attaching a
// sink to TokenStream over threading error strings through every call.
struct ParseDiagnostics {
        std::vector<std::string> messages;

        bool empty() const { return messages.empty(); }

        void add(std::string msg) {
            if (!msg.empty())
                messages.push_back(std::move(msg));
        }

        std::string join(const char *bullet = "- ") const {
            std::string out;
            for (const auto &m : messages) {
                out += bullet;
                out += m;
                out += '\n';
            }
            return out;
        }
};

struct ParseError : std::runtime_error {
        using std::runtime_error::runtime_error;
};

// Thrown by ##ERROR during render to abort the template.
struct RenderError : std::runtime_error {
        using std::runtime_error::runtime_error;
};

typedef enum {
    NODE_TEXT,
    NODE_COND,
    NODE_ERROR,
    NODE_XTEMPLATE,
    NODE_SET,
    NODE_CLEAR,
} NodeType;

typedef enum {
    ON,
    OFF,
    EQ,
    NEQ,
    ELIF_ON,
    ELIF_OFF,
    ELIF_EQ,
    ELIF_NEQ,
    ELSE,
    EMPTY,
    ELIF_EMPTY,
    NEMPTY,
    ELIF_NEMPTY,
} CondType;

typedef struct {
        CondType type;
        const char *arg1;
        const char *arg2; // null for ON/OFF
} Cond;

struct Node {
        NodeType type;
        Node *next_sibling;
        Node *cond_chain_next_sibling;
        Node *first_child;
        union {
                struct {
                        const char *content;
                        size_t len;
                } text; // NODE_TEXT and NODE_ERROR message
                struct {
                        Cond *cond;
                } cond;
                struct {
                        const char *name;
                        const char *xfile; // nullable => use resolver current_xfile
                        // Remaining CLI-style tokens after name / -xfile
                        // (e.g. --level $level --title $title).
                        const char **argv;
                        size_t argc;
                } xt;
                struct {
                        const char *name;
                        const char *value; // NEW, SET: rest of line (may contain $vars); CLEAR: null
                } assign;
        } as;

        // Soft-fails into diag (if set); otherwise throws ParseError.
        static Node *parse(const std::string &body,
                           ParseDiagnostics *diag = nullptr);
        static void destroy(Node *n);

        // May throw RenderError when a ##ERROR node is reached.
        // `vars` is mutable so ##NEW / ##SET / ##UNSET can update it mid-render.
        void render(std::unordered_map<std::string, std::string> &vars,
                    std::string &result, bool render_empty_vals,
                    unsigned char append_indent,
                    const struct XTemplateRenderContext *xctx) const;
};

// Render-time lookup for ##XTEMPLATE nodes.
// If an XTEMPLATE node does not specify -xfile, `current_xfile` is used.
struct XTemplateRenderContext {
        std::string current_xfile;
        unsigned char sub_indent = 0;
        int depth = 0;
        int max_depth = 12;

        ParseDiagnostics *diag = nullptr; // share diag for nested parsing
        std::function<bool(const std::string &name, const std::string &xfile, std::string &out_body)> resolve;
};

// Parse body into an AST and render with named $var substitutions.
// Soft parse issues go to diag. Returns false if ##ERROR aborted render;
// then *render_error (if non-null) holds the message (rest of ##ERROR line,
// with $vars substituted).
bool render_xtemplate(const std::string &body,
                      const std::unordered_map<std::string, std::string> &vars,
                      std::string &result, bool render_empty_vals,
                      ParseDiagnostics *diag = nullptr,
                      std::string *render_error = nullptr,
                      unsigned char append_indent = 0,
                      const XTemplateRenderContext *xctx = nullptr);

// Split "XVARIANT:a-b-c" into {"a","b","c"}; empty if not an XVARIANT type.
std::vector<std::string> parse_xvariant_options(const std::string &var_type);
bool is_xcheckbox(const std::string &var_type);
bool is_xvariant(const std::string &var_type);
std::string display_var_type(const std::string &var_type);
