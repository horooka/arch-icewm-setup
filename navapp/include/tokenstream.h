#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

enum { TS_LOOKAHEAD = 2 };
enum { CINT_IDENT_MAX = 16 };
enum { CINT_LEXEME_MAX = 16 };

static const char *const keywords[] = {"SELECT", "FROM", "WHERE", "LIMIT"};

enum { KEYWORDS_COUNT = sizeof(keywords) / sizeof(keywords[0]) };

typedef enum TokenKind {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_INT,
    TOK_FLOAT,
    TOK_STR,
    TOK_BINOP,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_SEMI,
    TOK_COMMA,
    TOK_ERROR,
} TokenKind;

typedef struct Token {
        TokenKind kind;
        char text[CINT_LEXEME_MAX]; /* lexeme; empty for punct / binop / EOF */
        char var_name[CINT_IDENT_MAX];
        char op[3]; /* TOK_BINOP only (NUL-terminated) */
        unsigned short symbol;
} Token;

typedef struct TokenStream {
        const char *data;
        unsigned short size;
        unsigned short pos; /* char cursor while lexing */
        Token la[TS_LOOKAHEAD];
        unsigned char n; /* how many lookahead slots filled */
        bool oom;
} TokenStream;

/* ---------- char stream ---------- */

static inline void init_tokenstream(TokenStream *ts, const char *data,
                                    unsigned short size) {
    memset(ts, 0, sizeof(*ts));
    ts->data = data;
    ts->size = size;
}

static inline char ts_peek(const TokenStream *ts) {
    return ts->pos < ts->size ? ts->data[ts->pos] : '\0';
}

static inline char ts_peek_at(const TokenStream *ts, unsigned short off) {
    unsigned short i = (unsigned short)(ts->pos + off);
    return i < ts->size ? ts->data[i] : '\0';
}

static inline bool ts_eof(const TokenStream *ts) { return ts->pos >= ts->size; }

static inline bool ts_advance(TokenStream *ts) {
    if (ts->pos < ts->size) {
        ++ts->pos;
        return true;
    }
    return false;
}

static inline void ts_skip_ws(TokenStream *ts) {
    while (!ts_eof(ts) && isspace((unsigned char)ts_peek(ts)))
        (void)ts_advance(ts);
}

static inline unsigned short ts_get_line(const TokenStream *ts) {
    unsigned short line = 1;
    for (unsigned short i = 0; i < ts->pos; ++i) {
        if (ts->data[i] == '\n')
            ++line;
    }
    return line;
}

/* ---------- classifiers ---------- */

static inline bool is_ident_start(char c) { return isalpha(c); }

static inline bool is_ident_char(char c) { return isalnum(c) || c == '_'; }

static inline bool is_binop_start(char c) {
    return strchr("=+-*/%&^|<>!", c) != NULL;
}

static inline bool str_in_list(const char *s, const char *const *list,
                               size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(s, list[i]) == 0)
            return true;
    }
    return false;
}

static inline bool is_keyword_name(const char *s) {
    return str_in_list(s, keywords, KEYWORDS_COUNT);
}

/* Longest-match operators for Pratt parsing. */
static inline const char *match_binop_at(const TokenStream *ts) {
    char a = ts_peek(ts);
    char b = ts_peek_at(ts, 1);
    if (a == '!' && b == '=')
        return "!=";
    if (a == '<' && b == '=')
        return "<=";
    if (a == '>' && b == '=')
        return ">=";
    if (a == '&' && b == '&')
        return "&&";
    if (a == '|' && b == '|')
        return "||";
    if (a == '<' && b == '<')
        return "<<";
    if (a == '>' && b == '>')
        return ">>";
    switch (a) {
    case '=':
        return "=";
    case '+':
        return "+";
    case '-':
        return "-";
    case '*':
        return "*";
    case '/':
        return "/";
    case '%':
        return "%";
    case '&':
        return "&";
    case '|':
        return "|";
    case '^':
        return "^";
    case '<':
        return "<";
    case '>':
        return ">";
    case '!':
        return "!";
    default:
        return NULL;
    }
}

/* ---------- lexeme readers ---------- */

