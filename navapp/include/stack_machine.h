#pragma once

#include "nav.h"
#include "tokenstream.h"
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOAD_VAR_I32 0x00
#define LOAD_VAR_STR 0x01
#define LOAD_VAR_YES 0x02
#define PUSH_CONST_I32 0x10
#define PUSH_CONST_STR 0x11
#define PUSH_CONST_YES 0x12
#define POP 0x20
#define EQ_I32 0x30
#define EQ_STR 0x31
#define NEQ_I32 0x32
#define NEQ_STR 0x33
#define GE_I32 0x34
#define LE_I32 0x35
#define G_I32 0x36
#define L_I32 0x37
#define AND 0x38
#define OR 0x39
#define ADD_I32 0x40
#define ADD_STR 0x41
#define SUB_I32 0x42
#define MUL_I32 0x43
#define MUL_STR 0x44
#define DIV_I32 0x45
#define MOD_I32 0x46
#define SHOW_I32 0x50
#define SHOW_STR 0x51
#define SHOW_YES 0x52
#define RETURN 0xF0

typedef unsigned char Type;

#define NUL 0
#define YES 1
#define I32 2
#define STR 4
#define F32 3
#define CHR 6
#define V_YES 129
#define V_I32 130
#define V_STR 132
#define V_F32 131

#define IS_VAR(t) (((t) & 0x80) != 0)
#define MK_TYPE(t, v) ((t) | v << 7)
#define BASE_TYPE(t) ((t) & 0x7F)

typedef struct {
        const char *name;
        Type type;
        unsigned char idx;
} SchemaCol;

static const SchemaCol schema_cols[] = {
    {"dest", STR, 0},  {"path", STR, 1},    {"brief", STR, 2},
    {"note", STR, 3},  {"command", STR, 4}, {"group", STR, 5},
    {"level", I32, 6}, {"format", I32, 7},  {"status", STR, 8}};

static char *alloc_format(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (size < 0) {
        va_end(copy);
        return NULL;
    }

    char *result = (char *)malloc((size_t)size + 1);
    if (result != NULL)
        vsnprintf(result, (size_t)size + 1, fmt, copy);
    va_end(copy);
    return result;
}

bool col_in_schema(char *col, SchemaCol *col_out) {
    for (unsigned int i = 0; i < sizeof(schema_cols) / sizeof(SchemaCol); i++) {
        if (strcmp(col, schema_cols[i].name) == 0) {
            if (col_out)
                *col_out = schema_cols[i];
            return true;
        }
    }
    return false;
}

enum { MAX_SELECT_COLS = sizeof(schema_cols) / sizeof(schema_cols[0]) };

/*
 * Fills `columns` with the schema indices of the selected columns and sets
 * *column_count. The array is caller-owned and must hold MAX_SELECT_COLS
 * entries. No heap strings are produced (the token lexemes are not borrowed),
 * so there is nothing per-column to free.
 */
int parse_cols(TokenStream *ts, unsigned char *columns, char **err_out,
               int *column_count) {
    Token *t = ts_tok_peek(ts);
    while (t->kind != TOK_KEYWORD && t->kind != TOK_SEMI &&
           t->kind != TOK_EOF) {
        if (t->kind == TOK_IDENT) {
            SchemaCol col;
            if (!col_in_schema(t->text, &col)) {
                *err_out = alloc_format("Unknown column %s", t->text);
                return t->symbol;
            }
            if (*column_count >= MAX_SELECT_COLS) {
                *err_out = (char *)"Too many columns in SELECT";
                return t->symbol;
            }
            columns[*column_count] = col.idx;
            ts_tok_consume(ts);
            *column_count += 1;
        } else if (t->kind != TOK_COMMA) {
            *err_out = alloc_format("Expected comma or column, got %s",
                                    token_kind_name(t->kind));
            return t->symbol;
        } else {
            ts_tok_consume(ts);
        }
        t = ts_tok_peek(ts);
    }
    return -1;
}

// ============================================================================
//                                   VALUES
// ============================================================================

typedef union ValUnion {
        bool yes;
        int32_t i32;
        float f32;
        char *str;
} ValUnion;

bool get_col_ptr(Dest *dest, unsigned char idx, ValUnion *val_out) {
    switch (idx) {
    case 0:
        val_out->str = dest->dest_name.data();
        return true;
    case 1:
        val_out->str = dest->path.data();
        return true;
    case 2:
        val_out->str = dest->brief.data();
        return true;
    case 3:
        val_out->str = dest->note_path.data();
        return true;
    case 4:
        val_out->str = dest->command.data();
        return true;
    case 5:
        val_out->str = dest->group_name.data();
        return true;
    case 6:
        val_out->i32 = dest->level;
        return true;
    case 7:
        val_out->i32 = dest->formatting;
        return true;
    case 8:
        val_out->str = dest->dest_kind.data();
        return true;
    default:
#ifdef DEBUG
        printf("  get_col_ptr: invalid idx %u\n", idx);
#endif
        exit(1);
        return false;
    }
}

typedef struct Val {
        Type type;
        ValUnion as;
} Val;

char *type_to_str(Type type) {
    switch (type) {
    case YES:
        return (char *)"yes";
    case V_YES:
        return (char *)"v_yes";
    case I32:
        return (char *)"i32";
    case V_I32:
        return (char *)"v_i32";
    case F32:
        return (char *)"f32";
    case V_F32:
        return (char *)"v_f32";
    case STR:
        return (char *)"str";
    case V_STR:
        return (char *)"v_str";
    case NUL:
        return (char *)"nan";
    default:
        return (char *)"Unknown type";
    }
}

