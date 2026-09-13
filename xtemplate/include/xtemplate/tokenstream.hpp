#pragma once

#include "xtemplate/nodes.hpp"

#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>

struct TokenStream {
        const char *data;
        size_t size;
        size_t pos;
        ParseDiagnostics *diag = nullptr;

        TokenStream(const std::string &s, ParseDiagnostics *d = nullptr)
            : data(s.data()), size(s.size()), pos(0), diag(d) {}

        bool eof() const { return pos >= size; }

        // 1-based line of the current cursor (for soft error messages).
        size_t current_line() const {
            size_t line = 1;
            for (size_t i = 0; i < pos && i < size; ++i) {
                if (data[i] == '\n')
                    ++line;
            }
            return line;
        }

        // Record a soft error (or throw ParseError if no diagnostics sink).
        void report(std::string msg) {
            std::string full =
                "line " + std::to_string(current_line()) + ": " + msg;
            if (diag)
                diag->add(std::move(full));
            else
                throw ParseError(std::move(full));
        }

        char peek() const { return pos < size ? data[pos] : '\0'; }

        void advance() {
            if (pos < size)
                ++pos;
        }

        void skip_ws() {
            while (!eof() && std::isspace(static_cast<unsigned char>(peek())))
                advance();
        }

        // Spaces/tabs only — do not cross to the next line
        void skip_spaces_tabs() {
            while (!eof() && (peek() == ' ' || peek() == '\t'))
                advance();
        }

        bool at_line_end() const {
            return eof() || peek() == '\n' || peek() == '\r';
        }

        // Consume remainder of the current line (including trailing newline)
        void consume_line_end() {
            while (!eof() && peek() != '\n')
                advance();
            if (!eof())
                advance();
        }

        bool match_keyword_at(size_t at, const char *kw) const {
            size_t len = std::strlen(kw);
            if (at + len > size)
                return false;
            if (std::memcmp(data + at, kw, len) != 0)
                return false;
            char next = (at + len < size) ? data[at + len] : '\0';
            return !std::isalnum(static_cast<unsigned char>(next)) &&
                   next != '_';
        }

        bool match_keyword(const char *kw) const {
            return match_keyword_at(pos, kw);
        }

        void consume_keyword(const char *kw) { pos += std::strlen(kw); }

        bool peek_directive_start() const {
            size_t look = pos;
            while (look < size && (data[look] == ' ' || data[look] == '\t'))
                ++look;
            return look + 1 < size && data[look] == '#' && data[look + 1] == '#';
        }

        void consume_directive_start() {
            skip_spaces_tabs();
            pos += 2;
        }

        bool peek_directive_keyword(const char *kw) const {
            size_t look = pos;
            while (look < size && (data[look] == ' ' || data[look] == '\t'))
                ++look;
            if (look + 1 >= size || data[look] != '#' || data[look + 1] != '#')
                return false;
            return match_keyword_at(look + 2, kw);
        }

        void consume_directive_keyword(const char *kw) {
            consume_directive_start();
            consume_keyword(kw);
        }

        bool peek_is_end() const { return peek_directive_keyword("END"); }
        void consume_end() {
            consume_directive_keyword("END");
            consume_line_end();
        }

        bool peek_is_if_on() const { return peek_directive_keyword("IF_ON"); }
        void consume_if_on() { consume_directive_keyword("IF_ON"); }

        bool peek_is_if_off() const { return peek_directive_keyword("IF_OFF"); }
        void consume_if_off() { consume_directive_keyword("IF_OFF"); }

        bool peek_is_if_eq() const { return peek_directive_keyword("IF_EQ"); }
        void consume_if_eq() { consume_directive_keyword("IF_EQ"); }

        bool peek_is_if_neq() const { return peek_directive_keyword("IF_NEQ"); }
        void consume_if_neq() { consume_directive_keyword("IF_NEQ"); }
     
        bool peek_is_if_empty() const { return peek_directive_keyword("IF_EMPTY"); }
        void consume_if_empty() { consume_directive_keyword("IF_EMPTY"); }
   
        bool peek_is_if_nempty() const { return peek_directive_keyword("IF_NEMPTY"); }
        void consume_if_nempty() { consume_directive_keyword("IF_NEMPTY"); }

        bool peek_is_elif_on() const {
            return peek_directive_keyword("ELIF_ON");
        }
        void consume_elif_on() { consume_directive_keyword("ELIF_ON"); }

        bool peek_is_elif_off() const {
            return peek_directive_keyword("ELIF_OFF");
        }
        void consume_elif_off() { consume_directive_keyword("ELIF_OFF"); }