static inline int copy_slice(const TokenStream *ts, unsigned short start,
                             unsigned short end, char *dst, size_t dst_sz) {
    size_t n = (size_t)(end - start);
    if (n + 1 > dst_sz)
        return 0;
    memcpy(dst, ts->data + start, n);
    dst[n] = '\0';
    return 1;
}

static inline int read_ident(TokenStream *ts, char *dst, size_t dst_sz) {
    unsigned short start = ts->pos;
    if (!ts_eof(ts) && is_ident_char(ts_peek(ts))) {
        (void)ts_advance(ts);
        while (!ts_eof(ts) && is_ident_char(ts_peek(ts)))
            (void)ts_advance(ts);
    }
    return copy_slice(ts, start, ts->pos, dst, dst_sz);
}

static inline int read_number(TokenStream *ts, TokenKind *out_kind, char *dst,
                              size_t dst_sz) {
    unsigned short start = ts->pos;
    *out_kind = TOK_INT;

    while (!ts_eof(ts) && isdigit((unsigned char)ts_peek(ts)))
        (void)ts_advance(ts);

    /* Floats disabled to keep AVR firmware free of soft-float. */
    return copy_slice(ts, start, ts->pos, dst, dst_sz);
}

static inline int read_cstr(TokenStream *ts, char *dst, size_t dst_sz) {
    unsigned short start = ts->pos;
    if (ts_peek(ts) != '\'')
        return copy_slice(ts, start, ts->pos, dst, dst_sz);

    (void)ts_advance(ts); /* opening " */
    while (!ts_eof(ts) && ts_peek(ts) != '\'') {
        if (ts_peek(ts) == '\\' && !ts_eof(ts))
            (void)ts_advance(ts); /* skip escape target */
        (void)ts_advance(ts);
    }
    if (ts_peek(ts) == '\'')
        (void)ts_advance(ts); /* closing " */
    return copy_slice(ts, start, ts->pos, dst, dst_sz);
}

/* ---------- single-token lex ---------- */

static inline void token_init(Token *t) {
    t->kind = TOK_EOF;
    t->text[0] = '\0';
    t->op[0] = '\0';
    t->symbol = 0;
}

static inline bool lex_token(TokenStream *ts, Token *out) {
    token_init(out);
    ts_skip_ws(ts);
    out->symbol = ts->pos;

    if (ts_eof(ts)) {
        out->kind = TOK_EOF;
        return true;
    }

    char c = ts_peek(ts);

    if (is_ident_start(c)) {
        if (!read_ident(ts, out->text, sizeof(out->text)))
            return false;
        if (is_keyword_name(out->text))
            out->kind = TOK_KEYWORD;
        else
            out->kind = TOK_IDENT;
        return true;
    }

    if (isdigit(c)) {
        TokenKind kind;
        if (!read_number(ts, &kind, out->text, sizeof(out->text)))
            return false;
        out->kind = kind;
        return true;
    }

    if (c == '\'') {
        if (!read_cstr(ts, out->text, sizeof(out->text)))
            return false;
        out->kind = TOK_STR;
        return true;
    }

    if (c == '(') {
        (void)ts_advance(ts);
        out->kind = TOK_LPAREN;
        return true;
    }
    if (c == ')') {
        (void)ts_advance(ts);
        out->kind = TOK_RPAREN;
        return true;
    }
    if (c == ';') {
        (void)ts_advance(ts);
        out->kind = TOK_SEMI;
        return true;
    }
    if (c == ',') {
        (void)ts_advance(ts);
        out->kind = TOK_COMMA;
        return true;
    }

    if (is_binop_start(c)) {
        const char *op = match_binop_at(ts);
        if (!op)
            return false;
        size_t len = strlen(op);
        memcpy(out->op, op, len + 1);
        ts->pos = (unsigned short)(ts->pos + len);
        out->kind = TOK_BINOP;
        return true;
    }

    /* Unknown character: emit error lexeme of length 1 and continue. */
    {
        if (!copy_slice(ts, ts->pos, (unsigned short)(ts->pos + 1), out->text,
                        sizeof(out->text)))
            return false;
        (void)ts_advance(ts);
        out->kind = TOK_ERROR;
        return true;
    }
}