Val make_yes(int8_t yes) {
    Val v = {YES, {0}};
    memset(&v.as, 0, sizeof(v.as));
    v.as.yes = yes;
    return v;
}
Val make_i32(int32_t i32) {
    Val v = {I32, {.i32 = i32}};
    memset(&v.as, 0, sizeof(v.as));
    v.as.i32 = i32;
    return v;
}
Val make_f32(float f32) {
    Val v = {F32, {.f32 = f32}};
    return v;
}
Val make_str(char *str) {
    Val v = {STR, {.str = str}};
    memset(&v.as, 0, sizeof(v.as));
    v.as.str = str;
    return v;
}
Val make_nan() {
    Val v = {NUL, {.i32 = -1}};
    return v;
}
Val make_col_with_type(char *str, Type val_type) {
    Val v = {val_type, {.str = str}};
    return v;
}
Val make_yes_with_type(bool yes, Type val_type) {
    Val v = {val_type, {yes}};
    return v;
}
Val make_i32_with_type(int32_t i32, Type val_type) {
    Val v = {val_type, {.i32 = i32}};
    return v;
}
Val make_f32_with_type(float f32, Type val_type) {
    Val v = {val_type, {.f32 = f32}};
    return v;
}
Val make_str_with_type(char *str, Type val_type) {
    Val v = {val_type, {.str = str}};
    return v;
}

float get_f32(Val val) {
    if (val.type == F32)
        return (float)(val.as.f32);
    else
        return 0;
}

bool cmp(Val *val1, Val *val2) {
    switch (val1->type) {
    case YES:
        return val1->as.yes == val2->as.yes;
    case I32:
        return val1->as.i32 == val2->as.i32;
    case F32:
        return val1->as.f32 == val2->as.f32;
    case STR: {
        switch (val2->type) {
        case STR:
            return strcmp(val1->as.str, val2->as.str) == 0;
            break;
        case V_STR:
            return false;
            break;
        }
        return false;
    }
    default: {
        if (IS_VAR(val1->type) || IS_VAR(val2->type))
            return false;
        assert(false);
    }
    }
}
char *int_to_str(int val) {
    size_t size = snprintf(NULL, 0, "%d", val) + 1;
    char *str = (char *)malloc(size);
    if (str == NULL)
        return NULL;
    snprintf(str, size, "%d", val);
    return str;
}
char *float_to_str(float val) {
    size_t size = snprintf(NULL, 0, "%f", val) + 1;
    char *str = (char *)malloc(size);
    if (str == NULL)
        return NULL;
    snprintf(str, size, "%f", val);
    return str;
}

Type resulting_type(Type type1, Type type2) {
    if (IS_VAR(type1) || IS_VAR(type2))
        return BASE_TYPE(type1) | 0x80;
    return type1;
}

bool looks_like_int(const char *s) {
    if (s[0] == '\0')
        return false;
    if (*s == '+' || *s == '-')
        ++s;
    while (*s) {
        if (!isdigit(*s))
            return false;
        ++s;
    }
    return true;
}
bool looks_like_float(const char *s) {
    if (s[0] == '\0')
        return false;
    if (*s == '+' || *s == '-')
        ++s;
    bool seen_dot = false;
    while (*s) {
        if (!isdigit(*s))
            return false;
        if (*s == '.') {
            if (seen_dot)
                return false;
            seen_dot = true;
        }
        ++s;
    }
    return true;
}

int32_t val_as_i32(Val val) {
    switch (val.type) {
    case YES:
        return val.as.yes;
    case I32:
        return val.as.i32;
    case F32:
        return (int32_t)val.as.f32;
    default:
        return 0;
    }
}

Type type_from_str(const char *type) {
    if (strcmp(type, "nan") == 0)
        return NUL;
    if (strcmp(type, "yes") == 0)
        return YES;
    if (strcmp(type, "i32") == 0)
        return I32;
    if (strcmp(type, "f32") == 0)
        return F32;
    if (strcmp(type, "str") == 0)
        return STR;
    return NUL;
}

bool val_is_number(Val v) {
    switch (v.type) {
    case I32:
    case V_I32:
    case F32:
        return true;
    default:
        return false;
    }
}

bool val_is_str(Val v) { return v.type == STR; }

bool val_is_int(Val v) { return v.type == I32; }

Val make_nan_str() {
    char *str = (char *)malloc(4);
    strcpy(str, "NaN");
    str[3] = '\0';
    return (Val){STR, {.str = str}};
}

Val cast_val(Type dest_t, Val v) {
#ifdef DEBUG
    printf("Casting %s from %s\n", type_to_str(dest_t), type_to_str(v.type));
#endif
    if (IS_VAR(v.type)) {
        if (dest_t == STR) {
            Val out = {STR, {.str = NULL}};
            out.as.str = alloc_format("%s", type_to_str(v.type));
            return out;
        }
    }
    if (v.type == dest_t) {
        Val out = {dest_t, v.as};
        return out;
    }
    if (v.type == YES)
        return make_str((char *)(v.as.yes ? "YES" : "NOT"));
    if (!val_is_int(v)) {
#ifdef DEBUG
        printf("cast_val: not int\n");
#endif
        return make_nan();
    }
    int32_t i = v.as.i32;
    if (dest_t == STR) {
        int n = snprintf(NULL, 0, "%d", i);
        char *str;
        if (n < 0)
            return make_nan_str();
        str = (char *)malloc((size_t)n + 1);
        if (str == NULL)
            return make_nan_str();
        sprintf(str, "%d", i);
        return make_str(str);
    }
    return make_i32(i);
}

Val apply_unop(const char *op, Val x) {
    if (!val_is_number(x))
        return make_nan();
    if (strcmp(op, "-") == 0)
        return make_i32(-val_as_i32(x));
    if (strcmp(op, "+") == 0)
        return cast_val(x.type, x);
    if (strcmp(op, "!") == 0)
        return make_yes(val_as_i32(x) == 0 ? 1 : 0);
    return make_nan();
}