        bool peek_is_elif_eq() const {
            return peek_directive_keyword("ELIF_EQ");
        }
        void consume_elif_eq() { consume_directive_keyword("ELIF_EQ"); }

        bool peek_is_elif_neq() const {
            return peek_directive_keyword("ELIF_NEQ");
        }
        void consume_elif_neq() { consume_directive_keyword("ELIF_NEQ"); }
         
        bool peek_is_elif_empty() const { return peek_directive_keyword("ELIF_EMPTY"); }
        void consume_elif_empty() { consume_directive_keyword("ELIF_EMPTY"); }

        bool peek_is_elif_nempty() const { return peek_directive_keyword("ELIF_NEMPTY"); }
        void consume_elif_nempty() { consume_directive_keyword("ELIF_NEMPTY"); }

        bool peek_is_else() const { return peek_directive_keyword("ELSE"); }
        void consume_else() {
            consume_directive_keyword("ELSE");
            consume_line_end();
        }

        bool peek_is_error() const { return peek_directive_keyword("ERROR"); }
        void consume_error() { consume_directive_keyword("ERROR"); }

        bool peek_is_set() const { return peek_directive_keyword("SET"); }
        void consume_set() { consume_directive_keyword("SET"); }

        bool peek_is_clear() const { return peek_directive_keyword("CLEAR"); }
        void consume_clear() { consume_directive_keyword("CLEAR"); }

        // Rest of the current line (after optional spaces/tabs), then consume NL.
        std::string read_rest_of_line() {
            skip_spaces_tabs();
            size_t start = pos;
            while (!eof() && peek() != '\n' && peek() != '\r')
                advance();
            std::string out(data + start, pos - start);
            // trim trailing whitespace
            while (!out.empty() &&
                   std::isspace(static_cast<unsigned char>(out.back())))
                out.pop_back();
            consume_line_end();
            return out;
        }

        bool peek_is_elif() const {
            return peek_is_elif_on() || peek_is_elif_off() ||
                   peek_is_elif_eq() || peek_is_elif_neq() ||
                   peek_is_elif_empty() || peek_is_elif_nempty();
        }

        // End of an IF/ELIF/ELSE branch body (do not consume — caller decides)
        bool peek_is_branch_boundary() const {
            return peek_is_end() || peek_is_elif() || peek_is_else();
        }

        // Read an identifier: [A-Za-z_][A-Za-z0-9_]*
        std::string read_ident() {
            skip_ws();
            size_t start = pos;
            if (!eof() && (std::isalpha(static_cast<unsigned char>(peek())) ||
                           peek() == '_')) {
                advance();
                while (!eof() &&
                       (std::isalnum(static_cast<unsigned char>(peek())) ||
                        peek() == '_')) {
                    advance();
                }
            }
            return std::string(data + start, pos - start);
        }

        // Condition var name: optional leading '$' then identifier
        // (##IF_ON $use_stdout and ##IF_ON use_stdout are equivalent)
        std::string read_cond_var() {
            skip_spaces_tabs();
            if (peek() == '$')
                advance();
            // Do not use skip_ws here — must not jump to the next line
            size_t start = pos;
            if (!eof() && (std::isalpha(static_cast<unsigned char>(peek())) ||
                           peek() == '_')) {
                advance();
                while (!eof() &&
                       (std::isalnum(static_cast<unsigned char>(peek())) ||
                        peek() == '_')) {
                    advance();
                }
            }
            return std::string(data + start, pos - start);
        }

        // Value on the same line only. Returns false if missing (EOL / ##).
        // Quoted "" is a present empty value (returns true, out empty).
        bool read_value(std::string &out) {
            out.clear();
            skip_spaces_tabs();
            if (at_line_end() || peek_directive_start())
                return false;

            if (peek() == '"') {
                advance();
                size_t start = pos;
                while (!eof() && peek() != '"' && peek() != '\n' &&
                       peek() != '\r')
                    advance();
                out.assign(data + start, pos - start);
                if (!eof() && peek() == '"')
                    advance();
                return true;
            }

            size_t start = pos;
            while (!eof() &&
                   !std::isspace(static_cast<unsigned char>(peek()))) {
                if (pos + 1 < size && data[pos] == '#' && data[pos + 1] == '#')
                    break;
                advance();
            }
            out.assign(data + start, pos - start);
            return !out.empty();
        }

        // Plain text until next directive or EOF (preserves newlines).
        // Leading spaces/tabs immediately before a ## directive are not
        // included — they belong to the directive line (nesting indent).
        std::string read_until_next_directive() {
            size_t start = pos;
            while (!eof() && !peek_directive_start())
                advance();
            return std::string(data + start, pos - start);
        }
};