/* ---------- streaming lookahead ---------- */

static inline bool ts_ensure(TokenStream *ts, unsigned char need) {
    if (ts->oom)
        return false;
    while (ts->n <= need) {
        if (ts->n > 0 && ts->la[ts->n - 1].kind == TOK_EOF)
            return true;
        if (ts->n >= TS_LOOKAHEAD)
            return true;
        if (!lex_token(ts, &ts->la[ts->n])) {
            ts->oom = true;
            return false;
        }
        ++ts->n;
    }
    return true;
}

static inline void free_tokenstream(TokenStream *ts) { (void)ts; }

/* Open a streaming lexer over data[0..size). Lexes on demand. ts is
 * caller-owned. */
static inline int tokenize_into(TokenStream *ts, const char *data,
                                size_t size) {
    if (!ts)
        return 0;
    if (size > 65535)
        size = 65535;
    init_tokenstream(ts, data, (unsigned short)size);
    return ts_ensure(ts, 0) ? 1 : 0;
}

static inline char *ts_tok_steal(Token *t) {
    char *s;
    if (!t || !t->text[0])
        return NULL;
    s = strdup(t->text);
    t->text[0] = '\0';
    return s;
}

static inline void ts_tok_copy_text(char *dst, size_t dst_sz, const Token *t) {
    if (!dst || dst_sz == 0)
        return;
    const char *src = "";
    if (t) {
        if (t->kind == TOK_BINOP)
            src = t->op;
        else if (t->text[0])
            src = t->text;
    }
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
}

/* ---------- parser-facing token cursor ---------- */

static inline Token *ts_tok_peek_n(TokenStream *ts, unsigned short n) {
    static Token err_tok;
    if (n >= TS_LOOKAHEAD)
        n = TS_LOOKAHEAD - 1;
    if (!ts_ensure(ts, (unsigned char)n)) {
        token_init(&err_tok);
        err_tok.kind = TOK_ERROR;
        return &err_tok;
    }
    if (n >= ts->n)
        return &ts->la[ts->n - 1];
    return &ts->la[n];
}

static inline const char *token_kind_name(TokenKind kind) {
    switch (kind) {
    case TOK_EOF:
        return "EOF";
    case TOK_IDENT:
        return "IDENT";
    case TOK_KEYWORD:
        return "KEYWORD";
    case TOK_INT:
        return "INT";
    case TOK_FLOAT:
        return "FLOAT";
    case TOK_STR:
        return "STR";
    case TOK_BINOP:
        return "BINOP";
    case TOK_LPAREN:
        return "LPAREN";
    case TOK_RPAREN:
        return "RPAREN";
    case TOK_SEMI:
        return "SEMI";
    case TOK_COMMA:
        return "COMMA";
    case TOK_ERROR:
        return "ERROR";
    default:
        return "?";
    }
}

static inline Token *ts_tok_peek(TokenStream *ts) {
    return ts_tok_peek_n(ts, 0);
}

/* Drop the current token. Invalidates prior peek pointers. */
static inline void ts_tok_consume(TokenStream *ts) {
    if (!ts_ensure(ts, 0) || ts->n == 0)
        return;
    for (unsigned char i = 1; i < ts->n; ++i)
        ts->la[i - 1] = ts->la[i];
    --ts->n;
    token_init(&ts->la[ts->n]);
}

static inline bool ts_tok_is(const Token *t, TokenKind kind) {
    return t && t->kind == kind;
}

static inline bool ts_tok_is_binop(const Token *t, const char *op) {
    return t && t->kind == TOK_BINOP && strcmp(t->op, op) == 0;
}

static inline bool ts_tok_check(TokenStream *ts, TokenKind kind) {
    return ts_tok_is(ts_tok_peek(ts), kind);
}

static inline bool ts_tok_match(TokenStream *ts, TokenKind kind) {
    if (!ts_tok_check(ts, kind))
        return false;
    ts_tok_consume(ts);
    return true;
}

static inline const char *tok_lexeme(const Token *t) {
    if (!t)
        return "";
    if (t->kind == TOK_BINOP)
        return t->op;
    return t->text;
}