Val make_val_with_type(Type type, char *val) {
    switch (BASE_TYPE(type)) {
    case YES:
        return make_yes_with_type(atoi(val), type);
    case I32:
        return make_i32_with_type(atoi(val), type);
    case F32:
        return make_f32_with_type(atof(val), type);
    case STR:
        return make_str_with_type(val, type);
    case NUL:
        return make_nan();
    default:
        assert(0);
    }
}

// ============================================================================
//                                  COMPILATION
// ============================================================================

typedef struct CompileCtx {
        TokenStream *ts;
        uint8_t *bytecode;
        int bytecode_size;
        int bytecode_cap;
        const char **const_strs;
        int const_strs_size;
        int const_strs_cap;
        uint8_t *const_pool;
        char *err;
        int symbol;
} CompileCtx;

CompileCtx *compile_ctx_new(TokenStream *ts) {
    CompileCtx *ctx = (CompileCtx *)malloc(sizeof(CompileCtx));
    ctx->ts = ts;
    ctx->bytecode_size = 0;
    ctx->bytecode = (uint8_t *)malloc(10);
    ctx->bytecode_cap = 10;
    ctx->const_strs_size = 0;
    ctx->const_strs_cap = 10;
    ctx->const_strs = (const char **)malloc((size_t)ctx->const_strs_cap *
                                            sizeof(const char *));
    ctx->err = NULL;
    ctx->symbol = 0;
    return ctx;
}

int get_str_in_list(const char *s, const char **const_strs, int n) {
    for (int i = 0; i < n; i++) {
        if (strcmp(s, const_strs[i]) == 0)
            return i;
    }
    return -1;
}

void write_byte_to_bytecode(CompileCtx *ctx, uint8_t op) {
    if (ctx->bytecode_size == ctx->bytecode_cap) {
        ctx->bytecode_cap *= 2;
        ctx->bytecode = (uint8_t *)realloc(ctx->bytecode, ctx->bytecode_cap);
    }
    uint8_t *bytecode = ctx->bytecode;
    bytecode[ctx->bytecode_size++] = op;
#ifdef DEBUG
    printf("  opcode write: (%02x)\n", op);
#endif
}

void write_le_val_to_bytecode(CompileCtx *ctx, Val v) {
    if (ctx->bytecode_size + sizeof(v.as) >= (unsigned long)ctx->bytecode_cap) {
        ctx->bytecode_cap *= 2;
        ctx->bytecode = (uint8_t *)realloc(ctx->bytecode, ctx->bytecode_cap);
    }
    uint8_t *bytecode = ctx->bytecode + ctx->bytecode_size;
    memcpy(bytecode, &v.as, sizeof(v.as));
#ifdef DEBUG
    for (unsigned int i = 0; i < sizeof(v.as); ++i)
        printf("%02x", bytecode[i]);
    printf("\n");
#endif
    ctx->bytecode_size += sizeof(v.as);
}

void write_le_uint32_to_bytecode(CompileCtx *ctx, uint32_t v) {
    if (ctx->bytecode_size + sizeof(v) >= (unsigned long)ctx->bytecode_cap) {
        ctx->bytecode_cap *= 2;
        ctx->bytecode = (uint8_t *)realloc(ctx->bytecode, ctx->bytecode_cap);
    }
    uint8_t *bytecode = ctx->bytecode + ctx->bytecode_size;
    memcpy(bytecode, &v, sizeof(v));
#ifdef DEBUG
    for (unsigned int i = 0; i < sizeof(v); ++i)
        printf("%02x", bytecode[i]);
    printf("\n");
#endif
    ctx->bytecode_size += sizeof(v);
}

int add_str_to_stack(CompileCtx *ctx, char *str) {
    int idx = get_str_in_list(str, ctx->const_strs, ctx->const_strs_size);
    if (idx < 0) {
        idx = ctx->const_strs_size++;
        if (idx >= ctx->const_strs_cap) {
            ctx->const_strs_cap += 10;
            ctx->const_strs = (const char **)realloc(
                ctx->const_strs,
                (size_t)ctx->const_strs_cap * sizeof(const char *));
        }
    }
    ctx->const_strs[idx] = str;
#ifdef DEBUG
    printf("PUSH_CONST_STR %d (%s)\n  ", idx, str);
#endif
    write_byte_to_bytecode(ctx, idx);
    return idx;
}

void add_i32_to_stack(CompileCtx *ctx, int i32) {
#ifdef DEBUG
    printf("PUSH_CONST_I32: %d\n  ", i32);
#endif
    write_le_uint32_to_bytecode(ctx, i32);
}

void add_yes_to_stack(CompileCtx *ctx, bool yes) {
#ifdef DEBUG
    printf("PUSH_CONST_YES: %d\n  ", yes);
#endif
    write_byte_to_bytecode(ctx, yes);
}

Val compile_unop(const char *op, Val x) {
    if (!val_is_number(x))
        return make_nan();
    if (strcmp(op, "-") == 0)
        return make_i32(-val_as_i32(x));
    if (strcmp(op, "+") == 0)
        return cast_val(x.type, x);
    if (strcmp(op, "!") == 0)
        return make_yes(val_as_i32(x) == 0 ? 1 : 0);
    return make_nan();
}

void compile_operands(CompileCtx *ctx, Val *left, Val *right,
                      Type result_type) {
    bool push_consts = IS_VAR(result_type);
    Type left_type = left->type;
    if (!IS_VAR(left_type) && push_consts) {
        switch (left_type) {
        case STR:
            write_byte_to_bytecode(ctx, PUSH_CONST_STR);
            add_str_to_stack(ctx, left->as.str);
            break;
        case I32:
            write_byte_to_bytecode(ctx, PUSH_CONST_I32);
            add_i32_to_stack(ctx, left->as.i32);
            break;
        case YES:
            write_byte_to_bytecode(ctx, PUSH_CONST_YES);
            add_yes_to_stack(ctx, left->as.yes);
            break;
        }
    }
    Type right_type = right->type;
    if (!IS_VAR(right_type) && push_consts) {
        switch (right_type) {
        case STR:
            write_byte_to_bytecode(ctx, PUSH_CONST_STR);
            add_str_to_stack(ctx, right->as.str);
            break;
        case I32:
            write_byte_to_bytecode(ctx, PUSH_CONST_I32);
            add_i32_to_stack(ctx, right->as.i32);
            break;
        case YES:
            write_byte_to_bytecode(ctx, PUSH_CONST_YES);
            add_yes_to_stack(ctx, right->as.yes);
            break;
        }
    }
}

Val compile_binop(struct CompileCtx *ctx, const char *op, Val left, Val right,
                  char **err_out) {
    Type res_type = resulting_type(left.type, right.type);
    bool res_is_var = IS_VAR(res_type);
#ifdef DEBUG
    printf("OP: %s from %s, %s\n", op, type_to_str(left.type),
           type_to_str(right.type));
#endif
    Type res_base_type = BASE_TYPE(res_type);
    compile_operands(ctx, &left, &right, res_type);
    /* Comparisons / logic → YES */
    if (strcmp(op, "=") == 0 || strcmp(op, "!=") == 0 || strcmp(op, "<") == 0 ||
        strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">=") == 0 || strcmp(op, "&&") == 0 ||
        strcmp(op, "||") == 0) {
        if (strcmp(op, "&&") == 0) {
            if (res_is_var)
                write_byte_to_bytecode(ctx, AND);
            return make_yes_with_type(val_as_i32(left) && val_as_i32(right) ? 1
                                                                            : 0,
                                      MK_TYPE(YES, res_is_var));
        } else if (strcmp(op, "||") == 0) {
            if (res_is_var)
                write_byte_to_bytecode(ctx, OR);
            return make_yes_with_type(val_as_i32(left) || val_as_i32(right) ? 1
                                                                            : 0,
                                      MK_TYPE(YES, res_is_var));
        } else if (strcmp(op, "=") == 0) {
            if (res_is_var)
                write_byte_to_bytecode(ctx, res_base_type == I32   ? EQ_I32
                                            : res_base_type == STR ? EQ_STR
                                                                   : EQ_STR);
            return make_yes_with_type(cmp(&left, &right),
                                      MK_TYPE(YES, res_is_var));
        } else if (strcmp(op, "!=") == 0) {
            if (res_is_var)
                write_byte_to_bytecode(ctx, res_base_type == I32   ? NEQ_I32
                                            : res_base_type == STR ? NEQ_STR
                                                                   : NEQ_STR);
            return make_yes_with_type(!cmp(&left, &right),
                                      MK_TYPE(YES, res_is_var));
        } else {
            int32_t a = val_as_i32(left), b = val_as_i32(right);
            int rel = (a < b) ? -1 : (a > b) ? 1 : 0;
            if (strcmp(op, "<") == 0) {
                if (res_is_var)
                    write_byte_to_bytecode(ctx, L_I32);
                return make_yes_with_type(rel < 0, MK_TYPE(YES, res_is_var));
            } else if (strcmp(op, ">") == 0) {
                if (res_is_var)
                    write_byte_to_bytecode(ctx, G_I32);
                return make_yes_with_type(rel > 0, MK_TYPE(YES, res_is_var));
            } else if (strcmp(op, "<=") == 0) {
                if (res_is_var)
                    write_byte_to_bytecode(ctx, LE_I32);
                return make_yes_with_type(rel <= 0, MK_TYPE(YES, res_is_var));
            } else {
                if (res_is_var)
                    write_byte_to_bytecode(ctx, GE_I32);
                return make_yes_with_type(rel >= 0, MK_TYPE(YES, res_is_var));
            }
        }
    }

    if (strcmp(op, "+") == 0) {
        if (val_is_str(left) && val_is_str(right)) {
            size_t ln = strlen(left.as.str);
            size_t rn = strlen(right.as.str);
            char *str;
            if (ln > (size_t)-1 - rn - 1)
                return make_nan();
            str = (char *)malloc(ln + rn + 1);
            if (!str)
                return make_nan();
            memcpy(str, left.as.str, ln);
            memcpy(str + ln, right.as.str, rn + 1);
            if (res_is_var)
                write_byte_to_bytecode(ctx, ADD_STR);
            return make_str_with_type(str, res_type);
        }
    }
    int32_t a = val_as_i32(left), b = val_as_i32(right);
    if (!val_is_number(left) || !val_is_number(right))
        return make_nan();
    Val ret = make_nan();
    if (strcmp(op, "+") == 0) {
        if (res_is_var)
            write_byte_to_bytecode(ctx, ADD_I32);
        return make_i32_with_type(a + b, res_type);
    } else if (strcmp(op, "-") == 0) {
        if (res_is_var)
            write_byte_to_bytecode(ctx, SUB_I32);
        return make_i32_with_type(a - b, res_type);
    } else if (strcmp(op, "*") == 0) {
        if (res_is_var)
            write_byte_to_bytecode(ctx, MUL_I32);
        return make_i32_with_type(a * b, res_type);
    } else if (strcmp(op, "/") == 0) {
        if (res_is_var)
            write_byte_to_bytecode(ctx, DIV_I32);
        return make_i32_with_type(a / b, res_type);
    } else if (strcmp(op, "%") == 0) {
        if (res_is_var)
            write_byte_to_bytecode(ctx, MOD_I32);
        return make_i32_with_type(a % b, res_type);
    } else
        *err_out = (char *)"Inavlid bin op";
    return ret;
}

/*
 * Pratt expression parser — evaluates during parse (no AST).
 *
 * binding powers (higher = tighter):
 *   ||                1
 *   &&                2
 *   |                 3
 *   ^                 4
 *   &                 5
 *   == !=             6
 *   < > <= >=         7
 *   << >>             8
 *   + -               9
 *   * / %            10
 *   unary + - !      11  (prefix)
 *
 * '=' is statement-level only (not an infix here).
 * Calls are parsed only as ident(...), not as postfix on arbitrary values.
 */

enum { PRATT_MAX_ARGS = 8 };

static inline unsigned char infix_bp(const Token *t) {
    if (!t || t->kind != TOK_BINOP)
        return 0;
    const char *op = t->op;
    if (strcmp(op, "||") == 0) {
        return 1;
    }
    if (strcmp(op, "&&") == 0) {
        return 2;
    }
    if (strcmp(op, "|") == 0) {
        return 3;
    }
    if (strcmp(op, "^") == 0) {
        return 4;
    }
    if (strcmp(op, "&") == 0) {
        return 5;
    }
    if (strcmp(op, "=") == 0 || strcmp(op, "!=") == 0) {
        return 6;
    }
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || strcmp(op, "<=") == 0 ||
        strcmp(op, ">=") == 0) {
        return 7;
    }
    if (strcmp(op, "<<") == 0 || strcmp(op, ">>") == 0) {
        return 8;
    }
    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0) {
        return 9;
    }
    if (strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
        return 10;
    }
    return 0;
}

static inline bool is_prefix_op(const Token *t) {
    return ts_tok_is_binop(t, "+") || ts_tok_is_binop(t, "-") ||
           ts_tok_is_binop(t, "!");
}

static inline Val literal_from_token(Token *t) {
    if (t->kind == TOK_INT)
        return make_i32(atoi(t->text));
    if (t->kind == TOK_STR) {
        size_t n = strlen(t->text);
        if (n >= 2 && t->text[0] == '\'' && t->text[n - 1] == '\'') {
            char *s = (char *)malloc(n - 1);
            if (!s)
                return make_nan();
            memcpy(s, t->text + 1, n - 2);
            s[n - 2] = '\0';
            return make_str(s);
        }
        return make_nan();
    }
    return make_nan();
}

typedef struct {
        TokenStream *ts;
        const char bytecode[256];
        const char *err;
        int symbol;
} PrattCompileCtx;

uint8_t *encode_const_pool(CompileCtx *ctx, uint16_t *const_pool_size) {
    int const_pool_cap = 64;
    uint8_t *const_pool = (uint8_t *)malloc(const_pool_cap);
    uint16_t offset = sizeof(uint16_t);
    const char **const_strs = ctx->const_strs;
    for (int i = 0; i < ctx->const_strs_size; ++i) {
        const size_t length = strlen(const_strs[i]);
        while ((size_t)offset + sizeof(uint16_t) + length >
               (size_t)const_pool_cap) {
            const_pool_cap *= 2;
            const_pool = (uint8_t *)realloc(const_pool, const_pool_cap);
        }
        const_pool[offset++] = length;
        memcpy(&const_pool[offset], const_strs[i], length);
        offset += length;
    }
#ifdef DEBUG
    printf("\nconst pool:\n");
    printf("  const pool offset: %u\n", offset);
#endif
    memcpy(const_pool, &offset, 2);
    ctx->const_pool = const_pool;
    *const_pool_size = offset;
    return const_pool;
}

void decode_const_pool(char **const_strs, uint8_t *buffer, uint16_t offset) {
#ifdef DEBUG
    printf("const pool:\n");
    printf("  const pool offset: %u\n", offset);
#endif
    int decode_pos = sizeof(offset);
    int const_strs_pos = 0;
    int const_strs_cap = 5;
    while (decode_pos + (int)sizeof(uint16_t) <= offset) {
        uint8_t length = buffer[decode_pos++];
        if (const_strs_pos == const_strs_cap) {
            const_strs_cap *= 2;
            const_strs =
                (char **)realloc(const_strs, const_strs_cap * sizeof(char *));
        }
        char *str = (char *)malloc(length + 1);
        if (!str)
            break;
        memcpy(str, &buffer[decode_pos], length);
        str[length] = '\0';
        decode_pos += length;
#ifdef DEBUG
        printf("  str[%u]: %s\n", length, str);
#endif
        const_strs[const_strs_pos++] = str;
    }
}

static inline Val compile_expr_bp(CompileCtx *ctx, unsigned char min_bp);

static inline Val compile_prefix(CompileCtx *ctx) {
    Token *t = ts_tok_peek(ctx->ts);

    if (t->kind == TOK_INT || t->kind == TOK_STR) {
        Val v = literal_from_token(t);
        ts_tok_consume(ctx->ts);
        return v;
    } else if (t->kind == TOK_IDENT) {
        SchemaCol col;
        if (!col_in_schema(t->text, &col)) {
            ctx->err = alloc_format("Unknown column: %s\n", t->text);
            ctx->symbol = t->symbol;
            return make_nan();
        }
        ts_tok_consume(ctx->ts);
        switch (col.type) {
        case STR: {
            Val val_str = make_str_with_type(t->var_name, V_STR);
#ifdef DEBUG
            printf("LOAD_VAR_STR %s", val_str.as.str);
#endif
            write_byte_to_bytecode(ctx, LOAD_VAR_STR);
            write_byte_to_bytecode(ctx, col.idx);
            return val_str;
            break;
        }
        case I32: {
            Val val_i32 = make_i32_with_type(atoi(t->var_name), V_I32);
#ifdef DEBUG
            printf("LOAD_VAR_I32 %d", val_i32.as.i32);
#endif
            write_byte_to_bytecode(ctx, LOAD_VAR_I32);
            write_byte_to_bytecode(ctx, col.idx);
            return val_i32;
            break;
        }
        case YES: {
            Val val_yes = make_yes_with_type(atoi(t->var_name), V_YES);
#ifdef DEBUG
            printf("LOAD_VAR_YES %d", val_yes.as.yes);
#endif
            write_byte_to_bytecode(ctx, LOAD_VAR_YES);
            write_byte_to_bytecode(ctx, col.idx);
            return val_yes;
            break;
        }
        default:
            assert(0);
        }
    } else if (ts_tok_match(ctx->ts, TOK_LPAREN)) {
        Val inner = compile_expr_bp(ctx, 0);
        if (ctx->err)
            return make_nan();
        if (!ts_tok_match(ctx->ts, TOK_RPAREN)) {
            ctx->err = (char *)"expected ')'";
            ctx->symbol = t->symbol;
            return make_nan();
        }
        return inner;
    }

    if (is_prefix_op(t)) {
        char op[3];
        memcpy(op, t->op, sizeof(op));
        ts_tok_consume(ctx->ts);
        Val x = compile_expr_bp(ctx, 11);
        if (ctx->err)
            return make_nan();
        Val r = apply_unop(op, x);
        if (r.type == NUL) {
            ctx->err = (char *)"bad unary operation";
            ctx->symbol = t->symbol;
            return make_nan();
        }
        return r;
    }

    ctx->err = alloc_format("expected expression, got token: %s (%s)", t->text,
                            token_kind_name(t->kind));
    ctx->symbol = t->symbol;
    return make_nan();
}

Val compile_expr_bp(CompileCtx *ctx, unsigned char min_bp) {
    Val left = compile_prefix(ctx);
    if (ctx->err)
        return make_nan();

    for (;;) {
        Token *t = ts_tok_peek(ctx->ts);
        unsigned char bp = infix_bp(t);
        if (bp == 0 || bp < min_bp)
            break;
        int op_pos = ctx->ts->pos - 1;
        char op[3];
        memcpy(op, t->op, sizeof(op));
        ts_tok_consume(ctx->ts);
        Val right = compile_expr_bp(ctx, bp + 1);
        if (ctx->err)
            return make_nan();

        char *op_err = NULL;
        Val r = compile_binop(ctx, op, left, right, &op_err);
        if (op_err) {
            ctx->err = op_err;
            ctx->symbol = op_pos;
            return make_nan();
        }
        if (r.type == NUL) {
            Val left_str = cast_val(STR, left);
            Val right_str = cast_val(STR, right);
            ctx->err = alloc_format("bad binary operation with '%s' "
                                    "and '%s'",
                                    left_str.as.str, right_str.as.str);
            ctx->symbol = op_pos;
            return make_nan();
        }
        left = r;
    }
    return left;
}

Val compile(CompileCtx *ctx, const char *opcode_path, bool do_cache,
            char **err_out, int *symbol_out) {
#ifdef DEBUG
    printf("\n========================\n");
    printf("      COMPILATION\n");
    printf("========================\n");
#endif
    Val res = compile_expr_bp(ctx, 0);
    if (ctx->err) {
        *err_out = ctx->err;
        *symbol_out = ctx->symbol;
        return make_nan();
    }
#ifdef DEBUG
    printf("res: %s\n", type_to_str(res.type));
#endif
    if (!IS_VAR(res.type)) {
        switch (res.type) {
        case STR:
            write_byte_to_bytecode(ctx, PUSH_CONST_STR);
            add_str_to_stack(ctx, res.as.str);
            break;
        case I32:
            write_byte_to_bytecode(ctx, PUSH_CONST_I32);
            add_i32_to_stack(ctx, res.as.i32);
            break;
        case YES:
            write_byte_to_bytecode(ctx, PUSH_CONST_YES);
            add_yes_to_stack(ctx, res.as.yes);
            break;
        }
    }
    uint16_t const_pool_size = 0;
    uint8_t *const_pool = encode_const_pool(ctx, &const_pool_size);
    uint8_t *bytecode = ctx->bytecode;
    int bytecode_size = ctx->bytecode_size;
    if (do_cache) {
        FILE *f = fopen(opcode_path, "wb");
        fwrite(const_pool, 1, const_pool_size, f);
        fwrite(bytecode, 1, bytecode_size, f);
        fclose(f);
    }
#ifdef DEBUG
    printf("Compiled: ");
    for (int i = 0; i < const_pool_size; i++) {
        printf("%02X ", const_pool[i]);
    }
    printf("| ");
    for (int i = 0; i < bytecode_size; i++) {
        printf("%02X ", bytecode[i]);
    }
    printf("\n");
#endif
    return res;
}

// ============================================================================
//                                    STACK
// ============================================================================

typedef union {
        bool yes;
        int32_t i32;
        char *str;
} Value;

typedef struct {
        uint8_t *data;
        unsigned int pos;
        unsigned int size;
} Stack;

Stack stack_new(unsigned int size) {
    Stack stack = {NULL, 0, size};
    stack.data = (uint8_t *)malloc(size);
    return stack;
}

void stack_push_val(Stack *stack, Value value) {
    if (stack->pos + sizeof(Value) >= stack->size) {
        stack->size *= 2;
        stack->data = (uint8_t *)realloc(stack->data, stack->size);
    }
    memcpy(&stack->data[stack->pos], &value, sizeof(Value));
    stack->pos += sizeof(Value);
}

void stack_peek_val(Stack *stack, Value *value) {
    if (stack->pos < sizeof(Value)) {
#ifdef DEBUG
        printf("  err: stack underflow on peek val\n");
#endif
        return;
    }
    memcpy(value, &stack->data[stack->pos], sizeof(Value));
}

void stack_pop_val(Stack *stack, Value *value) {
    if (stack->pos < sizeof(Value)) {
#ifdef DEBUG
        printf("  err: stack underflow on pop val\n");
#endif
        return;
    }
    stack->pos -= sizeof(Value);
    memcpy(value, &stack->data[stack->pos], sizeof(Value));
}

void stack_push_uint32(Stack *stack, uint32_t i32) {
    if (stack->pos + sizeof(uint32_t) >= stack->size) {
        stack->size *= 2;
        stack->data = (uint8_t *)realloc(stack->data, stack->size);
    }
    memcpy(&stack->data[stack->pos], &i32, sizeof(uint32_t));
    stack->pos += sizeof(uint32_t);
}

uint32_t stack_peek_uint32(Stack *stack) {
    if (stack->pos < sizeof(uint32_t)) {
#ifdef DEBUG
        printf("  err: stack underflow on peek uint32\n");
#endif
        return 0;
    }
    return *(uint32_t *)&stack->data[stack->pos - sizeof(uint32_t)];
}

uint32_t stack_pop_uint32(Stack *stack) {
    if (stack->pos < sizeof(uint32_t)) {
#ifdef DEBUG
        printf("  err: stack underflow on pop uint32\n");
#endif
        return 0;
    }
    stack->pos -= sizeof(uint32_t);
    return *(uint32_t *)&stack->data[stack->pos];
}

void stack_push_byte(Stack *stack, uint8_t byte) {
    if (stack->pos == stack->size) {
        stack->size *= 2;
        stack->data = (uint8_t *)realloc(stack->data, stack->size);
    }
    stack->data[stack->pos++] = byte;
}

uint8_t stack_peek_byte(Stack *stack) {
    if (stack->pos == 0) {
#ifdef DEBUG
        printf("  err: stack underflow on peek byte\n");
#endif
        return 0;
    }
    return stack->data[stack->pos - 1];
}

uint8_t stack_pop_byte(Stack *stack) {
    if (stack->pos == 0) {
#ifdef DEBUG
        printf("  err: stack underflow on pop byte\n");
#endif
        return 0;
    }
    return stack->data[--stack->pos];
}

// ============================================================================
//                                  EXECUTION
// ============================================================================

typedef struct {
        unsigned char *instructions;
        unsigned int pos;
        unsigned int size;
} OpcodeStream;

char os_peek(OpcodeStream *stream) { return stream->instructions[stream->pos]; }
uint32_t os_peek_uint32(OpcodeStream *stream) {
    uint32_t value;
    memcpy(&value, &stream->instructions[stream->pos], sizeof(uint32_t));
#ifdef DEBUG
    printf("  peek value: ");
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        uint8_t byte = stream->instructions[stream->pos + i];
        printf("%02X ", byte);
    }
    printf("(%d)\n", value);
#endif
    return value;
}

void os_consume(OpcodeStream *stream) { stream->pos++; }
void os_consume_value(OpcodeStream *stream) { stream->pos += sizeof(Value); }
void os_consume_uint32(OpcodeStream *stream) {
    stream->pos += sizeof(uint32_t);
}
bool os_eof(OpcodeStream *stream) { return stream->pos > stream->size; }

int exec_instruction(Stack *stack, OpcodeStream *stream, char **const_strs,
                     Dest *dest) {
#ifdef DEBUG
    printf("%d: %02X ", stream->pos, stream->instructions[stream->pos]);
#endif
    switch (stream->instructions[stream->pos]) {
    case LOAD_VAR_I32:
#ifdef DEBUG
        printf("LOAD_VAR_I32:\n");
#endif
        os_consume(stream);
        if (os_eof(stream)) {
#ifdef DEBUG
            printf("  err: eof\n");
#endif
            return -1;
        }
        {
            unsigned char idx = os_peek(stream);
            ValUnion val;
            if (!get_col_ptr(dest, idx, &val)) {
#ifdef DEBUG
                printf("  err: get_col_ptr\n");
#endif
                return -1;
            }
#ifdef DEBUG
            printf("  load idx %u (%d)\n", idx, val.i32);
#endif
            stack_push_uint32(stack, val.i32);
            os_consume(stream);
        }
        break;
    case LOAD_VAR_STR:
#ifdef DEBUG
        printf("LOAD_VAR_STR:\n");
#endif
        os_consume(stream);
        if (os_eof(stream)) {
#ifdef DEBUG
            printf("  err: eof\n");
#endif
            return -1;
        }
        {
            unsigned char idx = os_peek(stream);
            ValUnion val;
            if (!get_col_ptr(dest, idx, &val)) {
#ifdef DEBUG
                printf("  err: get_col_ptr\n");
#endif
                return -1;
            }
#ifdef DEBUG
            printf("  load idx %u (%s)\n", idx, val.str);
#endif
            stack_push_val(stack, (Value){.str = val.str});
            os_consume(stream);
        }
        break;
    case LOAD_VAR_YES:
#ifdef DEBUG
        printf("LOAD_VAR_YES:\n");
#endif
        os_consume(stream);
        if (os_eof(stream)) {
#ifdef DEBUG
            printf("  err: eof\n");
#endif
            return -1;
        }
        {
            unsigned char idx = os_peek(stream);
            ValUnion val;
            if (!get_col_ptr(dest, idx, &val)) {
#ifdef DEBUG
                printf("  err: get_col_ptr\n");
#endif
                return -1;
            }
#ifdef DEBUG
            printf("  load idx %u (%d)\n", idx, val.yes);
#endif
            stack_push_byte(stack, val.yes);
            os_consume(stream);
        }
        break;
    case PUSH_CONST_I32:
#ifdef DEBUG
        printf("PUSH_CONST_I32:\n");
#endif
        os_consume(stream);
        if (stream->size - stream->pos < sizeof(uint32_t)) {
#ifdef DEBUG
            printf("  err: trailing bytes: %ud, needed %zu",
                   stream->size - stream->pos, sizeof(uint32_t));
#endif
            return -1;
        }
        stack_push_uint32(stack, os_peek_uint32(stream));
        os_consume_uint32(stream);
        break;
    case PUSH_CONST_STR:
#ifdef DEBUG
        printf("PUSH_CONST_STR:\n");
#endif
        os_consume(stream);
        if (os_eof(stream)) {
#ifdef DEBUG
            printf("  err: eof\n");
#endif
            return -1;
        }
        {
            unsigned char idx = os_peek(stream);
            char *str = const_strs[idx];
#ifdef DEBUG
            printf("  push idx %u: %s\n", idx, str);
#endif
            stack_push_val(stack, (Value){.str = str});
            os_consume(stream);
        }
        break;
    case PUSH_CONST_YES:
#ifdef DEBUG
        printf("PUSH_CONST_YES:\n");
#endif
        os_consume(stream);
        if (stream->size - stream->pos < sizeof(uint8_t)) {
#ifdef DEBUG
            printf("  err: trailing bytes: %ud, needed %zu",
                   stream->size - stream->pos, sizeof(uint8_t));
#endif
            return -1;
        }
        stack_push_byte(stack, os_peek(stream));
        os_consume(stream);
        break;
    case POP:
#ifdef DEBUG
        printf("POP:\n");
#endif
        os_consume(stream);
        Value value;
        stack_pop_val(stack, &value);
        break;
    case EQ_I32:
#ifdef DEBUG
        printf("EQ_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("  left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left == right);
        }
        break;
    case EQ_STR:
#ifdef DEBUG
        printf("STR_EQ:\n");
#endif
        os_consume(stream);
        {
            Value right;
            stack_pop_val(stack, &right);
            Value left;
            stack_pop_val(stack, &left);
#ifdef DEBUG
            printf("  left: %s, right: %s\n", left.str, right.str);
#endif
            stack_push_byte(stack, strcmp(left.str, right.str) == 0);
        }
        break;
    case NEQ_I32:
#ifdef DEBUG
        printf("NEQ_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("  left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left != right);
        }
        break;
    case NEQ_STR:
#ifdef DEBUG
        printf("NEQ_STR:\n");
#endif
        os_consume(stream);
        {
            Value right;
            stack_pop_val(stack, &right);
            Value left;
            stack_pop_val(stack, &left);
#ifdef DEBUG
            printf("  left: %s, right: %s\n", left.str, right.str);
#endif
            stack_push_byte(stack, strcmp(left.str, right.str) != 0);
        }
        break;
    case GE_I32:
#ifdef DEBUG
        printf("GE_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("  left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left >= right);
        }
        break;
    case LE_I32:
#ifdef DEBUG
        printf("LE_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("  left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left <= right);
        }
        break;
    case G_I32:
#ifdef DEBUG
        printf("G_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("  left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left > right);
        }
        break;
    case L_I32:
#ifdef DEBUG
        printf("L_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("  left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left < right);
        }
        break;
    case AND:
#ifdef DEBUG
        printf("AND:\n");
#endif
        os_consume(stream);
        {
            uint8_t right = stack_pop_byte(stack);
            uint8_t left = stack_pop_byte(stack);
#ifdef DEBUG
            printf("left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left && right);
        }
        break;
    case OR:
#ifdef DEBUG
        printf("OR:\n");
#endif
        os_consume(stream);
        {
            uint8_t right = stack_pop_byte(stack);
            uint8_t left = stack_pop_byte(stack);
#ifdef DEBUG
            printf("left: %d, right: %d\n", left, right);
#endif
            stack_push_byte(stack, left || right);
        }
        break;
    case ADD_I32:
#ifdef DEBUG
        printf("ADD_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("left: %d, right: %d\n", left, right);
#endif
            stack_push_uint32(stack, left + right);
        }
        break;
    case SUB_I32:
#ifdef DEBUG
        printf("SUB_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("left: %d, right: %d\n", left, right);
#endif
            stack_push_uint32(stack, left - right);
        }
        break;
    case MUL_I32:
#ifdef DEBUG
        printf("MUL_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
#ifdef DEBUG
            printf("left: %d, right: %d\n", left, right);
#endif
            stack_push_uint32(stack, left * right);
        }
        break;
    case DIV_I32:
#ifdef DEBUG
        printf("DIV_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
            stack_push_uint32(stack, left / right);
        }
        break;
    case MOD_I32:
#ifdef DEBUG
        printf("MOD_I32:\n");
#endif
        os_consume(stream);
        {
            uint32_t right = stack_pop_uint32(stack);
            uint32_t left = stack_pop_uint32(stack);
            stack_push_uint32(stack, left % right);
        }
        break;
    default:
#ifdef DEBUG
        printf("UNKNOWN\n");
#endif
        return -1;
    }
    return 0;
}

int execute(uint8_t *instructions, unsigned int size, char **const_strs,
            Dest *dest) {
    Stack stack = stack_new(10);
    OpcodeStream stream = {instructions, 0, size};
    while (stream.pos < stream.size) {
        int ret = exec_instruction(&stack, &stream, const_strs, dest);
        if (ret == -1) {
#ifdef DEBUG
            printf("  err: ret = %d\n", ret);
#endif
            return ret;
        } else if (ret != 0) {
#ifdef DEBUG
            printf(" early ret = %d\n", ret);
#endif
            return ret;
        }
    }
#ifdef DEBUG
    printf("return pop\n");
#endif
    return stack_pop_byte(&stack);
}
