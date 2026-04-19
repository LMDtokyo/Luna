/* =========================================================================
 * luna_bootstrap.c  -  Minimal C bootstrap for the Luna compiler.
 * -------------------------------------------------------------------------
 *  This file is a one-shot, throwaway compiler: its sole purpose is to
 *  compile `src/core/main.luna` (and its transitive imports) into a Linux
 *  x86-64 ELF64 executable so that the resulting self-hosted `luna`
 *  binary can rebuild itself.  It is never shipped, never updated beyond
 *  the minimum needed for the current main.luna, and intentionally
 *  supports only a small subset of the language.
 *
 *  Build:    cc -O2 -std=c99 -o luna-boot luna_bootstrap.c
 *  Run:      ./luna-boot <input.luna> [-o <out>] [-v]
 *
 *  Sections in this file:
 *      1.  types + constants
 *      2.  arena allocator
 *      3.  string / byte helpers
 *      4.  lexer
 *      5.  AST + parser
 *      6.  name resolution / light type pass
 *      7.  x86-64 instruction emitters
 *      8.  lowerer (AST -> machine code)
 *      9.  ELF64 writer
 *      10. main + driver
 *
 *  Written for C99.  Depends only on <stdio.h>, <stdlib.h>, <string.h>,
 *  <stdint.h>, <unistd.h>.
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* ========================================================================= */
/* 1. TYPES + CONSTANTS                                                       */
/* ========================================================================= */

#define MAX_SRC_BYTES     (8 * 1024 * 1024)
#define MAX_TOKENS        (1 << 20)
#define MAX_NODES         (1 << 18)       /* 256K — match arms add nodes */
#define MAX_FILES         256
#define KIDS_INLINE       256             /* max children per AST node */
#define MAX_SYMS          (1 << 16)
#define MAX_STRS          (1 << 15)
#define MAX_RELOCS        (1 << 15)
#define MAX_INCLUDE_PATHS 8
#define ARENA_BYTES       (16 * 1024 * 1024)
#define CODE_CAP          (16 * 1024 * 1024)
#define RODATA_CAP        (1 * 1024 * 1024)

/* Luna syscall numbers we care about. */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_NANOSLEEP   35
#define SYS_EXIT        60
#define SYS_EXIT_GROUP  231

/* Token kinds.  Keep this list small but complete enough for the subset. */
enum {
    TK_EOF = 0,
    TK_IDENT,        /* bare identifier (after stripping @) */
    TK_ATIDENT,      /* @ident                              */
    TK_INT,          /* integer literal                     */
    TK_STR,          /* string literal                      */
    TK_KW_FN,
    TK_KW_CONST,
    TK_KW_LET,
    TK_KW_MEOW,
    TK_KW_RETURN,
    TK_KW_IF,
    TK_KW_ELSE,
    TK_KW_ECLIPSE,   /* `elif`-style                        */
    TK_KW_WHILE,
    TK_KW_ORBIT,     /* for-loop                            */
    TK_KW_IN,
    TK_KW_BREAK,
    TK_KW_CONTINUE,
    TK_KW_PASS,
    TK_KW_TRUE,
    TK_KW_FALSE,
    TK_KW_NIL,
    TK_KW_NOT,
    TK_KW_AND,       /* word form of &&                     */
    TK_KW_OR,        /* word form of ||                     */
    TK_KW_STRUCT,
    TK_KW_EXTERN,
    TK_KW_IMPORT,
    TK_KW_UNSAFE,
    TK_KW_MATCH,     /* diagnosed as unsupported            */
    TK_KW_PHASE,     /* diagnosed as unsupported            */
    TK_KW_ACTOR,     /* diagnosed as unsupported            */
    TK_KW_FLOW,
    TK_KW_SPAWN,
    TK_KW_SEND,
    TK_KW_DEFER,
    TK_KW_NOVA,
    TK_KW_THEN,      /* `if X then A else B` — ternary form         */
    TK_KW_SELF,      /* used in trait impls                         */
    TK_KW_SEAL,      /* immutable-after-init binding                */
    TK_KW_TYPE,      /* ADT declaration (treated like struct skip)  */
    TK_KW_TRAIT,
    TK_KW_IMPL,
    TK_KW_FOR,
    TK_KW_MOVE,
    TK_KW_FREEZE,
    TK_KW_BOX,
    TK_KW_RC,
    TK_KW_WEAK,
    TK_KW_ATOMIC,
    TK_KW_DROP,
    TK_KW_GUARD,
    TK_KW_UNLESS,
    TK_KW_REQUIRE,
    TK_KW_ENSURE,
    TK_KW_WHERE,
    TK_KW_SHINE,
    TK_KW_ECLIPSE2,  /* NOT USED — placeholder to preserve numbering if needed */
    TK_KW_ENUM,
    TK_KW_USE,
    TK_KW_EXPORT,
    TK_KW_PUB,
    TK_KW_AS,
    TK_KW_MUT,
    TK_KW_REF,
    TK_KW_ASM,
    TK_LPAREN, TK_RPAREN,
    TK_LBRACE, TK_RBRACE,
    TK_LBRACK, TK_RBRACK,
    TK_COMMA, TK_SEMI, TK_COLON, TK_DOT, TK_DOTDOT,
    TK_ARROW,        /* ->                                  */
    TK_FAT_ARROW,    /* =>  (pattern/body separator)        */
    TK_ASSIGN,       /* =                                   */
    TK_EQ,           /* ==                                  */
    TK_NE,           /* !=                                  */
    TK_LT, TK_LE, TK_GT, TK_GE,
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_ANDAND, TK_OROR,
    TK_QMARK,        /* postfix ?                           */
    TK_BANG,         /* unary !                             */
    TK_AMP,          /* &                                   */
    TK_PIPE,         /* |                                   */
    TK_CARET,        /* ^                                   */
    TK_TILDE,        /* ~                                   */
    TK_SHL,          /* <<                                  */
    TK_SHR,          /* >>                                  */
    TK_AT            /* @ when not part of @ident           */
};

typedef struct {
    int  kind;
    int  file;
    int  line;
    int  col;
    /* Payload: for TK_INT int_val, for TK_STR/TK_IDENT/TK_ATIDENT str
       is (start, len) into the source buffer of the owning file.      */
    long long  int_val;
    const char *str;
    int         len;
} Tok;

/* AST node kinds. */
enum {
    N_NIL = 0,
    N_MODULE,         /* top level container, children = decls     */
    N_IMPORT,         /* data.s = module name                      */
    N_FN,             /* fields: name, params[], ret_ty, body      */
    N_EXTERN_FN,      /* fields: abi, name, params[], ret_ty       */
    N_CONST,          /* name, type, value                         */
    N_STRUCT,         /* name, fields[]                            */
    N_FIELD,          /* name, type                                */
    N_PARAM,          /* name, type                                */
    N_TYPE,           /* data.s = type name; data1 = ptr-flag      */
    N_BLOCK,          /* list of stmts                             */
    N_LET,            /* name, type?, value                        */
    N_ASSIGN,         /* lhs, rhs                                  */
    N_RETURN,         /* value?                                    */
    N_IF,             /* cond, then, else?                         */
    N_WHILE,          /* cond, body                                */
    N_ORBIT,          /* name, lo, hi, body                        */
    N_BREAK, N_CONTINUE, N_PASS,
    N_EXPR_STMT,      /* a single expression used as a statement   */
    N_BIN,            /* op in data1 (token kind), l, r            */
    N_UNARY,          /* op in data1, child                        */
    N_POSTFIX_Q,      /* child  (phase<T> propagator)              */
    N_INT,            /* data.iv                                   */
    N_STR,            /* data.s, data.ilen, data.str_idx           */
    N_BOOL,           /* data1 = 0|1                               */
    N_NILV,
    N_IDENT,          /* data.s = name                             */
    N_FIELD_ACCESS,   /* obj, field_name                           */
    N_CALL,           /* callee, args[]                            */
    N_GROUP,          /* single child  ( expr )                    */
    N_UNSAFE_BLOCK,   /* child = block, ignored marker             */
    /* v0.2 bootstrap additions for compiling real Luna modules */
    N_STRUCT_LIT,     /* s = type name; kids = N_FIELD_INIT list   */
    N_FIELD_INIT,     /* s = field name; kid[0] = value expr       */
    N_ARRAY_LIT,      /* kids = [value, count]  for [v; N] form    */
    N_INDEX,          /* kids = [base, index]   for a[i]           */
    N_MATCH_STUB,     /* match/phase with unsupported patterns     */
    N_MATCH,          /* kids = [scrutinee, arm1, arm2, ...]       */
    N_MATCH_ARM       /* kids = [pattern, body]                    */
};

typedef struct AstNode {
    int  kind;
    int  file;
    int  line;
    /* children: up to KIDS_INLINE inline slots */
    int  nkids;
    int  kids[KIDS_INLINE];
    /* generic scalar payloads */
    long long iv;
    int       i1;
    int       i2;
    int       str_idx;  /* for N_STR: index into string-pool */
    const char *s;
    int         slen;
} AstNode;

/* Relocation: a place where we wrote a call/jmp rel32 that must be patched
   once we know the target function's final offset.                       */
typedef struct {
    int code_off;      /* offset in code buffer of the 4-byte rel32 slot */
    int target_fn;     /* index into sym[]                               */
    int from_off;      /* rip at that point (code_off + 4)               */
} Reloc;

/* String literal record. */
typedef struct {
    int  off;          /* offset in rodata buffer                         */
    int  len;
} Str;

/* Symbol (global function / const / struct type). */
typedef struct {
    int  kind;         /* N_FN, N_EXTERN_FN, N_CONST, N_STRUCT            */
    const char *name;
    int  name_len;
    int  node;         /* ast node index                                  */
    int  code_off;     /* for N_FN: final offset in code buf              */
    int  arity;
    int  syscall_nr;   /* for extern linux_syscall                        */
    /* struct layout cache */
    int  size;
    int  nfields;
} Sym;

/* Compilation unit (one source file). */
typedef struct {
    const char *path;
    char *src;
    int   src_len;
    int   tok_start;   /* first token index belonging to this file      */
    int   tok_end;
    int   node_root;   /* N_MODULE node index                           */
} CUnit;

/* Local variable slot (per function). */
typedef struct {
    const char *name;
    int  name_len;
    int  offset;       /* negative; relative to RBP                      */
    int  is_param;
    int  param_idx;
    const char *type_name;   /* struct type name, or NULL if unknown     */
    int  type_name_len;
} Local;

/* ========================================================================= */
/* Globals                                                                   */
/* ========================================================================= */

static char    *g_arena_base;
static size_t   g_arena_used;

static Tok     *g_toks;
static int      g_ntoks;

static AstNode *g_nodes;
static int      g_nnodes;

static CUnit    g_files[MAX_FILES];
static int      g_nfiles;

static Sym      g_syms[MAX_SYMS];
static int      g_nsyms;

static Str      g_strs[MAX_STRS];
static int      g_nstrs;

static uint8_t *g_code;
static int      g_code_len;

static uint8_t *g_rodata;
static int      g_rodata_len;

static Reloc    g_relocs[MAX_RELOCS];
static int      g_nrelocs;

static const char *g_include_paths[MAX_INCLUDE_PATHS];
static int         g_ninclude = 0;

static int      g_verbose = 0;

/* Output target.  Auto-detected from the host OS at startup; overridable
 * via `--target {linux,windows}` on the command line.                     */
enum { TARGET_LINUX = 1, TARGET_WINDOWS = 2 };
static int      g_target = 0;

/* Cached rodata index of the "\n" literal used by shine().  Lazily
 * allocated the first time shine emits a print sequence.                  */
static int      g_newline_str_idx = -1;

/* Offset in g_code of the lazily-emitted `_luna_print_int` helper, or -1
 * if the helper hasn't been requested yet.  The helper converts an integer
 * (passed in rdi on Linux, rcx on Windows) to decimal and writes it to
 * stdout followed by a newline.  All print_int / shine_int call sites
 * emit a regular `call rel32` — the reloc is recorded in g_helper_relocs
 * and patched once the helper's final offset is known.                    */
static int      g_print_int_helper_off = -1;

typedef struct {
    int code_off;     /* offset of the 4-byte disp slot                    */
    int helper;       /* HELPER_PRINT_INT (room for more later)            */
} HelperReloc;

enum { HELPER_PRINT_INT = 0 };

#define MAX_HELPER_RELOCS 4096
static HelperReloc g_helper_relocs[MAX_HELPER_RELOCS];
static int         g_nhelper_relocs;

/* Current function codegen state. */
static Local    g_locals[256];
static int      g_nlocals;
static int      g_frame_size;       /* bytes reserved via sub rsp, N      */
static int      g_cur_fn_sym;
static int      g_loop_break_patch[32];
static int      g_nbreak_patch;
static int      g_loop_cont_target[32];
static int      g_loop_depth;

/* ========================================================================= */
/* 2. ARENA ALLOCATOR                                                         */
/* ========================================================================= */

static void arena_init(void)
{
    g_arena_base = (char *)malloc(ARENA_BYTES);
    if (!g_arena_base) { fprintf(stderr, "luna-boot: out of memory (arena)\n"); exit(1); }
    g_arena_used = 0;
}

static void *arena_alloc(size_t n)
{
    /* 8-byte align */
    g_arena_used = (g_arena_used + 7u) & ~(size_t)7u;
    if (g_arena_used + n > ARENA_BYTES) {
        fprintf(stderr, "luna-boot: arena exhausted (needed %zu)\n", n);
        exit(1);
    }
    void *p = g_arena_base + g_arena_used;
    g_arena_used += n;
    memset(p, 0, n);
    return p;
}

static char *arena_strndup(const char *s, int n)
{
    char *p = (char *)arena_alloc((size_t)n + 1);
    memcpy(p, s, (size_t)n);
    p[n] = 0;
    return p;
}

/* ========================================================================= */
/* 3. STRING / BYTE HELPERS                                                   */
/* ========================================================================= */

static int streq_n(const char *a, int alen, const char *b)
{
    int blen = (int)strlen(b);
    return alen == blen && memcmp(a, b, (size_t)alen) == 0;
}

static int streq_nn(const char *a, int alen, const char *b, int blen)
{
    return alen == blen && memcmp(a, b, (size_t)alen) == 0;
}

static void die_at(const char *file, int line, const char *msg)
{
    fprintf(stderr, "luna-boot: %s at %s:%d\n", msg, file ? file : "?", line);
    exit(1);
}

static void die_unsup(const char *feat, const char *file, int line)
{
    fprintf(stderr, "luna-boot: unsupported: %s at %s:%d\n",
            feat, file ? file : "?", line);
    exit(1);
}

static char *slurp_file(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0 || sz > MAX_SRC_BYTES) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)arena_alloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    *out_len = (int)n;
    return buf;
}

static void code_emit_byte(uint8_t b)
{
    if (g_code_len >= CODE_CAP) {
        fprintf(stderr, "luna-boot: code buffer overflow\n"); exit(1);
    }
    g_code[g_code_len++] = b;
}

static void code_emit_bytes(const uint8_t *bs, int n)
{
    for (int i = 0; i < n; i++) code_emit_byte(bs[i]);
}

static void code_emit_u32(uint32_t v)
{
    code_emit_byte((uint8_t)(v       & 0xff));
    code_emit_byte((uint8_t)((v>>8)  & 0xff));
    code_emit_byte((uint8_t)((v>>16) & 0xff));
    code_emit_byte((uint8_t)((v>>24) & 0xff));
}

static void code_emit_u64(uint64_t v)
{
    for (int i = 0; i < 8; i++) code_emit_byte((uint8_t)((v >> (8*i)) & 0xff));
}

static void code_patch_u32(int off, uint32_t v)
{
    g_code[off+0] = (uint8_t)(v       & 0xff);
    g_code[off+1] = (uint8_t)((v>>8)  & 0xff);
    g_code[off+2] = (uint8_t)((v>>16) & 0xff);
    g_code[off+3] = (uint8_t)((v>>24) & 0xff);
}

/* Add a string literal to rodata, return pool index.
 *
 * Layout per string (so `shine(s)` and any future string op can read the
 * length at runtime without a separate parameter):
 *
 *     [ 8-byte little-endian length ] [ string bytes ] [ NUL ]
 *
 * The returned `off` points at the first byte of the string data (past the
 * length prefix). To read the length of a string whose address is in rax,
 * emit `mov rdx, [rax - 8]`. The trailing NUL is cosmetic — keeps C-style
 * interop possible, but the length is authoritative.
 */
static int strpool_add(const char *s, int len)
{
    if (g_nstrs >= MAX_STRS) { fprintf(stderr, "luna-boot: string pool full\n"); exit(1); }
    if (g_rodata_len + 8 + len + 1 > RODATA_CAP) {
        fprintf(stderr, "luna-boot: rodata overflow\n"); exit(1);
    }
    int idx = g_nstrs++;
    uint64_t llen = (uint64_t)(uint32_t)len;
    for (int i = 0; i < 8; i++) {
        g_rodata[g_rodata_len + i] = (uint8_t)(llen >> (i * 8));
    }
    g_rodata_len += 8;
    g_strs[idx].off = g_rodata_len;          /* points past the length prefix */
    g_strs[idx].len = len;
    memcpy(g_rodata + g_rodata_len, s, (size_t)len);
    g_rodata_len += len;
    g_rodata[g_rodata_len++] = 0;
    return idx;
}

/* ========================================================================= */
/* 4. LEXER                                                                   */
/* ========================================================================= */

static int is_id_start(int c) { return (c=='_') || (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
static int is_id_cont(int c)  { return is_id_start(c) || (c>='0'&&c<='9'); }
static int is_digit(int c)    { return c>='0' && c<='9'; }

struct Kw { const char *w; int tk; };
static struct Kw KWS[] = {
    { "fn",       TK_KW_FN       },
    { "const",    TK_KW_CONST    },
    { "let",      TK_KW_LET      },
    { "meow",     TK_KW_MEOW     },
    { "return",   TK_KW_RETURN   },
    { "if",       TK_KW_IF       },
    { "else",     TK_KW_ELSE     },
    { "eclipse",  TK_KW_ECLIPSE  },
    { "while",    TK_KW_WHILE    },
    { "orbit",    TK_KW_ORBIT    },
    { "in",       TK_KW_IN       },
    { "break",    TK_KW_BREAK    },
    { "continue", TK_KW_CONTINUE },
    { "pass",     TK_KW_PASS     },
    { "true",     TK_KW_TRUE     },
    { "false",    TK_KW_FALSE    },
    { "nil",      TK_KW_NIL      },
    { "not",      TK_KW_NOT      },
    { "and",      TK_KW_AND      },
    { "or",       TK_KW_OR       },
    { "struct",   TK_KW_STRUCT   },
    { "extern",   TK_KW_EXTERN   },
    { "import",   TK_KW_IMPORT   },
    { "unsafe",   TK_KW_UNSAFE   },
    { "match",    TK_KW_MATCH    },
    { "phase",    TK_KW_PHASE    },
    { "actor",    TK_KW_ACTOR    },
    { "flow",     TK_KW_FLOW     },
    { "spawn",    TK_KW_SPAWN    },
    { "send",     TK_KW_SEND     },
    { "defer",    TK_KW_DEFER    },
    { "nova",     TK_KW_NOVA     },
    { "elif",     TK_KW_ECLIPSE  },   /* alias: `elif` treated as `eclipse` */
    { "then",     TK_KW_THEN     },
    { "self",     TK_KW_SELF     },
    { "seal",     TK_KW_SEAL     },
    { "type",     TK_KW_TYPE     },
    { "trait",    TK_KW_TRAIT    },
    { "impl",     TK_KW_IMPL     },
    { "for",      TK_KW_FOR      },
    { "move",     TK_KW_MOVE     },
    { "freeze",   TK_KW_FREEZE   },
    { "box",      TK_KW_BOX      },
    { "rc",       TK_KW_RC       },
    { "weak",     TK_KW_WEAK     },
    { "atomic",   TK_KW_ATOMIC   },
    { "drop",     TK_KW_DROP     },
    { "guard",    TK_KW_GUARD    },
    { "unless",   TK_KW_UNLESS   },
    { "require",  TK_KW_REQUIRE  },
    { "ensure",   TK_KW_ENSURE   },
    { "where",    TK_KW_WHERE    },
    { "shine",    TK_KW_SHINE    },
    { "enum",     TK_KW_ENUM     },
    { "use",      TK_KW_USE      },
    { "export",   TK_KW_EXPORT   },
    { "pub",      TK_KW_PUB      },
    { "as",       TK_KW_AS       },
    { "mut",      TK_KW_MUT      },
    { "ref",      TK_KW_REF      },
    { "asm",      TK_KW_ASM      },
    { NULL, 0 }
};

static int lookup_kw(const char *s, int len)
{
    for (int i = 0; KWS[i].w; i++) {
        if (streq_n(s, len, KWS[i].w)) return KWS[i].tk;
    }
    return TK_IDENT;
}

static void push_tok(int kind, int file, int line, int col,
                     const char *s, int len, long long iv)
{
    if (g_ntoks >= MAX_TOKENS) {
        fprintf(stderr, "luna-boot: too many tokens\n"); exit(1);
    }
    Tok *t = &g_toks[g_ntoks++];
    t->kind = kind; t->file = file; t->line = line; t->col = col;
    t->str = s; t->len = len; t->int_val = iv;
}

/* Does `tk` trail a line that wants to continue onto the next?
   We're conservative — only clearly-binary operators that cannot also
   legally begin a new statement qualify.  Tokens like `->`, `:`, `,`
   are excluded: `->` ends a fn signature; `:` ends a type annotation;
   `,` only wraps inside parens (already handled by bracket_depth).   */
static int tok_wants_continuation(int tk)
{
    switch (tk) {
        case TK_PLUS: case TK_MINUS:
        case TK_STAR: case TK_SLASH: case TK_PERCENT:
        case TK_ANDAND: case TK_OROR:
        case TK_AMP:  case TK_PIPE:  case TK_CARET:
        case TK_SHL:  case TK_SHR:
        case TK_KW_AND: case TK_KW_OR:
            return 1;
    }
    return 0;
}

static void lex_file(int file_idx)
{
    CUnit *u = &g_files[file_idx];
    const char *p = u->src;
    const char *end = u->src + u->src_len;
    int line = 1, col = 1;

    /* ---- Indent tracking (Luna-style off-side rule) ---------------------
       When brackets are NOT open, a newline followed by deeper indentation
       emits a synthetic TK_LBRACE; shallower indentation emits one TK_RBRACE
       per popped level.  This lets the existing brace-style parser handle
       real Luna source without any parser changes.
       Lines ending with a binary operator ("... +\n    more") are treated
       as physically wrapped — the newline there does NOT start a new
       statement.                                                           */
    int indent_stack[64];
    int indent_depth = 0;
    indent_stack[0] = 0;
    int bracket_depth = 0;   /* () [] {} nesting — indent disabled inside */
    int at_line_start = 1;

    u->tok_start = g_ntoks;

    while (p < end) {
        int c = (unsigned char)*p;

        /* -------- Handle line-start indentation (off-side rule) -------- */
        if (at_line_start && bracket_depth == 0) {
            /* Count leading whitespace */
            int indent = 0;
            const char *ls = p;
            while (ls < end && (*ls == ' ' || *ls == '\t')) {
                indent += (*ls == '\t') ? 4 : 1;   /* tab = 4 cols */
                ls++;
            }
            /* Blank or comment-only line → don't disturb indent state */
            if (ls >= end || *ls == '\n' || *ls == '#'
                || (*ls == '/' && ls+1 < end && ls[1] == '/')) {
                /* fall through and let the main loop skip whitespace */
            } else {
                int top = indent_stack[indent_depth];
                if (indent > top) {
                    /* Deeper — push a new level and emit a synthetic { */
                    if (indent_depth + 1 < 64) {
                        indent_depth++;
                        indent_stack[indent_depth] = indent;
                    }
                    push_tok(TK_LBRACE, file_idx, line, 1, p, 0, 0);
                } else if (indent < top) {
                    /* Shallower — pop stack and emit one } per level */
                    while (indent_depth > 0 && indent_stack[indent_depth] > indent) {
                        push_tok(TK_RBRACE, file_idx, line, 1, p, 0, 0);
                        indent_depth--;
                    }
                }
                /* equal → no brace emitted; statement continuation */
            }
            at_line_start = 0;
            /* Skip the leading whitespace we just counted */
            p = ls;
            col = (int)(ls - (p - (ls - p)));  /* approximate; kept for diagnostics */
            if (p >= end) break;
            continue;
        }

        /* whitespace */
        if (c == ' ' || c == '\t' || c == '\r') { p++; col++; continue; }
        /* Line continuation: `\` followed immediately by a newline joins the
           next line onto the current one.  Some Luna modules use this for
           wrapping long boolean expressions.                               */
        if (c == '\\' && p+1 < end && (p[1] == '\n' || p[1] == '\r')) {
            p++;  /* skip the backslash */
            if (p < end && *p == '\r') p++;
            if (p < end && *p == '\n') { p++; line++; col = 1; }
            /* DO NOT set at_line_start — the logical line continues. */
            continue;
        }
        if (c == '\n') {
            p++; line++; col = 1;
            /* Indent tracking is suppressed inside brackets — a newline in
               the middle of `fn foo(@a,\n              @b) -> int` must not
               emit a synthetic `{` on the next iteration when the line
               wraps back out of the parens.  Only mark at_line_start when
               the newline actually starts a NEW statement (depth 0).     */
            if (bracket_depth == 0) {
                /* Also: if the previous token wants more input (trailing
                   binary operator), the newline is a physical wrap — don't
                   start a new logical line. */
                int last = (g_ntoks > 0) ? g_toks[g_ntoks - 1].kind : -1;
                if (!tok_wants_continuation(last)) {
                    at_line_start = 1;
                }
            }
            continue;
        }

        /* line comment */
        if (c == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        /* // line comment */
        if (c == '/' && p+1 < end && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        /* /* block comment */
        if (c == '/' && p+1 < end && p[1] == '*') {
            p += 2;
            while (p+1 < end && !(p[0]=='*' && p[1]=='/')) {
                if (*p == '\n') { line++; col = 1; }
                p++;
            }
            if (p+1 < end) p += 2;
            continue;
        }

        /* @identifier */
        if (c == '@' && p+1 < end && is_id_start((unsigned char)p[1])) {
            const char *s = p+1;
            p += 2;
            while (p < end && is_id_cont((unsigned char)*p)) p++;
            push_tok(TK_ATIDENT, file_idx, line, col, s, (int)(p - s), 0);
            col += (int)(p - (s - 1));
            continue;
        }

        /* identifier or keyword */
        if (is_id_start(c)) {
            const char *s = p;
            while (p < end && is_id_cont((unsigned char)*p)) p++;
            int len = (int)(p - s);
            int kw = lookup_kw(s, len);
            push_tok(kw, file_idx, line, col, s, len, 0);
            col += len;
            continue;
        }

        /* number */
        if (is_digit(c)) {
            const char *s = p;
            long long v = 0;
            int base = 10;
            if (c == '0' && p+1 < end && (p[1]=='x' || p[1]=='X')) {
                base = 16;
                p += 2;
                while (p < end) {
                    int d = (unsigned char)*p;
                    int dv;
                    if (d >= '0' && d <= '9') dv = d - '0';
                    else if (d >= 'a' && d <= 'f') dv = 10 + d - 'a';
                    else if (d >= 'A' && d <= 'F') dv = 10 + d - 'A';
                    else if (d == '_') { p++; continue; }
                    else break;
                    v = v * 16 + dv;
                    p++;
                }
            } else if (c == '0' && p+1 < end && (p[1]=='o' || p[1]=='O')) {
                /* Octal literal: 0o755 */
                base = 8;
                p += 2;
                while (p < end) {
                    int d = (unsigned char)*p;
                    if (d == '_') { p++; continue; }
                    if (d < '0' || d > '7') break;
                    v = v * 8 + (d - '0');
                    p++;
                }
            } else if (c == '0' && p+1 < end && (p[1]=='b' || p[1]=='B')) {
                /* Binary literal: 0b1010_0101 */
                base = 2;
                p += 2;
                while (p < end) {
                    int d = (unsigned char)*p;
                    if (d == '_') { p++; continue; }
                    if (d != '0' && d != '1') break;
                    v = v * 2 + (d - '0');
                    p++;
                }
            } else {
                while (p < end && (is_digit((unsigned char)*p) || *p=='_')) {
                    if (*p != '_') v = v * (long long)base + (*p - '0');
                    p++;
                }
                /* Float literal tolerance (bootstrap subset).  We can't codegen
                   floats, but source like `float_value: 0.0` appears in structs
                   for zero-initialisation.  Accept `N.M` and `N.M[eE]S` and keep
                   the integer part `v`.  This is lossy but only hurts programs
                   that actually do fractional arithmetic — which the bootstrap
                   isn't meant to compile anyway.                              */
                if (p < end && *p == '.' && p+1 < end && p[1] == '.') {
                    /* integer followed by range operator — leave alone */
                } else if (p < end && (*p == '.' || *p == 'e' || *p == 'E')) {
                    if (*p == '.') {
                        p++;  /* consume '.' */
                        while (p < end && (is_digit((unsigned char)*p) || *p == '_')) p++;
                    }
                    if (p < end && (*p == 'e' || *p == 'E')) {
                        p++;
                        if (p < end && (*p == '+' || *p == '-')) p++;
                        while (p < end && is_digit((unsigned char)*p)) p++;
                    }
                    /* v holds the integer part; that's what downstream code gets */
                }
            }
            push_tok(TK_INT, file_idx, line, col, s, (int)(p - s), v);
            col += (int)(p - s);
            continue;
        }

        /* Character literal `'x'` or `'\n'` — lowered to TK_INT with the
           Unicode code-point value (ASCII range only for the bootstrap). */
        if (c == '\'') {
            p++; col++;
            long long cv = 0;
            if (p < end && *p == '\\' && p+1 < end) {
                char e = p[1];
                if (e == 'n')       cv = '\n';
                else if (e == 't')  cv = '\t';
                else if (e == 'r')  cv = '\r';
                else if (e == '0')  cv = 0;
                else if (e == '\\') cv = '\\';
                else if (e == '\'') cv = '\'';
                else if (e == '"')  cv = '"';
                else                cv = (unsigned char)e;
                p += 2; col += 2;
            } else if (p < end) {
                cv = (unsigned char)*p;
                p++; col++;
            }
            if (p < end && *p == '\'') { p++; col++; }
            push_tok(TK_INT, file_idx, line, col, NULL, 0, cv);
            continue;
        }

        /* string literal (double quoted) */
        if (c == '"') {
            p++; col++;
            const char *s = p;
            /* Scan once to find the maximum decoded length, then copy while
               decoding.  We put the result in the arena so downstream tokens
               can point straight at it.                                     */
            const char *scan = p;
            int raw_len = 0;
            while (scan < end && *scan != '"') {
                if (*scan == '\\' && scan+1 < end) { scan += 2; raw_len++; }
                else { scan++; raw_len++; }
            }
            char *out = (char *)arena_alloc((size_t)raw_len + 1);
            int olen = 0;
            while (p < end && *p != '"') {
                if (*p == '\\' && p+1 < end) {
                    char e = p[1];
                    char r = e;
                    if (e == 'n')  r = '\n';
                    else if (e=='t') r = '\t';
                    else if (e=='r') r = '\r';
                    else if (e=='0') r = '\0';
                    else if (e=='\\') r = '\\';
                    else if (e=='"')  r = '"';
                    else if (e=='\'') r = '\'';
                    out[olen++] = r;
                    p += 2;
                } else {
                    if (*p == '\n') { line++; col = 1; }
                    out[olen++] = *p++;
                }
            }
            if (p < end) p++;  /* consume closing " */
            out[olen] = 0;
            push_tok(TK_STR, file_idx, line, col, out, olen, 0);
            col += (int)(p - s) + 2;
            continue;
        }

        /* multi-char punctuation */
        if (c == '-' && p+1 < end && p[1] == '>') { push_tok(TK_ARROW, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '=' && p+1 < end && p[1] == '=') { push_tok(TK_EQ, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '=' && p+1 < end && p[1] == '>') { push_tok(TK_FAT_ARROW, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '!' && p+1 < end && p[1] == '=') { push_tok(TK_NE, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '<' && p+1 < end && p[1] == '=') { push_tok(TK_LE, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '>' && p+1 < end && p[1] == '=') { push_tok(TK_GE, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '&' && p+1 < end && p[1] == '&') { push_tok(TK_ANDAND, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '|' && p+1 < end && p[1] == '|') { push_tok(TK_OROR, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '.' && p+1 < end && p[1] == '.') { push_tok(TK_DOTDOT, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '<' && p+1 < end && p[1] == '<') { push_tok(TK_SHL, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }
        if (c == '>' && p+1 < end && p[1] == '>') { push_tok(TK_SHR, file_idx, line, col, p, 2, 0); p += 2; col += 2; continue; }

        /* single-char punctuation */
        switch (c) {
            case '(': push_tok(TK_LPAREN, file_idx, line, col, p, 1, 0); bracket_depth++; p++; col++; continue;
            case ')': push_tok(TK_RPAREN, file_idx, line, col, p, 1, 0); if (bracket_depth>0) bracket_depth--; p++; col++; continue;
            case '{': push_tok(TK_LBRACE, file_idx, line, col, p, 1, 0); bracket_depth++; p++; col++; continue;
            case '}': push_tok(TK_RBRACE, file_idx, line, col, p, 1, 0); if (bracket_depth>0) bracket_depth--; p++; col++; continue;
            case '[': push_tok(TK_LBRACK, file_idx, line, col, p, 1, 0); bracket_depth++; p++; col++; continue;
            case ']': push_tok(TK_RBRACK, file_idx, line, col, p, 1, 0); if (bracket_depth>0) bracket_depth--; p++; col++; continue;
            case ',': push_tok(TK_COMMA,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case ';': push_tok(TK_SEMI,   file_idx, line, col, p, 1, 0); p++; col++; continue;
            case ':': push_tok(TK_COLON,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '.': push_tok(TK_DOT,    file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '=': push_tok(TK_ASSIGN, file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '<': push_tok(TK_LT,     file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '>': push_tok(TK_GT,     file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '+': push_tok(TK_PLUS,   file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '-': push_tok(TK_MINUS,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '*': push_tok(TK_STAR,   file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '/': push_tok(TK_SLASH,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '%': push_tok(TK_PERCENT,file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '?': push_tok(TK_QMARK,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '!': push_tok(TK_BANG,   file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '&': push_tok(TK_AMP,    file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '|': push_tok(TK_PIPE,   file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '^': push_tok(TK_CARET,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '~': push_tok(TK_TILDE,  file_idx, line, col, p, 1, 0); p++; col++; continue;
            case '@': push_tok(TK_AT,     file_idx, line, col, p, 1, 0); p++; col++; continue;
        }

        fprintf(stderr, "luna-boot: lex error: unexpected char '%c' (0x%02x) at %s:%d:%d\n",
                (c >= 32 && c < 127) ? c : '?', c, u->path, line, col);
        exit(1);
    }

    /* At EOF, close every remaining indent level with synthetic '}' so the
       outermost block the parser opened via an indent-INDENT is balanced. */
    while (indent_depth > 0) {
        push_tok(TK_RBRACE, file_idx, line, 1, p, 0, 0);
        indent_depth--;
    }

    u->tok_end = g_ntoks;
}

/* ========================================================================= */
/* 5. AST + PARSER                                                            */
/* ========================================================================= */

static int  g_tpos;            /* global parse cursor                         */
static int  g_tend;            /* end of current file's token range           */
static int  g_cur_file;

static Tok *tok_at(int i) { return &g_toks[i]; }
static Tok *peek(void)    { return &g_toks[g_tpos]; }
static Tok *peek_n(int n) { return &g_toks[g_tpos + n]; }

/* Forward declarations for helpers defined later. */
static int ffi_register(const char *name, int len);
static int ffi_register_linux(const char *name, int len);

/* Human-readable name for a token kind — turns "kind=72" into "','". */
static const char *tok_name(int k)
{
    switch (k) {
        case TK_EOF:     return "end of file";
        case TK_IDENT:   return "identifier";
        case TK_ATIDENT: return "@identifier";
        case TK_INT:     return "integer literal";
        case TK_STR:     return "string literal";
        case TK_LPAREN:  return "'('";
        case TK_RPAREN:  return "')'";
        case TK_LBRACE:  return "'{'";
        case TK_RBRACE:  return "'}'";
        case TK_LBRACK:  return "'['";
        case TK_RBRACK:  return "']'";
        case TK_COMMA:   return "','";
        case TK_SEMI:    return "';'";
        case TK_COLON:   return "':'";
        case TK_DOT:     return "'.'";
        case TK_DOTDOT:  return "'..'";
        case TK_ARROW:   return "'->'";
        case TK_FAT_ARROW: return "'=>'";
        case TK_ASSIGN:  return "'='";
        case TK_EQ:      return "'=='";
        case TK_NE:      return "'!='";
        case TK_LT:      return "'<'";
        case TK_LE:      return "'<='";
        case TK_GT:      return "'>'";
        case TK_GE:      return "'>='";
        case TK_PLUS:    return "'+'";
        case TK_MINUS:   return "'-'";
        case TK_STAR:    return "'*'";
        case TK_SLASH:   return "'/'";
        case TK_PERCENT: return "'%'";
        case TK_ANDAND:  return "'&&'";
        case TK_OROR:    return "'||'";
        case TK_AMP:     return "'&'";
        case TK_PIPE:    return "'|'";
        case TK_CARET:   return "'^'";
        case TK_TILDE:   return "'~'";
        case TK_BANG:    return "'!'";
        case TK_SHL:     return "'<<'";
        case TK_SHR:     return "'>>'";
        case TK_AT:      return "'@'";
        case TK_KW_FN:      return "'fn'";
        case TK_KW_LET:     return "'let'";
        case TK_KW_CONST:   return "'const'";
        case TK_KW_MEOW:    return "'meow'";
        case TK_KW_RETURN:  return "'return'";
        case TK_KW_IF:      return "'if'";
        case TK_KW_ELSE:    return "'else'";
        case TK_KW_ECLIPSE: return "'eclipse'";
        case TK_KW_WHILE:   return "'while'";
        case TK_KW_ORBIT:   return "'orbit'";
        case TK_KW_IN:      return "'in'";
        case TK_KW_BREAK:   return "'break'";
        case TK_KW_CONTINUE:return "'continue'";
        case TK_KW_STRUCT:  return "'struct'";
        case TK_KW_EXTERN:  return "'extern'";
        case TK_KW_IMPORT:  return "'import'";
        case TK_KW_SHINE:   return "'shine'";
        case TK_KW_NOVA:    return "'nova'";
        case TK_KW_SEAL:    return "'seal'";
        case TK_KW_PHASE:   return "'phase'";
        case TK_KW_MATCH:   return "'match'";
        case TK_KW_GUARD:   return "'guard'";
        default:            return "unknown token";
    }
}

/* Print `file:line: cause` followed by the source line itself with a
 * caret pointing at the column.  Helps turn a raw "kind=72" into
 * something a human can act on.                                    */
static void diag_tok(const char *kind_label, const char *msg, Tok *t)
{
    const char *path = "?";
    const char *src  = NULL;
    int src_len = 0;
    if (t && t->file >= 0 && t->file < g_nfiles) {
        path    = g_files[t->file].path;
        src     = g_files[t->file].src;
        src_len = g_files[t->file].src_len;
    }
    int ln = t ? t->line : 0;
    fprintf(stderr, "luna-boot: %s: %s:%d: %s\n", kind_label, path, ln, msg);
    if (src) {
        int cur_line = 1;
        const char *line_start = src;
        for (const char *p = src; p < src + src_len && cur_line < ln; p++) {
            if (*p == '\n') { cur_line++; line_start = p + 1; }
        }
        const char *line_end = line_start;
        while (line_end < src + src_len && *line_end != '\n') line_end++;
        fprintf(stderr, "   | ");
        for (const char *p = line_start; p < line_end; p++) {
            if (*p == '\t') fputc(' ', stderr);
            else            fputc(*p, stderr);
        }
        fputc('\n', stderr);
    }
}

static int accept(int k)
{
    if (g_tpos < g_tend && g_toks[g_tpos].kind == k) { g_tpos++; return 1; }
    return 0;
}

static Tok *expect(int k, const char *what)
{
    if (g_tpos >= g_tend || g_toks[g_tpos].kind != k) {
        Tok *got = (g_tpos < g_tend) ? &g_toks[g_tpos] : NULL;
        char msg[256];
        snprintf(msg, sizeof(msg), "expected %s, but saw %s",
                 what, got ? tok_name(got->kind) : "end of file");
        diag_tok("parse error", msg, got);
        exit(1);
    }
    return &g_toks[g_tpos++];
}

static int node_new(int kind, Tok *anchor)
{
    if (g_nnodes >= MAX_NODES) { fprintf(stderr, "luna-boot: too many AST nodes\n"); exit(1); }
    int id = g_nnodes++;
    AstNode *n = &g_nodes[id];
    n->kind = kind;
    n->file = anchor ? anchor->file : g_cur_file;
    n->line = anchor ? anchor->line : 0;
    n->nkids = 0;
    n->str_idx = -1;
    return id;
}

static void node_add(int parent, int child)
{
    AstNode *p = &g_nodes[parent];
    if (p->nkids < KIDS_INLINE) { p->kids[p->nkids++] = child; return; }
    fprintf(stderr, "luna-boot: ast node too many children (limit %d) "
            "kind=%d file=%s line=%d\n",
            KIDS_INLINE, p->kind,
            (p->file >= 0 && p->file < g_nfiles) ? g_files[p->file].path : "?",
            p->line);
    exit(1);
}

/* Forward */
static int parse_expr(void);
static int parse_type(void);
static int parse_block_seq(void);
static int parse_stmt(void);
static Tok *expect_ident_or_kw(const char *what);

/* Context flag: struct literals are disabled inside control-flow conditions
   so `if SomeType { ... }` is parsed as "if expr, then block" rather than
   "if (SomeType{...})".  Set/restored around parse_expr calls in if/while.  */
static int g_no_struct_lit = 0;

/* Types: int, str, bool, *int, [int; N] */
/* Parse a type.  Forms supported:
     T                  — bare type name or custom name (may be keyword-aliased)
     &T  / &mut T       — reference (tolerant: skipped to inner name)
     *T / *const T      — raw pointer
     [T; N]             — fixed-size array (element type can be another type)
     [T]                — dynamic slice (treat like fixed size 0)
     fn(T, U) -> R      — function type (skip arg list and return type)
     T<U, V>            — generic application (skip < ... >)
     T?                 — nullable suffix (skip)
   The bootstrap represents most of these as opaque N_TYPE nodes — struct
   field / param lookup only cares about NAME, not full layout.          */
static int parse_type(void)
{
    Tok *a = peek();
    int t = node_new(N_TYPE, a);
    AstNode *n = &g_nodes[t];

    /* Reference: `&T` or `&mut T` — consume and fall into element parse */
    if (accept(TK_AMP)) {
        accept(TK_KW_MUT);
        (void)parse_type();
        n->s = "ref"; n->slen = 3; n->i1 = 3;
        return t;
    }
    if (accept(TK_STAR)) {
        /* `*T`, `*const T`, `*mut T` */
        accept(TK_KW_CONST);
        accept(TK_KW_MUT);
        /* Pointee can itself be a type; we keep the element name only. */
        if (peek()->kind == TK_IDENT ||
            (peek()->kind >= TK_KW_FN && peek()->kind <= TK_KW_ASM)) {
            Tok *nm = expect_ident_or_kw("type name");
            n->s = nm->str; n->slen = nm->len;
        } else {
            (void)parse_type();
            n->s = "ptr"; n->slen = 3;
        }
        n->i1 = 1;
        return t;
    }
    if (accept(TK_LBRACK)) {
        /* [T; N] or [T] */
        int inner = parse_type();     /* recursive — supports [Foo<Bar>; N] */
        AstNode *innerN = &g_nodes[inner];
        n->s = innerN->s; n->slen = innerN->slen;
        n->i1 = 2;
        if (accept(TK_SEMI)) {
            /* size may be an int literal OR a const identifier — accept both */
            Tok *sz = peek();
            if (sz->kind == TK_INT) { n->iv = sz->int_val; g_tpos++; }
            else if (sz->kind == TK_IDENT ||
                     (sz->kind >= TK_KW_FN && sz->kind <= TK_KW_ASM)) {
                g_tpos++;
                n->iv = 0;   /* unknown at bootstrap time */
            } else {
                expect(TK_INT, "array size (int or ident)");
            }
        } else {
            n->iv = 0;   /* slice form */
        }
        expect(TK_RBRACK, "']'");
        return t;
    }
    if (accept(TK_KW_FN)) {
        /* fn(T, U) -> R — consume shape */
        if (accept(TK_LPAREN)) {
            while (peek()->kind != TK_RPAREN && peek()->kind != TK_EOF) {
                (void)parse_type();
                if (!accept(TK_COMMA)) break;
            }
            expect(TK_RPAREN, "')' in fn type");
        }
        if (accept(TK_ARROW)) {
            (void)parse_type();
        }
        n->s = "fn"; n->slen = 2; n->i1 = 4;
        return t;
    }
    /* Bare type name: identifier or keyword-as-name */
    Tok *nm = expect_ident_or_kw("type name");
    n->s = nm->str; n->slen = nm->len;
    n->i1 = 0;
    /* Generic args `<T, U>` — skip */
    if (accept(TK_LT)) {
        int depth = 1;
        while (depth > 0 && peek()->kind != TK_EOF) {
            if (peek()->kind == TK_LT) depth++;
            else if (peek()->kind == TK_GT) depth--;
            if (depth > 0) g_tpos++;
            else g_tpos++;  /* consume closing */
        }
    }
    /* Subscript-style type args `array[T, N]` or `Map[K, V]` — consume */
    if (accept(TK_LBRACK)) {
        int depth = 1;
        while (depth > 0 && peek()->kind != TK_EOF) {
            if (peek()->kind == TK_LBRACK) depth++;
            else if (peek()->kind == TK_RBRACK) depth--;
            if (depth > 0) g_tpos++;
            else g_tpos++;  /* consume ']' */
        }
    }
    /* Nullable suffix `?` — skip */
    (void)accept(TK_QMARK);
    return t;
}

/* Primary: literals, @ident, (...), ident (, for builtins used as expr) */
static int parse_primary(void)
{
    Tok *t = peek();
    if (t->kind == TK_INT) {
        g_tpos++;
        int id = node_new(N_INT, t);
        g_nodes[id].iv = t->int_val;
        return id;
    }
    if (t->kind == TK_STR) {
        g_tpos++;
        int id = node_new(N_STR, t);
        g_nodes[id].s = t->str;
        g_nodes[id].slen = t->len;
        g_nodes[id].str_idx = strpool_add(t->str, t->len);
        return id;
    }
    if (t->kind == TK_KW_TRUE || t->kind == TK_KW_FALSE) {
        g_tpos++;
        int id = node_new(N_BOOL, t);
        g_nodes[id].i1 = (t->kind == TK_KW_TRUE) ? 1 : 0;
        return id;
    }
    if (t->kind == TK_KW_NIL) {
        g_tpos++;
        return node_new(N_NILV, t);
    }
    /* Lambda comes FIRST — `fn(` must parse as a lambda expression, not as
       a keyword-named function call.                                      */
    if (t->kind == TK_KW_FN && peek_n(1)->kind == TK_LPAREN) {
        g_tpos++;                             /* fn */
        expect(TK_LPAREN, "'(' in lambda");
        while (peek()->kind != TK_RPAREN && peek()->kind != TK_EOF) {
            if (accept(TK_KW_MUT) || accept(TK_KW_REF) || accept(TK_KW_MOVE)) { }
            if (peek()->kind == TK_ATIDENT || peek()->kind == TK_IDENT) g_tpos++;
            if (accept(TK_COLON)) (void)parse_type();
            if (!accept(TK_COMMA)) break;
        }
        expect(TK_RPAREN, "')' in lambda");
        if (accept(TK_ARROW)) {
            int k = peek()->kind;
            if (k == TK_LBRACE) {
                (void)parse_block_seq();
            } else if (k == TK_KW_RETURN || k == TK_KW_LET ||
                       k == TK_KW_MEOW   || k == TK_KW_SEAL ||
                       k == TK_KW_IF     || k == TK_KW_WHILE ||
                       k == TK_KW_ORBIT  || k == TK_KW_MATCH ||
                       k == TK_KW_PHASE  || k == TK_KW_SPAWN ||
                       k == TK_KW_DEFER) {
                (void)parse_stmt();
            } else {
                (void)parse_expr();
            }
        } else if (peek()->kind == TK_LBRACE) {
            (void)parse_block_seq();
        }
        int z = node_new(N_INT, t);
        g_nodes[z].iv = 0;
        return z;
    }

    /* Identifier OR keyword-used-as-a-name.  Keywords can collide with
       stdlib function names (`send`, `recv`, `match`/`phase` were keywords
       already handled; the rest go here).  If a keyword is followed by `(`
       it's a function call — treat it as an identifier.                   */
    int is_kw_name = (t->kind >= TK_KW_FN && t->kind <= TK_KW_ASM) &&
                     (peek_n(1)->kind == TK_LPAREN);
    if (t->kind == TK_ATIDENT || t->kind == TK_IDENT || is_kw_name) {
        g_tpos++;
        int id = node_new(N_IDENT, t);
        g_nodes[id].s = t->str;
        g_nodes[id].slen = t->len;
        /* Struct literal: `TypeName { field: value, ... }`.  Only recognized
           when struct-literal is allowed in the surrounding context — control
           flow conditions (`if X { ... }`, `while X { ... }`) set the flag
           so the `{` there opens a block, not a struct.                    */
        if (t->kind == TK_IDENT && !g_no_struct_lit && peek()->kind == TK_LBRACE) {
            Tok *lb = peek();
            g_tpos++;
            int sl = node_new(N_STRUCT_LIT, lb);
            g_nodes[sl].s = t->str;
            g_nodes[sl].slen = t->len;
            while (peek()->kind != TK_RBRACE && peek()->kind != TK_EOF) {
                Tok *ft = peek();
                int fi = node_new(N_FIELD_INIT, ft);
                Tok *fn = expect_ident_or_kw("struct-literal field name");
                g_nodes[fi].s = fn->str;
                g_nodes[fi].slen = fn->len;
                expect(TK_COLON, "':' in struct literal");
                int val = parse_expr();
                node_add(fi, val);
                node_add(sl, fi);
                if (!accept(TK_COMMA)) break;
            }
            expect(TK_RBRACE, "'}' closing struct literal");
            return sl;
        }
        return id;
    }
    if (t->kind == TK_LPAREN) {
        g_tpos++;
        /* Empty unit `()` — commonly used as `return ok(())` in Luna.
           Treat as the integer 0 for the bootstrap. */
        if (accept(TK_RPAREN)) {
            int z = node_new(N_INT, t);
            g_nodes[z].iv = 0;
            return z;
        }
        int e = parse_expr();
        /* Tuple literal `(a, b, ...)` — bootstrap evaluates the first element
           and discards the rest; the self-hosted compiler handles tuples
           proper.  Real projects rarely use tuples outside stdlib collections. */
        if (peek()->kind == TK_COMMA) {
            while (accept(TK_COMMA)) {
                if (peek()->kind == TK_RPAREN) break;
                (void)parse_expr();
            }
        }
        expect(TK_RPAREN, "')'");
        int g = node_new(N_GROUP, t);
        node_add(g, e);
        return g;
    }
    if (t->kind == TK_LBRACK) {
        /* Array literal, two forms:
             [v1, v2, v3]     — comma-separated elements
             [value; count]   — repeated `value` `count` times
           Both are evaluated for side-effects and yield 0 in rax (the
           bootstrap doesn't materialise arrays; self-hosted compiler does). */
        g_tpos++;
        int al = node_new(N_ARRAY_LIT, t);
        g_nodes[al].i1 = 0;          /* 0 = list form [a, b, c] */
        if (peek()->kind != TK_RBRACK) {
            int first = parse_expr();
            node_add(al, first);
            if (accept(TK_SEMI)) {
                int cnt = parse_expr();
                node_add(al, cnt);
                g_nodes[al].i1 = 1;  /* 1 = repeat form [v; N] */
            } else {
                /* Cap stored elements to avoid overflowing the AST node's
                   child list — very large literals (X.509 trust store has
                   ~400 integers) don't need to be kept in AST; the
                   bootstrap never materialises them.                    */
                while (accept(TK_COMMA)) {
                    if (peek()->kind == TK_RBRACK) break;
                    int e = parse_expr();
                    if (g_nodes[al].nkids < KIDS_INLINE - 4) {
                        node_add(al, e);
                    }
                }
            }
        }
        expect(TK_RBRACK, "']' closing array literal");
        return al;
    }
    if (t->kind == TK_MINUS) {
        /* unary negation -> 0 - x */
        g_tpos++;
        int e = parse_primary();
        int z = node_new(N_INT, t);
        g_nodes[z].iv = 0;
        int b = node_new(N_BIN, t);
        g_nodes[b].i1 = TK_MINUS;
        node_add(b, z);
        node_add(b, e);
        return b;
    }
    if (t->kind == TK_KW_NOT || t->kind == TK_BANG) {
        g_tpos++;
        int e = parse_primary();
        int u = node_new(N_UNARY, t);
        g_nodes[u].i1 = TK_KW_NOT;
        node_add(u, e);
        return u;
    }
    if (t->kind == TK_AMP) {
        /* Unary address-of `&EXPR` — bootstrap just evaluates the inner
           expression and passes its value through.  Used in stdlib
           atomics wrappers: `llvm_atomic_load_i64(&@atom.value, ord)`. */
        g_tpos++;
        int e = parse_primary();
        int u = node_new(N_UNARY, t);
        g_nodes[u].i1 = TK_AMP;
        node_add(u, e);
        return u;
    }
    if (t->kind == TK_STAR) {
        /* Unary dereference `*EXPR` — ditto, inner value passed through. */
        g_tpos++;
        int e = parse_primary();
        int u = node_new(N_UNARY, t);
        g_nodes[u].i1 = TK_STAR;
        node_add(u, e);
        return u;
    }
    if (t->kind == TK_TILDE) {
        /* Bitwise NOT: lower as `x XOR -1` at emit time */
        g_tpos++;
        int e = parse_primary();
        int u = node_new(N_UNARY, t);
        g_nodes[u].i1 = TK_TILDE;
        node_add(u, e);
        return u;
    }
    if (t->kind == TK_KW_IF) {
        /* `if COND then A else B` — ternary expression form.
           Supports multi-line form where the condition is followed by a
           newline → synthetic `{` → `then A \n else B \n }` (indent block). */
        g_tpos++;
        int saved = g_no_struct_lit; g_no_struct_lit = 1;
        int cond = parse_expr();
        g_no_struct_lit = saved;
        /* Peek past an optional synthetic-brace block that the indent
           tokeniser may have inserted between `cond` and `then`.        */
        int had_block = 0;
        if (peek()->kind == TK_LBRACE && peek_n(1)->kind == TK_KW_THEN) {
            g_tpos++;          /* eat `{` */
            had_block = 1;
        }
        if (peek()->kind == TK_KW_THEN) {
            g_tpos++;
            int thn = parse_expr();
            expect(TK_KW_ELSE, "'else' in ternary if");
            int els = parse_expr();
            if (had_block) {
                /* Close the synthetic block that wrapped the then/else. */
                accept(TK_RBRACE);
            }
            int n = node_new(N_IF, t);
            g_nodes[n].i1 = 1;          /* marker: expression form */
            node_add(n, cond);
            node_add(n, thn);
            node_add(n, els);
            return n;
        }
        /* Unusual: `if` at expr position without `then`. Fall back: treat
           the condition value as the expression result.                   */
        return cond;
    }
    /* Cosmic-keyword unary expressions: `nova EXPR`, `move EXPR`, `freeze EXPR`,
       `box EXPR`, `rc EXPR`, `weak EXPR`, `drop EXPR`.  Bootstrap evaluates the
       inner expression for side-effects and returns its value unchanged.
       Exception: if the keyword is immediately followed by `(`, it is a
       function call of that name (e.g. `shine("hello")`) — fall through to
       the identifier-as-call path so the call is lowered properly.        */
    if ((t->kind == TK_KW_NOVA || t->kind == TK_KW_MOVE ||
        t->kind == TK_KW_FREEZE || t->kind == TK_KW_BOX ||
        t->kind == TK_KW_RC || t->kind == TK_KW_WEAK ||
        t->kind == TK_KW_DROP || t->kind == TK_KW_SHINE)
        && peek_n(1)->kind != TK_LPAREN) {
        g_tpos++;
        return parse_primary();
    }
    if (t->kind == TK_KW_SELF) {
        /* `self` used inside impl methods — treat as a bare identifier */
        g_tpos++;
        int id = node_new(N_IDENT, t);
        g_nodes[id].s = t->str;
        g_nodes[id].slen = t->len;
        return id;
    }
    /* (Lambda handled earlier in parse_primary, above the ident/kw branch.) */

    /* Bare anonymous struct literal `{ field: value, ... }` — used in
       `return { ciphertext: @c, tag: @t }`.  Only recognised when struct
       literals are permitted in context (not inside `if`/`while` tests). */
    if (t->kind == TK_LBRACE && !g_no_struct_lit) {
        int k1 = peek_n(1)->kind;
        int k2 = peek_n(2)->kind;
        int looks_like_struct =
            (k2 == TK_COLON) &&
            (k1 == TK_IDENT ||
             (k1 >= TK_KW_FN && k1 <= TK_KW_ASM));
        if (looks_like_struct) {
            Tok *lb = t;
            g_tpos++;                           /* eat `{` */
            int sl = node_new(N_STRUCT_LIT, lb);
            g_nodes[sl].s = "";
            g_nodes[sl].slen = 0;
            while (peek()->kind != TK_RBRACE && peek()->kind != TK_EOF) {
                Tok *ft = peek();
                int fi = node_new(N_FIELD_INIT, ft);
                Tok *fn = expect_ident_or_kw("struct-literal field name");
                g_nodes[fi].s = fn->str;
                g_nodes[fi].slen = fn->len;
                expect(TK_COLON, "':' in struct literal");
                int val = parse_expr();
                node_add(fi, val);
                node_add(sl, fi);
                if (!accept(TK_COMMA)) break;
            }
            expect(TK_RBRACE, "'}' closing struct literal");
            return sl;
        }
    }

    {
        char msg[128];
        snprintf(msg, sizeof(msg), "unexpected %s at start of expression",
                 tok_name(t->kind));
        diag_tok("parse error", msg, t);
    }
    exit(1);
}

/* Postfix: call, field access, postfix ? */
static int parse_postfix(void)
{
    int e = parse_primary();
    for (;;) {
        Tok *t = peek();
        if (t->kind == TK_LPAREN) {
            g_tpos++;
            int c = node_new(N_CALL, t);
            node_add(c, e);
            if (!accept(TK_RPAREN)) {
                for (;;) {
                    int a = parse_expr();
                    node_add(c, a);
                    if (accept(TK_COMMA)) continue;
                    expect(TK_RPAREN, "')'");
                    break;
                }
            }
            e = c;
            continue;
        }
        if (t->kind == TK_DOT) {
            g_tpos++;
            /* Field name may be any identifier OR a keyword (e.g. `.phase`,
               `.type`, `.send` — all collide with Luna keywords). */
            Tok *nm = expect_ident_or_kw("field name");
            int f = node_new(N_FIELD_ACCESS, t);
            node_add(f, e);
            g_nodes[f].s = nm->str; g_nodes[f].slen = nm->len;
            e = f;
            continue;
        }
        /* Namespace access `Module::member` — two colons.  The bootstrap
           doesn't model modules; treat the right-hand name as the effective
           identifier (discard the left namespace).                         */
        if (t->kind == TK_COLON && peek_n(1)->kind == TK_COLON) {
            g_tpos += 2;
            Tok *nm = expect_ident_or_kw("name after '::'");
            int id = node_new(N_IDENT, nm);
            g_nodes[id].s = nm->str;
            g_nodes[id].slen = nm->len;
            e = id;
            continue;
        }
        if (t->kind == TK_QMARK) {
            g_tpos++;
            int q = node_new(N_POSTFIX_Q, t);
            node_add(q, e);
            e = q;
            continue;
        }
        if (t->kind == TK_LBRACK) {
            /* Array indexing: `base[index]`.  Bootstrap evaluates both for
               side-effects and returns 0 (indexing into bootstrap-produced
               binaries isn't supported at runtime — the self-hosted compiler
               does the real memory math).                                   */
            g_tpos++;
            int idx = parse_expr();
            expect(TK_RBRACK, "']' in index");
            int ix = node_new(N_INDEX, t);
            node_add(ix, e);
            node_add(ix, idx);
            e = ix;
            continue;
        }
        break;
    }
    return e;
}

/* Pratt-ish precedence; keep it simple and left-associative.
   Ordering matches common C / Rust expectations so callers can rely on
   `a | b & c` meaning `a | (b & c)`.                                   */
static int prec_of(int k)
{
    switch (k) {
        case TK_OROR: case TK_KW_OR:   return 1;
        case TK_ANDAND: case TK_KW_AND:return 2;
        case TK_PIPE:                  return 3;   /* bitwise | */
        case TK_CARET:                 return 4;   /* bitwise ^ */
        case TK_AMP:                   return 5;   /* bitwise & */
        case TK_EQ: case TK_NE:        return 6;
        case TK_LT: case TK_LE:
        case TK_GT: case TK_GE:        return 7;
        case TK_SHL: case TK_SHR:      return 8;   /* << >> */
        case TK_PLUS: case TK_MINUS:   return 9;
        case TK_STAR: case TK_SLASH:
        case TK_PERCENT:               return 10;
        default: return 0;
    }
}

static int parse_bin(int min_prec)
{
    int lhs = parse_postfix();
    for (;;) {
        Tok *t = peek();
        int p = prec_of(t->kind);
        if (p < min_prec || p == 0) break;
        int op = t->kind;
        g_tpos++;
        int rhs = parse_bin(p + 1);
        int b = node_new(N_BIN, t);
        g_nodes[b].i1 = op;
        node_add(b, lhs);
        node_add(b, rhs);
        lhs = b;
    }
    return lhs;
}

static int parse_expr(void) { return parse_bin(1); }

/* A block is a sequence of statements wrapped in braces.  The AST node for
   a block stores up to 8 children inline; if more statements appear we
   cascade via a nested N_BLOCK in the last slot.  The lowerer walks blocks
   recursively, so the nesting is fully transparent.                         */
static int parse_block_seq(void)
{
    /* Tolerate a missing `{` — empty-body indent blocks (e.g. `orbit @i in
       range(0, 0)\n    # comment-only body`) never emit a synthetic LBRACE,
       so the body is syntactically absent.  Return an empty N_BLOCK. */
    if (peek()->kind != TK_LBRACE) {
        Tok *hdr = peek();
        return node_new(N_BLOCK, hdr);
    }
    Tok *lb = expect(TK_LBRACE, "'{'");
    int outer = node_new(N_BLOCK, lb);
    int cur = outer;
    while (peek()->kind != TK_RBRACE && peek()->kind != TK_EOF) {
        /* Allow `;` as optional statement separator: lexer emits it only
           when the source uses explicit `;`. Skip any run of them.       */
        while (accept(TK_SEMI)) { /* skip */ }
        if (peek()->kind == TK_RBRACE) break;
        int s = parse_stmt();
        /* After each statement, eat a trailing `;` the same way. */
        while (accept(TK_SEMI)) { /* skip */ }
        if (s < 0) continue;
        /* If we're about to hit 8 children and there's still content to
           come, reserve the last slot for an overflow block and switch
           the cursor there.                                               */
        if (g_nodes[cur].nkids == 7 && peek()->kind != TK_RBRACE) {
            node_add(cur, s);
            int nxt = node_new(N_BLOCK, lb);
            node_add(cur, nxt);  /* 8th slot holds the overflow block */
            cur = nxt;
        } else {
            if (g_nodes[cur].nkids >= 8) {
                fprintf(stderr, "luna-boot: internal: block-chain overflow at %s:%d\n",
                        g_files[g_cur_file].path, lb->line);
                exit(1);
            }
            node_add(cur, s);
        }
    }
    expect(TK_RBRACE, "'}'");
    return outer;
}

static int parse_stmt(void)
{
    Tok *t = peek();

    if (t->kind == TK_KW_PASS) {
        g_tpos++;
        return node_new(N_PASS, t);
    }
    if (t->kind == TK_KW_BREAK) {
        g_tpos++;
        return node_new(N_BREAK, t);
    }
    if (t->kind == TK_KW_CONTINUE) {
        g_tpos++;
        return node_new(N_CONTINUE, t);
    }
    if (t->kind == TK_KW_RETURN) {
        g_tpos++;
        int r = node_new(N_RETURN, t);
        /* A bare `return` should not greedily consume tokens on the NEXT
           logical line — important for `guard COND else return\n@next = ...`
           where `@next` belongs to the NEXT statement, not this return value. */
        if (peek()->kind != TK_RBRACE && peek()->kind != TK_SEMI &&
            peek()->line == t->line) {
            int v = parse_expr();
            node_add(r, v);
            /* Tuple return `return @a, @b` — evaluate the rest for
               side-effects but only the first value is returned in rax.  */
            while (accept(TK_COMMA)) {
                if (peek()->line != t->line) break;
                (void)parse_expr();
            }
        }
        return r;
    }
    if (t->kind == TK_KW_LET || t->kind == TK_KW_MEOW || t->kind == TK_KW_SEAL) {
        /* `let`/`meow`/`seal` — all three bind a local; bootstrap treats them
           identically because it has no mutability enforcement.
           Additionally, bare `meow` on its own (no `@name`) is used in some
           stdlib files as a marker statement — treat as pass.              */
        g_tpos++;
        /* Bare `meow` — no name, no value.  Treat as pass. */
        if (t->kind == TK_KW_MEOW) {
            int k = peek()->kind;
            if (k != TK_ATIDENT && k != TK_IDENT && k != TK_KW_MUT) {
                /* Check: is this on the same line?  If the next token is on
                   a new line, the `meow` really is standalone. */
                if (peek()->line != t->line) {
                    return node_new(N_PASS, t);
                }
            }
        }
        int l = node_new(N_LET, t);
        /* Optional `mut` modifier after `let` */
        if (accept(TK_KW_MUT)) { /* consumed */ }
        Tok *nm = peek();
        if (nm->kind == TK_ATIDENT || nm->kind == TK_IDENT) {
            g_tpos++;
        } else {
            nm = expect(TK_ATIDENT, "binding name (@x)");
        }
        g_nodes[l].s = nm->str; g_nodes[l].slen = nm->len;
        if (accept(TK_COLON)) {
            int ty = parse_type();
            node_add(l, ty);
        } else {
            /* placeholder TYPE node (untyped `let`) */
            int ty = node_new(N_TYPE, t);
            g_nodes[ty].s = "int"; g_nodes[ty].slen = 3;
            node_add(l, ty);
        }
        if (accept(TK_ASSIGN)) {
            int v = parse_expr();
            node_add(l, v);
        } else {
            /* No initialiser — synthesise `0` so lower_stmt is happy. */
            int z = node_new(N_INT, t);
            g_nodes[z].iv = 0;
            node_add(l, z);
        }
        return l;
    }
    if (t->kind == TK_KW_IF) {
        g_tpos++;
        int outer = node_new(N_IF, t);
        /* Disable struct literals inside the condition so `if Type { ... }`
           parses as "if Type, then body" and not as "if (Type{...})".      */
        int saved = g_no_struct_lit; g_no_struct_lit = 1;
        int cond = parse_expr();
        g_no_struct_lit = saved;
        node_add(outer, cond);
        int body = parse_block_seq();
        node_add(outer, body);
        /* Chain of `eclipse` clauses becomes a right-leaning else-if tree.
           `eclipse` without a condition acts as a bare `else`.             */
        int tail = outer;
        while (peek()->kind == TK_KW_ECLIPSE) {
            g_tpos++;
            /* Bare `eclipse` followed directly by the body means a plain
               else-clause (no condition).                                  */
            if (peek()->kind == TK_LBRACE) {
                int el = parse_block_seq();
                node_add(tail, el);
                break;
            }
            /* `eclipse if` is rewritten as `eclipse <cond> { ... }` by the
               real Luna grammar; our bootstrap keeps the same shape. */
            if (peek()->kind == TK_KW_IF) g_tpos++;   /* skip optional `if` */
            int es = g_no_struct_lit; g_no_struct_lit = 1;
            int e_cond = parse_expr();
            g_no_struct_lit = es;
            int e_body = parse_block_seq();
            int e_if = node_new(N_IF, t);
            node_add(e_if, e_cond);
            node_add(e_if, e_body);
            node_add(tail, e_if);   /* nested in prev's else slot */
            tail = e_if;
        }
        if (peek()->kind == TK_KW_ELSE) {
            g_tpos++;
            /* `else if` — continue the chain like `eclipse if` */
            if (peek()->kind == TK_KW_IF) {
                g_tpos++;
                int es = g_no_struct_lit; g_no_struct_lit = 1;
                int e_cond = parse_expr();
                g_no_struct_lit = es;
                int e_body = parse_block_seq();
                int e_if = node_new(N_IF, t);
                node_add(e_if, e_cond);
                node_add(e_if, e_body);
                node_add(tail, e_if);
                tail = e_if;
                /* more `else`s may follow recursively */
                while (peek()->kind == TK_KW_ELSE) {
                    g_tpos++;
                    if (peek()->kind == TK_KW_IF) {
                        g_tpos++;
                        int saved2 = g_no_struct_lit; g_no_struct_lit = 1;
                        int ec2 = parse_expr();
                        g_no_struct_lit = saved2;
                        int eb2 = parse_block_seq();
                        int e2 = node_new(N_IF, t);
                        node_add(e2, ec2);
                        node_add(e2, eb2);
                        node_add(tail, e2);
                        tail = e2;
                    } else {
                        int el = parse_block_seq();
                        node_add(tail, el);
                        break;
                    }
                }
            } else {
                int el = parse_block_seq();
                node_add(tail, el);
            }
        }
        return outer;
    }
    if (t->kind == TK_KW_WHILE) {
        g_tpos++;
        int w = node_new(N_WHILE, t);
        int saved = g_no_struct_lit; g_no_struct_lit = 1;
        int cond = parse_expr();
        g_no_struct_lit = saved;
        int body = parse_block_seq();
        node_add(w, cond);
        node_add(w, body);
        return w;
    }
    if (t->kind == TK_KW_ORBIT) {
        g_tpos++;
        int o = node_new(N_ORBIT, t);
        /* `orbit @i in lo..hi` — the iterator variable may be `_` in Luna
           (an anonymous counter), but the bootstrap lexer emits TK_IDENT
           for bare `_` (it's id-start). Accept either TK_ATIDENT or TK_IDENT. */
        Tok *iv = peek();
        if (iv->kind != TK_ATIDENT && iv->kind != TK_IDENT) {
            iv = expect(TK_ATIDENT, "loop variable");
        } else {
            g_tpos++;
        }
        g_nodes[o].s = iv->str; g_nodes[o].slen = iv->len;
        expect(TK_KW_IN, "'in'");
        int saved = g_no_struct_lit; g_no_struct_lit = 1;
        int lo = parse_expr();
        /* Two shapes:
             orbit @i in lo..hi              — explicit range
             orbit @i in iterable            — any iterable (e.g. range(0, n),
                                                a list, etc.). The bootstrap
                                                can't iterate arbitrary values
                                                so it treats the iterable as
                                                `lo` and synthesises `hi = 0`.  */
        int hi;
        if (accept(TK_DOTDOT)) {
            hi = parse_expr();
        } else {
            hi = node_new(N_INT, t);
            g_nodes[hi].iv = 0;
        }
        g_no_struct_lit = saved;
        int body = parse_block_seq();
        node_add(o, lo);
        node_add(o, hi);
        node_add(o, body);
        return o;
    }
    if (t->kind == TK_KW_UNSAFE) {
        g_tpos++;
        int u = node_new(N_UNSAFE_BLOCK, t);
        int b = parse_block_seq();
        node_add(u, b);
        return u;
    }
    if (t->kind == TK_KW_MATCH || t->kind == TK_KW_PHASE) {
        /* Parse match/phase arms into a real AST.  Supported patterns
         * are limited — int literal, wildcard '_', and bare identifier
         * binding — but the full syntax (including ADT patterns like
         * Bright(@x)) parses tolerantly so unsupported arms become
         * "always match, body returns opaque".                         */
        g_tpos++;
        int saved = g_no_struct_lit; g_no_struct_lit = 1;
        int scrut = parse_expr();
        g_no_struct_lit = saved;
        expect(TK_LBRACE, "'{' starting match arms");

        int m = node_new(N_MATCH, t);
        node_add(m, scrut);

        while (peek()->kind != TK_RBRACE && peek()->kind != TK_EOF) {
            /* Parse the pattern loosely — the concrete shape is recorded
             * as an N_INT, N_IDENT or N_PASS; anything more elaborate is
             * read as a tolerant expression and treated as "wildcard".
             * Accept optional `is` prefix (alternative cosmic spelling
             * seen in stdlib/veil.luna).                              */
            if (peek()->kind == TK_IDENT && peek()->len == 2 &&
                peek()->str[0] == 'i' && peek()->str[1] == 's') {
                g_tpos++;
            }
            Tok *pt = peek();
            int pat = -1;
            if (pt->kind == TK_INT) {
                g_tpos++;
                pat = node_new(N_INT, pt);
                g_nodes[pat].iv = pt->int_val;
            } else if (pt->kind == TK_IDENT && pt->len == 1 && pt->str[0] == '_') {
                g_tpos++;
                pat = node_new(N_PASS, pt);
            } else if (pt->kind == TK_ATIDENT || pt->kind == TK_IDENT) {
                /* Bare @name or name — bind (treated as wildcard for now). */
                g_tpos++;
                pat = node_new(N_IDENT, pt);
                g_nodes[pat].s = pt->str;
                g_nodes[pat].slen = pt->len;
                /* Tolerate constructor-style patterns: @name(...)        */
                if (peek()->kind == TK_LPAREN) {
                    int depth = 0;
                    do {
                        if (peek()->kind == TK_LPAREN) depth++;
                        else if (peek()->kind == TK_RPAREN) depth--;
                        g_tpos++;
                    } while (depth > 0 && peek()->kind != TK_EOF);
                }
            } else {
                /* Fallback: consume until '=>' or newline, produce wildcard. */
                while (peek()->kind != TK_FAT_ARROW &&
                       peek()->kind != TK_RBRACE &&
                       peek()->kind != TK_EOF &&
                       peek()->line == pt->line) {
                    g_tpos++;
                }
                pat = node_new(N_PASS, pt);
            }

            /* Optional `if guard` — bootstrap ignores the guard. */
            if (peek()->kind == TK_KW_IF) {
                g_tpos++;
                int sv = g_no_struct_lit; g_no_struct_lit = 1;
                (void)parse_expr();
                g_no_struct_lit = sv;
            }

            /* Separator: accept `=>` or `->`.  Both are used in the wild. */
            if (!accept(TK_FAT_ARROW)) {
                (void)accept(TK_ARROW);
            }

            /* Parse body.  Four shapes seen in real Luna:
             *    PAT => expr                         (single expr)
             *    PAT => return expr                  (statement)
             *    PAT => shine("...")                 (call stmt)
             *    PAT <newline>                       (indent-opened
             *        block                            block body)      */
            int body;
            if (peek()->kind == TK_LBRACE) {
                body = parse_block_seq();
            } else {
                body = parse_stmt();
            }

            int arm = node_new(N_MATCH_ARM, pt);
            node_add(arm, pat);
            node_add(arm, body);
            node_add(m, arm);

            /* Arms are separated implicitly by newlines or commas.       */
            accept(TK_COMMA);
            accept(TK_SEMI);
        }
        expect(TK_RBRACE, "'}' closing match");
        return m;
    }
    if (t->kind == TK_KW_ACTOR || t->kind == TK_KW_FLOW) {
        /* actor/flow NAME { ... } — tolerant skip.  Bootstrap doesn't emit
           real message-passing code, so we just consume the block.        */
        g_tpos++;
        if (peek()->kind == TK_IDENT) g_tpos++;             /* name */
        /* skip optional generic <T> — we don't have it but tolerate */
        if (accept(TK_LBRACE)) {
            int depth = 1;
            while (depth > 0 && peek()->kind != TK_EOF) {
                if (peek()->kind == TK_LBRACE) depth++;
                else if (peek()->kind == TK_RBRACE) depth--;
                if (depth > 0) g_tpos++;
            }
            expect(TK_RBRACE, "actor/flow close");
        }
        return node_new(N_PASS, t);
    }
    if ((t->kind == TK_KW_SPAWN || t->kind == TK_KW_SEND  ||
        t->kind == TK_KW_DEFER || t->kind == TK_KW_NOVA  ||
        t->kind == TK_KW_DROP  || t->kind == TK_KW_SHINE ||
        t->kind == TK_KW_ATOMIC)
        && peek_n(1)->kind != TK_LPAREN) {
        /* `spawn EXPR`, `send T, M`, `defer EXPR`, `nova EXPR`, `drop @v`,
           `shine EXPR`, `atomic { BLOCK }`, AND bare `nova` / `shine` on
           their own — tolerant parse.
           If followed by `(`, fall through: `shine(x)` / `nova(x)` are
           actual function calls and must be lowered as such.             */
        g_tpos++;
        int next_k = peek()->kind;
        if (next_k == TK_LBRACE) {
            int depth = 0;
            do {
                if (peek()->kind == TK_LBRACE) depth++;
                else if (peek()->kind == TK_RBRACE) depth--;
                g_tpos++;
            } while (depth > 0 && peek()->kind != TK_EOF);
        } else if (next_k == TK_RBRACE || next_k == TK_EOF ||
                   next_k == TK_SEMI) {
            /* Bare `nova` / `shine` as a yield statement — no operand. */
        } else {
            (void)parse_expr();
            while (accept(TK_COMMA)) (void)parse_expr();
        }
        return node_new(N_PASS, t);
    }
    if (t->kind == TK_KW_GUARD) {
        /* Several shapes:
             guard COND else BLOCK       — canonical
             guard COND else STMT        — inline single-stmt else
             guard COND <INDENT BLOCK>   — indent-only block (no `else`)
             guard COND, "message"       — inline assertion with message  */
        g_tpos++;
        int saved = g_no_struct_lit; g_no_struct_lit = 1;
        (void)parse_expr();
        g_no_struct_lit = saved;
        /* Optional `, msg, ...` trailing assertion-style arguments. */
        while (accept(TK_COMMA)) {
            if (peek()->line != t->line) break;
            (void)parse_expr();
        }
        /* Accept `else` OR cosmic-synonym `eclipse` as the guard
           fall-through clause — both are idiomatic in Luna stdlib. */
        if (accept(TK_KW_ELSE) || accept(TK_KW_ECLIPSE)) {
            if (peek()->kind == TK_LBRACE) {
                (void)parse_block_seq();
            } else {
                (void)parse_stmt();
            }
        } else if (peek()->kind == TK_LBRACE) {
            /* No explicit else — just a block follows (indent-based).  Skip it. */
            (void)parse_block_seq();
        }
        return node_new(N_PASS, t);
    }
    if (t->kind == TK_KW_UNLESS) {
        /* `unless COND BLOCK` — evaluate cond + skip body */
        g_tpos++;
        (void)parse_expr();
        if (peek()->kind == TK_LBRACE) (void)parse_block_seq();
        return node_new(N_PASS, t);
    }
    if (t->kind == TK_KW_REQUIRE || t->kind == TK_KW_ENSURE) {
        /* contracts — evaluate expression, no codegen */
        g_tpos++;
        (void)parse_expr();
        return node_new(N_PASS, t);
    }

    /* Implicit-let shortcut: `@name: type = value` — real Luna often omits
       the `let` keyword when the binding has a type annotation.            */
    if (t->kind == TK_ATIDENT && peek_n(1)->kind == TK_COLON) {
        /* Peek at whether this is actually a let-with-type (next-after-:
           must be a type name or `[`) — we don't know for sure but this
           pattern is dominant enough that mispredictions are rare.        */
        Tok *nm = peek();
        g_tpos++;             /* consume @name */
        g_tpos++;             /* consume ':'    */
        int l = node_new(N_LET, t);
        g_nodes[l].s = nm->str; g_nodes[l].slen = nm->len;
        int ty = parse_type();
        node_add(l, ty);
        if (accept(TK_ASSIGN)) {
            int v = parse_expr();
            node_add(l, v);
        } else {
            int z = node_new(N_INT, t);
            g_nodes[z].iv = 0;
            node_add(l, z);
        }
        return l;
    }

    /* Tuple destructuring `@a, @b, ... = EXPR` — bootstrap binds the first
       target to the expression value and discards the rest.                */
    if (t->kind == TK_ATIDENT && peek_n(1)->kind == TK_COMMA) {
        /* Scan forward to confirm this is actually a destructuring — must
           see only `@x` / `_` separated by commas, terminating at `=`.     */
        int look = 0;
        int is_destr = 1;
        for (;;) {
            int k = peek_n(look)->kind;
            if (k != TK_ATIDENT && k != TK_IDENT) { is_destr = 0; break; }
            look++;
            if (peek_n(look)->kind == TK_ASSIGN) break;
            if (peek_n(look)->kind != TK_COMMA)   { is_destr = 0; break; }
            look++;
        }
        if (is_destr) {
            Tok *nm = peek();
            /* Skip all target names + commas */
            while (peek()->kind != TK_ASSIGN && peek()->kind != TK_EOF) {
                g_tpos++;
            }
            expect(TK_ASSIGN, "'=' in tuple destructuring");
            int v = parse_expr();
            int l = node_new(N_LET, t);
            g_nodes[l].s = nm->str; g_nodes[l].slen = nm->len;
            int ty = node_new(N_TYPE, t);
            g_nodes[ty].s = "int"; g_nodes[ty].slen = 3;
            node_add(l, ty);
            node_add(l, v);
            return l;
        }
    }

    /* assignment or bare expression statement */
    int e = parse_expr();
    if (accept(TK_ASSIGN)) {
        int rhs = parse_expr();
        int a = node_new(N_ASSIGN, t);
        node_add(a, e);
        node_add(a, rhs);
        return a;
    }
    int es = node_new(N_EXPR_STMT, t);
    node_add(es, e);
    return es;
}

/* Top-level declarations. */
static int parse_param(void)
{
    Tok *t = peek();
    int p = node_new(N_PARAM, t);
    /* Accept `@name`, `name`, `mut @name`, `ref @name`, `move @name` */
    if (accept(TK_KW_MUT) || accept(TK_KW_REF) || accept(TK_KW_MOVE)) {
        /* modifier consumed */
    }
    Tok *nm = peek();
    if (nm->kind == TK_ATIDENT || nm->kind == TK_IDENT) {
        g_tpos++;
    } else if (nm->kind == TK_KW_SELF) {
        g_tpos++;
    } else {
        nm = expect(TK_ATIDENT, "param name");
    }
    g_nodes[p].s = nm->str; g_nodes[p].slen = nm->len;
    /* Type annotation is optional — if missing, treat as `int` */
    if (!accept(TK_COLON)) {
        int ty = node_new(N_TYPE, t);
        g_nodes[ty].s = "int"; g_nodes[ty].slen = 3;
        node_add(p, ty);
        return p;
    }
    int ty = parse_type();
    node_add(p, ty);
    return p;
}

static int parse_fn_decl(int is_extern, const char *abi, int abi_len)
{
    Tok *t = expect(TK_KW_FN, "'fn'");
    int fnid = node_new(is_extern ? N_EXTERN_FN : N_FN, t);
    g_nodes[fnid].i1 = is_extern;

    /* Function name — accept any identifier or keyword (for stdlib externs
       whose OS symbol names collide with Luna keywords: send/recv/etc). */
    Tok *nm = expect_ident_or_kw("function name");
    g_nodes[fnid].s = nm->str; g_nodes[fnid].slen = nm->len;

    /* stash abi in i2 = abi-kind (1 = linux_syscall, 2 = C, 0 = default) */
    if (is_extern && abi) {
        if (abi_len == 13 && memcmp(abi, "linux_syscall", 13) == 0) g_nodes[fnid].i2 = 1;
        else if (abi_len == 1 && memcmp(abi, "C", 1) == 0) g_nodes[fnid].i2 = 2;
        else g_nodes[fnid].i2 = 3;
    }

    expect(TK_LPAREN, "'('");
    if (!accept(TK_RPAREN)) {
        for (;;) {
            int p = parse_param();
            node_add(fnid, p);
            if (accept(TK_COMMA)) continue;
            expect(TK_RPAREN, "')'");
            break;
        }
    }
    /* return type (optional) */
    if (accept(TK_ARROW)) {
        int rt = parse_type();
        node_add(fnid, rt);
        /* Tuple return `-> T1, T2, ...` — bootstrap ignores the extra
           components and just uses the first type.                      */
        while (accept(TK_COMMA)) {
            (void)parse_type();
        }
    } else {
        /* synthesise void-ish by adding a TYPE node with s="int" */
        int rt = node_new(N_TYPE, t);
        g_nodes[rt].s = "int"; g_nodes[rt].slen = 3;
        node_add(fnid, rt);
    }

    if (!is_extern) {
        int body = parse_block_seq();
        node_add(fnid, body);
    } else if (peek()->kind == TK_LBRACE) {
        /* `extern "C" fn NAME(...) -> TY` with an actual body — parse the
           body and promote the node to a real N_FN so it gets compiled.
           Used for `#[no_mangle] extern "C" fn` that defines a C-callable
           implementation in Luna itself.                                  */
        int body = parse_block_seq();
        node_add(fnid, body);
        g_nodes[fnid].kind = N_FN;
        g_nodes[fnid].i1 = 0;
    }
    return fnid;
}

/* Struct field names may collide with Luna keywords (e.g. a struct with a
   `phase: int` field, or `type: str`).  The bootstrap accepts any keyword
   as a field name — the keyword meaning is context-dependent and only
   matters at statement-keyword positions.                                  */
static Tok *expect_ident_or_kw(const char *what)
{
    Tok *t = peek();
    if (t->kind == TK_IDENT ||
        (t->kind >= TK_KW_FN && t->kind <= TK_KW_ASM)) {
        g_tpos++;
        return t;
    }
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected %s, but saw %s",
                 what, tok_name(t->kind));
        diag_tok("parse error", msg, t);
    }
    exit(1);
}

static int parse_struct_decl(void)
{
    Tok *t = expect(TK_KW_STRUCT, "'struct'");
    int s = node_new(N_STRUCT, t);
    Tok *nm = expect_ident_or_kw("struct name");
    g_nodes[s].s = nm->str; g_nodes[s].slen = nm->len;
    expect(TK_LBRACE, "'{'");
    while (peek()->kind != TK_RBRACE && peek()->kind != TK_EOF) {
        Tok *ft = peek();
        int f = node_new(N_FIELD, ft);
        Tok *fn = expect_ident_or_kw("field name");
        g_nodes[f].s = fn->str; g_nodes[f].slen = fn->len;
        expect(TK_COLON, "':' in field");
        int ty = parse_type();
        node_add(f, ty);
        node_add(s, f);
        accept(TK_COMMA);
    }
    expect(TK_RBRACE, "'}'");
    return s;
}

static int parse_const_decl(void)
{
    Tok *t = expect(TK_KW_CONST, "'const'");
    int c = node_new(N_CONST, t);
    Tok *nm = expect(TK_IDENT, "const name");
    g_nodes[c].s = nm->str; g_nodes[c].slen = nm->len;
    expect(TK_COLON, "':' in const");
    int ty = parse_type();
    node_add(c, ty);
    expect(TK_ASSIGN, "'='");
    int v = parse_expr();
    node_add(c, v);
    return c;
}

static int parse_meow_decl(void)
{
    /* module-level meow is a mutable global — we treat as const for codegen */
    Tok *t = expect(TK_KW_MEOW, "'meow'");
    int c = node_new(N_CONST, t);
    Tok *nm = expect(TK_ATIDENT, "global name");
    g_nodes[c].s = nm->str; g_nodes[c].slen = nm->len;
    if (accept(TK_COLON)) {
        int ty = parse_type();
        node_add(c, ty);
    } else {
        int rt = node_new(N_TYPE, t);
        g_nodes[rt].s = "int"; g_nodes[rt].slen = 3;
        node_add(c, rt);
    }
    expect(TK_ASSIGN, "'='");
    int v = parse_expr();
    node_add(c, v);
    return c;
}

static int parse_extern_decl(void)
{
    Tok *t = expect(TK_KW_EXTERN, "'extern'");
    const char *abi = NULL;
    int abi_len = 0;
    if (peek()->kind == TK_STR) {
        Tok *a = &g_toks[g_tpos++];
        abi = a->str; abi_len = a->len;
    }
    if (peek()->kind == TK_KW_FN) {
        return parse_fn_decl(1, abi, abi_len);
    }
    die_unsup("extern form", g_files[g_cur_file].path, t->line);
    return -1;
}

static int parse_import_decl(void)
{
    Tok *t = expect(TK_KW_IMPORT, "'import'");
    int im = node_new(N_IMPORT, t);
    Tok *nm = expect(TK_IDENT, "module name");
    g_nodes[im].s = nm->str; g_nodes[im].slen = nm->len;
    return im;
}

/* Parse one module (one file). */
static int parse_file(int file_idx)
{
    CUnit *u = &g_files[file_idx];
    g_cur_file = file_idx;
    g_tpos = u->tok_start;
    g_tend = u->tok_end;

    int root = node_new(N_MODULE, NULL);
    g_nodes[root].file = file_idx;

    /* Build a chain of modules so we aren't capped at 8 decls. */
    int cur = root;
    while (g_tpos < g_tend && g_toks[g_tpos].kind != TK_EOF) {
        Tok *t = &g_toks[g_tpos];
        int d = -1;
        if      (t->kind == TK_KW_IMPORT) d = parse_import_decl();
        else if (t->kind == TK_KW_USE)    { g_tpos++; if (peek()->kind==TK_IDENT) g_tpos++; d = node_new(N_PASS, t); }
        else if (t->kind == TK_KW_PUB)    { g_tpos++; continue; /* pub just visibility */ }
        else if (t->kind == TK_KW_FN)     d = parse_fn_decl(0, NULL, 0);
        else if (t->kind == TK_KW_EXTERN) d = parse_extern_decl();
        else if (t->kind == TK_KW_CONST)  d = parse_const_decl();
        else if (t->kind == TK_KW_MEOW)   d = parse_meow_decl();
        else if (t->kind == TK_KW_SEAL)   d = parse_meow_decl();   /* seal = meow + immutable */
        else if (t->kind == TK_KW_STRUCT) d = parse_struct_decl();
        /* Tolerant skip for constructs the bootstrap can't lower but must
           see often enough to parse through them without errors. */
        else if (t->kind == TK_KW_ENUM || t->kind == TK_KW_TRAIT ||
                 t->kind == TK_KW_IMPL || t->kind == TK_KW_TYPE ||
                 t->kind == TK_KW_ACTOR || t->kind == TK_KW_FLOW) {
            g_tpos++;
            /* consume name */
            if (peek()->kind == TK_IDENT ||
                (peek()->kind >= TK_KW_FN && peek()->kind <= TK_KW_ASM)) g_tpos++;
            /* skip optional generic-style `<...>` */
            if (peek()->kind == TK_LT) {
                int depth = 1; g_tpos++;
                while (depth > 0 && peek()->kind != TK_EOF) {
                    if (peek()->kind == TK_LT) depth++;
                    else if (peek()->kind == TK_GT) depth--;
                    g_tpos++;
                }
            }
            /* skip optional `for TypeName` (for trait impl) */
            if (peek()->kind == TK_KW_FOR) { g_tpos++; (void)parse_type(); }
            /* skip optional where-clause */
            while (peek()->kind == TK_KW_WHERE || peek()->kind == TK_COMMA ||
                   peek()->kind == TK_IDENT || peek()->kind == TK_COLON ||
                   peek()->kind == TK_PLUS) {
                /* naive consume until '{' */
                if (peek()->kind == TK_LBRACE) break;
                g_tpos++;
            }
            /* skip body `{ ... }` */
            if (peek()->kind == TK_LBRACE) {
                int depth = 0;
                do {
                    if (peek()->kind == TK_LBRACE) depth++;
                    else if (peek()->kind == TK_RBRACE) depth--;
                    g_tpos++;
                } while (depth > 0 && peek()->kind != TK_EOF);
            } else if (peek()->kind == TK_ASSIGN) {
                /* `type X = Y` alias form — skip */
                g_tpos++;
                (void)parse_type();
            }
            d = node_new(N_PASS, t);
        }
        else if (t->kind == TK_KW_EXPORT) {
            /* `export { name1, name2, ... }` or `export name` — just skip */
            g_tpos++;
            if (peek()->kind == TK_LBRACE) {
                int depth = 0;
                do {
                    if (peek()->kind == TK_LBRACE) depth++;
                    else if (peek()->kind == TK_RBRACE) depth--;
                    g_tpos++;
                } while (depth > 0 && peek()->kind != TK_EOF);
            } else {
                while (peek()->kind == TK_IDENT || peek()->kind == TK_COMMA) g_tpos++;
            }
            d = node_new(N_PASS, t);
        }
        else if (t->kind == TK_ATIDENT && peek_n(1)->kind == TK_COLON) {
            /* Module-level implicit meow with type:
                   @last_tremor_bytes: array[int, 256]
                   @counter: int = 0
               Parse as a N_CONST / N_PASS node; the initializer is
               evaluated for side-effects only.                         */
            Tok *nm = peek();
            g_tpos += 2;            /* consume @name and ':' */
            (void)parse_type();
            if (accept(TK_ASSIGN)) (void)parse_expr();
            /* register a synthetic pass — we don't model module-level
               mutable globals in the bootstrap. */
            d = node_new(N_PASS, nm);
        }
        else if (t->kind == TK_IDENT || t->kind == TK_ATIDENT) {
            /* Top-level expression statement, typically a call like
               `run_self_tests()` that fires as a module initializer.  We
               parse it for side-effects and stash it as an N_PASS since
               the bootstrap doesn't execute module initialisers.       */
            (void)parse_expr();
            /* optional assignment at module scope: `@foo = bar` */
            if (accept(TK_ASSIGN)) (void)parse_expr();
            d = node_new(N_PASS, t);
        }
        else {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "unexpected %s at top level (expected import, fn, struct, const, ...)",
                     tok_name(t->kind));
            diag_tok("parse error", msg, t);
            exit(1);
        }
        if (g_nodes[cur].nkids == 7) {
            /* reserve last slot for a continuation N_MODULE */
            node_add(cur, d);
            int next = node_new(N_MODULE, NULL);
            node_add(cur, next);
            cur = next;
        } else {
            node_add(cur, d);
        }
    }

    u->node_root = root;
    return root;
}

/* =========================================================================
 * Module file loading (handle `import X` recursively).
 * ========================================================================= */

static int file_already_loaded(const char *path)
{
    for (int i = 0; i < g_nfiles; i++) {
        if (strcmp(g_files[i].path, path) == 0) return i;
    }
    return -1;
}

static char *try_resolve_module(const char *name, int name_len)
{
    for (int i = 0; i < g_ninclude; i++) {
        char *full = (char *)arena_alloc(1024);
        int n = snprintf(full, 1024, "%s/%.*s.luna", g_include_paths[i], name_len, name);
        if (n <= 0 || n >= 1024) continue;
        FILE *f = fopen(full, "rb");
        if (f) { fclose(f); return full; }
    }
    return NULL;
}

static int load_file(const char *path)
{
    int existing = file_already_loaded(path);
    if (existing >= 0) return existing;
    if (g_nfiles >= MAX_FILES) { fprintf(stderr, "luna-boot: too many files\n"); exit(1); }
    int idx = g_nfiles++;
    g_files[idx].path = arena_strndup(path, (int)strlen(path));
    int len;
    char *buf = slurp_file(path, &len);
    if (!buf) {
        fprintf(stderr, "luna-boot: cannot read '%s'\n", path);
        exit(1);
    }
    g_files[idx].src = buf;
    g_files[idx].src_len = len;
    lex_file(idx);
    parse_file(idx);
    return idx;
}

/* Recursively resolve imports referenced by file `idx`. */
static void resolve_imports(int idx)
{
    int root = g_files[idx].node_root;
    /* Walk a chain of N_MODULE blocks. */
    int cur = root;
    while (cur >= 0) {
        AstNode *m = &g_nodes[cur];
        int next = -1;
        for (int i = 0; i < m->nkids; i++) {
            int c = m->kids[i];
            AstNode *cn = &g_nodes[c];
            if (cn->kind == N_IMPORT) {
                char *resolved = try_resolve_module(cn->s, cn->slen);
                if (!resolved) {
                    /* Missing imports are a soft error: we warn, then skip.
                       This lets us bootstrap when stdlib modules aren't
                       relevant to the tiny subset.                       */
                    if (g_verbose) {
                        fprintf(stderr, "luna-boot: note: import '%.*s' not resolved, skipping\n",
                                cn->slen, cn->s);
                    }
                } else {
                    int already = file_already_loaded(resolved);
                    if (already < 0) {
                        int sub = load_file(resolved);
                        resolve_imports(sub);
                    }
                }
            } else if (cn->kind == N_MODULE) {
                next = c;
            }
        }
        cur = next;
    }
}

/* ========================================================================= */
/* 6. NAME RESOLUTION / LIGHT TYPE PASS                                       */
/* ========================================================================= */

static int sym_find(const char *name, int name_len)
{
    for (int i = 0; i < g_nsyms; i++) {
        if (streq_nn(g_syms[i].name, g_syms[i].name_len, name, name_len)) return i;
    }
    return -1;
}

static int sym_add(int kind, const char *name, int name_len, int node)
{
    if (g_nsyms >= MAX_SYMS) { fprintf(stderr, "luna-boot: too many symbols\n"); exit(1); }
    int id = g_nsyms++;
    g_syms[id].kind = kind;
    g_syms[id].name = name;
    g_syms[id].name_len = name_len;
    g_syms[id].node = node;
    g_syms[id].code_off = -1;
    g_syms[id].arity = 0;
    g_syms[id].syscall_nr = -1;
    return id;
}

static int syscall_number_for(const char *name, int len)
{
    if (streq_n(name, len, "sys_read"))        return SYS_READ;
    if (streq_n(name, len, "sys_write"))       return SYS_WRITE;
    if (streq_n(name, len, "sys_open"))        return SYS_OPEN;
    if (streq_n(name, len, "sys_close"))       return SYS_CLOSE;
    if (streq_n(name, len, "sys_exit"))        return SYS_EXIT;
    if (streq_n(name, len, "sys_exit_group")) return SYS_EXIT_GROUP;
    if (streq_n(name, len, "sys_nanosleep"))  return SYS_NANOSLEEP;
    if (streq_n(name, len, "sys_lseek"))       return 8;
    if (streq_n(name, len, "sys_mmap"))        return 9;
    if (streq_n(name, len, "sys_creat"))       return 85;
    return -1;
}

static void tc_walk_module(int mod_idx);
static void tc_collect_decl(int d)
{
    AstNode *n = &g_nodes[d];
    if (n->kind == N_FN) {
        int s = sym_add(N_FN, n->s, n->slen, d);
        int arity = 0;
        /* children: params..., ret_type, body (last two) */
        for (int i = 0; i < n->nkids; i++) {
            AstNode *c = &g_nodes[n->kids[i]];
            if (c->kind == N_PARAM) arity++;
        }
        g_syms[s].arity = arity;
    } else if (n->kind == N_EXTERN_FN) {
        int s = sym_add(N_EXTERN_FN, n->s, n->slen, d);
        int arity = 0;
        for (int i = 0; i < n->nkids; i++) {
            AstNode *c = &g_nodes[n->kids[i]];
            if (c->kind == N_PARAM) arity++;
        }
        g_syms[s].arity = arity;
        if (n->i2 == 1) {  /* linux_syscall */
            int nr = syscall_number_for(n->s, n->slen);
            g_syms[s].syscall_nr = nr;
            if (nr < 0 && g_verbose) {
                fprintf(stderr, "luna-boot: note: unknown syscall '%.*s' (will int3)\n",
                        n->slen, n->s);
            }
        }
        if (n->i2 == 2) {  /* extern "C" — platform FFI                 */
            if (g_target == TARGET_WINDOWS) {
                int slot = ffi_register(n->s, n->slen);
                g_syms[s].syscall_nr = slot;   /* IAT index on Windows   */
            } else if (g_target == TARGET_LINUX) {
                int slot = ffi_register_linux(n->s, n->slen);
                g_syms[s].syscall_nr = slot;   /* GOT index on Linux     */
            }
        }
    } else if (n->kind == N_CONST) {
        sym_add(N_CONST, n->s, n->slen, d);
    } else if (n->kind == N_STRUCT) {
        int s = sym_add(N_STRUCT, n->s, n->slen, d);
        int nf = 0;
        for (int i = 0; i < n->nkids; i++) if (g_nodes[n->kids[i]].kind == N_FIELD) nf++;
        g_syms[s].nfields = nf;
        g_syms[s].size = nf * 8;
    } else if (n->kind == N_MODULE) {
        tc_walk_module(d);
    }
}

static void tc_walk_module(int mod_idx)
{
    AstNode *m = &g_nodes[mod_idx];
    for (int i = 0; i < m->nkids; i++) tc_collect_decl(m->kids[i]);
}

static void tc_light(void)
{
    for (int f = 0; f < g_nfiles; f++) {
        tc_walk_module(g_files[f].node_root);
    }
}

/* ========================================================================= */
/* 7. X86-64 INSTRUCTION EMITTERS                                             */
/* ========================================================================= */
/*
 * We emit only the encodings we need.  Everything else is accessible via
 * code_emit_byte/bytes.  This keeps the surface tiny and avoids pulling in
 * a real assembler.
 */

/* Registers (x86-64 numbering): RAX=0 RCX=1 RDX=2 RBX=3 RSP=4 RBP=5 RSI=6 RDI=7
   R8..R15 = 8..15 (need REX.B/X/R).  */
#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R8  8
#define REG_R9  9
#define REG_R10 10

/* push rbp */
static void emit_push_rbp(void) { code_emit_byte(0x55); }
/* pop rbp  */
static void emit_pop_rbp(void)  { code_emit_byte(0x5d); }
/* mov rbp, rsp */
static void emit_mov_rbp_rsp(void)
{
    uint8_t b[] = { 0x48, 0x89, 0xe5 };
    code_emit_bytes(b, 3);
}
/* mov rsp, rbp */
static void emit_mov_rsp_rbp(void)
{
    uint8_t b[] = { 0x48, 0x89, 0xec };
    code_emit_bytes(b, 3);
}
/* sub rsp, imm32 */
static void emit_sub_rsp_imm32(uint32_t v)
{
    uint8_t b[] = { 0x48, 0x81, 0xec };
    code_emit_bytes(b, 3);
    code_emit_u32(v);
}
/* ret */
static void emit_ret(void) { code_emit_byte(0xc3); }
/* int3 */
static void emit_int3(void) { code_emit_byte(0xcc); }
/* syscall */
static void emit_syscall(void)
{
    code_emit_byte(0x0f);
    code_emit_byte(0x05);
}

/* mov rax, imm64  (REX.W + B8 + imm64) */
static void emit_mov_rax_imm64(uint64_t v)
{
    code_emit_byte(0x48);
    code_emit_byte(0xb8);
    code_emit_u64(v);
}
/* mov reg64, imm64  (reg 0..7) */
static void emit_mov_r_imm64(int reg, uint64_t v)
{
    if (reg < 8) {
        code_emit_byte(0x48);
        code_emit_byte((uint8_t)(0xb8 + reg));
    } else {
        code_emit_byte(0x49);
        code_emit_byte((uint8_t)(0xb8 + (reg - 8)));
    }
    code_emit_u64(v);
}

/* mov dst_reg, src_reg  (both 0..15) */
static void emit_mov_r_r(int dst, int src)
{
    uint8_t rex = 0x48;
    if (src >= 8) rex |= 0x04;  /* REX.R */
    if (dst >= 8) rex |= 0x01;  /* REX.B */
    code_emit_byte(rex);
    code_emit_byte(0x89);
    code_emit_byte((uint8_t)(0xc0 | ((src & 7) << 3) | (dst & 7)));
}

/* mov [rbp + disp32], rax */
static void emit_mov_rbp_disp_rax(int32_t disp)
{
    /* REX.W, 0x89, ModRM: mod=10 reg=000(rax) rm=101(rbp) -> 0x85 */
    code_emit_byte(0x48);
    code_emit_byte(0x89);
    code_emit_byte(0x85);
    code_emit_u32((uint32_t)disp);
}
/* mov rax, [rbp + disp32] */
static void emit_mov_rax_rbp_disp(int32_t disp)
{
    code_emit_byte(0x48);
    code_emit_byte(0x8b);
    code_emit_byte(0x85);
    code_emit_u32((uint32_t)disp);
}
/* mov r, [rbp + disp32] */
static void emit_mov_r_rbp_disp(int reg, int32_t disp)
{
    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x04;
    code_emit_byte(rex);
    code_emit_byte(0x8b);
    code_emit_byte((uint8_t)(0x85 | ((reg & 7) << 3)));
    code_emit_u32((uint32_t)disp);
}
/* mov [rbp + disp32], r */
static void emit_mov_rbp_disp_r(int reg, int32_t disp)
{
    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x04;
    code_emit_byte(rex);
    code_emit_byte(0x89);
    code_emit_byte((uint8_t)(0x85 | ((reg & 7) << 3)));
    code_emit_u32((uint32_t)disp);
}

/* push rax */
static void emit_push_rax(void) { code_emit_byte(0x50); }
/* pop rdi / rcx / etc. (reg 0..7) */
static void emit_pop_r(int reg)
{
    if (reg < 8) code_emit_byte((uint8_t)(0x58 + reg));
    else {
        code_emit_byte(0x41);
        code_emit_byte((uint8_t)(0x58 + (reg - 8)));
    }
}
/* push r */
static void emit_push_r(int reg)
{
    if (reg < 8) code_emit_byte((uint8_t)(0x50 + reg));
    else {
        code_emit_byte(0x41);
        code_emit_byte((uint8_t)(0x50 + (reg - 8)));
    }
}

/* add rax, rcx / sub rax, rcx / imul rax, rcx / cqo + idiv rcx / etc. */
static void emit_add_rax_rcx(void) { uint8_t b[]={0x48,0x01,0xc8}; code_emit_bytes(b,3); }
static void emit_sub_rax_rcx(void) { uint8_t b[]={0x48,0x29,0xc8}; code_emit_bytes(b,3); }
static void emit_imul_rax_rcx(void){ uint8_t b[]={0x48,0x0f,0xaf,0xc1}; code_emit_bytes(b,4); }
static void emit_cqo(void)         { uint8_t b[]={0x48,0x99}; code_emit_bytes(b,2); }
static void emit_idiv_rcx(void)    { uint8_t b[]={0x48,0xf7,0xf9}; code_emit_bytes(b,3); }
/* mov rcx, rax */
static void emit_mov_rcx_rax(void) { uint8_t b[]={0x48,0x89,0xc1}; code_emit_bytes(b,3); }
/* cmp rax, rcx */
static void emit_cmp_rax_rcx(void) { uint8_t b[]={0x48,0x39,0xc8}; code_emit_bytes(b,3); }
/* setcc al then movzx rax, al */
static void emit_setcc_al(uint8_t cc)
{
    /* 0F 9x c0 : setCC al */
    code_emit_byte(0x0f);
    code_emit_byte(cc);
    code_emit_byte(0xc0);
}
static void emit_movzx_rax_al(void)
{
    /* 48 0F B6 C0 : movzx rax, al */
    uint8_t b[] = { 0x48, 0x0f, 0xb6, 0xc0 };
    code_emit_bytes(b, 4);
}
/* test rax, rax */
static void emit_test_rax_rax(void) { uint8_t b[]={0x48,0x85,0xc0}; code_emit_bytes(b,3); }
/* je rel32 / jne rel32 / jmp rel32 — return the offset of the 4-byte slot */
static int emit_je_rel32(void) {
    code_emit_byte(0x0f); code_emit_byte(0x84);
    int off = g_code_len; code_emit_u32(0); return off;
}
static int emit_jne_rel32(void) {
    code_emit_byte(0x0f); code_emit_byte(0x85);
    int off = g_code_len; code_emit_u32(0); return off;
}
static int emit_jmp_rel32(void) {
    code_emit_byte(0xe9);
    int off = g_code_len; code_emit_u32(0); return off;
}
/* call rel32 */
static int emit_call_rel32(void) {
    code_emit_byte(0xe8);
    int off = g_code_len; code_emit_u32(0); return off;
}
/* cmp byte [rax], 0 */
static void emit_cmp_byte_mem_rax_0(void)
{
    uint8_t b[] = { 0x80, 0x38, 0x00 };
    code_emit_bytes(b, 3);
}
/* mov rax, [rax + 8] */
static void emit_mov_rax_rax_plus_8(void)
{
    uint8_t b[] = { 0x48, 0x8b, 0x40, 0x08 };
    code_emit_bytes(b, 4);
}
/* mov rax, rsp  (48 89 e0) */
static void emit_mov_rax_rsp(void)
{
    uint8_t b[] = { 0x48, 0x89, 0xe0 };
    code_emit_bytes(b, 3);
}
/* mov [rsp + disp8], rax — store rax to [rsp+disp8]  (48 89 44 24 disp) */
static void emit_mov_rsp_disp8_rax(int8_t disp)
{
    uint8_t b[] = { 0x48, 0x89, 0x44, 0x24, (uint8_t)disp };
    code_emit_bytes(b, 5);
}
/* mov [rsp + disp32], rax  (48 89 84 24 disp32) */
static void emit_mov_rsp_disp32_rax(int32_t disp)
{
    uint8_t b[] = { 0x48, 0x89, 0x84, 0x24 };
    code_emit_bytes(b, 4);
    for (int i = 0; i < 4; i++) code_emit_byte((uint8_t)(disp >> (i * 8)));
}
/* mov rax, [rcx + rax*8]  (48 8b 04 c1) — array read: base=rcx, index=rax */
static void emit_mov_rax_mem_rcx_rax_x8(void)
{
    uint8_t b[] = { 0x48, 0x8b, 0x04, 0xc1 };
    code_emit_bytes(b, 4);
}
/* mov rax, [rax + rcx*8]  (48 8b 04 c8) — base=rax, index=rcx */
static void emit_mov_rax_mem_rax_rcx_x8(void)
{
    uint8_t b[] = { 0x48, 0x8b, 0x04, 0xc8 };
    code_emit_bytes(b, 4);
}
/* ------------------------------------------------------------------------
 * Heap allocator relocations
 *
 * The compiled program ships with a 16 MiB zero-initialised BSS region:
 *     [ bss_base + 0  .. + 8 )   _luna_heap_top pointer (writable)
 *     [ bss_base + 64 .. + 16M)  bump-allocated heap area
 *
 * A `BssReloc` records a RIP-relative displacement in the emitted code
 * that must be patched once the BSS's virtual address is decided at
 * file-write time.  Both N_STRUCT_LIT and N_ARRAY_LIT route through
 * this machinery so their memory outlives the enclosing call frame.    */
typedef struct {
    int code_off;       /* offset of the disp32 slot                 */
    int bss_off;        /* byte offset within BSS the slot points at */
} BssReloc;

#define MAX_BSS_RELOCS 8192
static BssReloc g_bssrelocs[MAX_BSS_RELOCS];
static int      g_nbssrelocs;

#define BSS_BYTES          (16 * 1024 * 1024 + 64)
#define BSS_HEAP_TOP_OFF   0
#define BSS_ARGC_OFF       8
#define BSS_ARGV_OFF       16
#define BSS_HEAP_BASE_OFF  64

/* Emit `lea rax, [rip + bss_disp]` with deferred patching. */
static void emit_lea_rax_rip_bss(int bss_off)
{
    uint8_t b[] = { 0x48, 0x8d, 0x05 };
    code_emit_bytes(b, 3);
    int slot = g_code_len;
    code_emit_u32(0);
    if (g_nbssrelocs >= MAX_BSS_RELOCS) {
        fprintf(stderr, "luna-boot: too many BSS relocs\n"); exit(1);
    }
    g_bssrelocs[g_nbssrelocs].code_off = slot;
    g_bssrelocs[g_nbssrelocs].bss_off  = bss_off;
    g_nbssrelocs++;
}

/* mov [rip + disp32], rax  — 48 89 05 xx xx xx xx  */
static void emit_mov_mem_rip_rax_bss(int bss_off)
{
    uint8_t b[] = { 0x48, 0x89, 0x05 };
    code_emit_bytes(b, 3);
    int slot = g_code_len;
    code_emit_u32(0);
    if (g_nbssrelocs >= MAX_BSS_RELOCS) {
        fprintf(stderr, "luna-boot: too many BSS relocs\n"); exit(1);
    }
    g_bssrelocs[g_nbssrelocs].code_off = slot;
    g_bssrelocs[g_nbssrelocs].bss_off  = bss_off;
    g_nbssrelocs++;
}
/* mov rax, [rip + disp32]  — 48 8B 05 xx xx xx xx  */
static void emit_mov_rax_mem_rip_bss(int bss_off)
{
    uint8_t b[] = { 0x48, 0x8b, 0x05 };
    code_emit_bytes(b, 3);
    int slot = g_code_len;
    code_emit_u32(0);
    if (g_nbssrelocs >= MAX_BSS_RELOCS) {
        fprintf(stderr, "luna-boot: too many BSS relocs\n"); exit(1);
    }
    g_bssrelocs[g_nbssrelocs].code_off = slot;
    g_bssrelocs[g_nbssrelocs].bss_off  = bss_off;
    g_nbssrelocs++;
}
/* add rax, imm32 (sign-extended) — 48 05 imm32 */
static void emit_add_rax_imm32(int32_t v)
{
    code_emit_byte(0x48);
    code_emit_byte(0x05);
    code_emit_u32((uint32_t)v);
}

/* Emit a bump-allocation of `bytes`.  On return rax holds the pointer
 * to the freshly-allocated `bytes`-byte block, bumped off the heap.
 * The block is NOT zero-initialised — callers that need that should
 * clear it themselves (or let a later str_alloc / array_lit do it). */
static void emit_bump_alloc(int bytes)
{
    /* round up to 8-byte alignment */
    int aligned = (bytes + 7) & ~7;
    emit_mov_rax_mem_rip_bss(BSS_HEAP_TOP_OFF); /* rax = current top */
    emit_push_rax();                            /* save pointer      */
    emit_add_rax_imm32(aligned);                /* rax = top + size  */
    emit_mov_mem_rip_rax_bss(BSS_HEAP_TOP_OFF); /* store new top     */
    emit_pop_r(REG_RAX);                        /* rax = old top     */
}

/* mov [rcx + rax*8], rdx  (48 89 14 c1) — array write */
static void emit_mov_mem_rcx_rax_x8_rdx(void)
{
    uint8_t b[] = { 0x48, 0x89, 0x14, 0xc1 };
    code_emit_bytes(b, 4);
}
/* mov reg, [rsp + disp8]  — reg in 0..15.  mod=01, rm=100 (SIB), sib=0x24 (rsp) */
static void emit_mov_r_rsp_disp8(int reg, int8_t d)
{
    uint8_t rex = (reg >= 8) ? 0x4C : 0x48;
    code_emit_byte(rex);
    code_emit_byte(0x8B);
    code_emit_byte((uint8_t)((1 << 6) | ((reg & 7) << 3) | 4));
    code_emit_byte(0x24);
    code_emit_byte((uint8_t)d);
}
/* mov [rsp + disp8], reg */
static void emit_mov_rsp_disp8_r(int8_t d, int reg)
{
    uint8_t rex = (reg >= 8) ? 0x4C : 0x48;
    code_emit_byte(rex);
    code_emit_byte(0x89);
    code_emit_byte((uint8_t)((1 << 6) | ((reg & 7) << 3) | 4));
    code_emit_byte(0x24);
    code_emit_byte((uint8_t)d);
}

/* mov [rbx + disp32], rax — store element into a heap-allocated array /
 * struct whose base we parked in rbx (48 89 83 xx xx xx xx).           */
static void emit_mov_mem_rbx_disp32_rax(int32_t disp)
{
    if (disp >= -128 && disp <= 127) {
        uint8_t b[] = { 0x48, 0x89, 0x43, (uint8_t)disp };
        code_emit_bytes(b, 4);
        return;
    }
    uint8_t b[] = { 0x48, 0x89, 0x83 };
    code_emit_bytes(b, 3);
    code_emit_u32((uint32_t)disp);
}
/* mov rcx, rax  (48 89 c1) */
static void emit_mov_rcx_rax2(void) { uint8_t b[]={0x48,0x89,0xc1}; code_emit_bytes(b,3); }
/* pop rdx  (5a) */
static void emit_pop_rdx(void) { code_emit_byte(0x5a); }

/* mov reg, [rax + disp8]  (reg 0..7, signed 8-bit disp) */
static void emit_mov_r_mem_rax_disp8(int reg, int8_t disp)
{
    code_emit_byte(0x48);
    code_emit_byte(0x8B);
    code_emit_byte((uint8_t)(0x40 | ((reg & 7) << 3)));
    code_emit_byte((uint8_t)disp);
}
/* mov rax, [rax + imm32] (struct field load) */
static void emit_mov_rax_rax_plus_imm32(int32_t disp)
{
    if (disp >= -128 && disp <= 127) {
        uint8_t b[] = { 0x48, 0x8b, 0x40, (uint8_t)(int8_t)disp };
        code_emit_bytes(b, 4);
    } else {
        uint8_t b[] = { 0x48, 0x8b, 0x80 };
        code_emit_bytes(b, 3);
        code_emit_u32((uint32_t)disp);
    }
}

/* lea rax, [rip + disp32]  — used for string literal addresses.
   We emit disp=0 and record a RELOC of kind "rodata" that gets patched
   when the rodata placement is finalised.                               */
typedef struct {
    int code_off;   /* offset of the 4-byte slot                         */
    int from_rip;   /* rip-value at that disp (code_off + 4)             */
    int rodata_off; /* offset in rodata buffer                           */
} RodataReloc;

#define MAX_ROD_RELOCS 8192
static RodataReloc g_rodrelocs[MAX_ROD_RELOCS];
static int         g_nrodrelocs;

static void emit_lea_rax_rip_rodata(int rodata_off)
{
    uint8_t b[] = { 0x48, 0x8d, 0x05 };
    code_emit_bytes(b, 3);
    int slot = g_code_len;
    code_emit_u32(0);
    if (g_nrodrelocs >= MAX_ROD_RELOCS) {
        fprintf(stderr, "luna-boot: too many rodata relocs\n"); exit(1);
    }
    g_rodrelocs[g_nrodrelocs].code_off   = slot;
    g_rodrelocs[g_nrodrelocs].from_rip   = slot + 4;
    g_rodrelocs[g_nrodrelocs].rodata_off = rodata_off;
    g_nrodrelocs++;
}

/* ------------------------------------------------------------------------
 * Windows-target: import-address-table relocations
 *
 * The PE backend routes every WinAPI call through the IAT with
 *     call qword ptr [rip + disp32]           (FF 15 xx xx xx xx)
 * For each emission we record the 4-byte slot and which IAT entry it
 * points at; the PE writer patches disp once final RVAs are known.    */
/* Named slots for the original handful of Windows imports shine() /
 * print() / exit() rely on.  These correspond to the first entries in
 * g_imports[] below.                                                  */
enum {
    IAT_GETSTDHANDLE = 0,
    IAT_WRITEFILE    = 1,
    IAT_EXITPROCESS  = 2
};

/* Each Windows import tracks which DLL provides it so the PE writer
 * can emit one IMAGE_IMPORT_DESCRIPTOR per unique DLL and group the
 * per-function slots under the right one.                             */
typedef struct {
    const char *fn_name;
    const char *dll_name;
} WinImport;

#define MAX_IMPORTS 256
static WinImport g_imports[MAX_IMPORTS];
static int       g_nimports;

/* Maps a WinAPI / CRT function name to the DLL that exports it.  The
 * common ones are hardcoded; unknown names default to KERNEL32.DLL.  */
static const char *winapi_dll(const char *name, int len)
{
    static const struct { const char *fn; const char *dll; } T[] = {
        /* KERNEL32 — Win32 core                                    */
        { "GetStdHandle",    "KERNEL32.DLL" },
        { "WriteFile",       "KERNEL32.DLL" },
        { "ReadFile",        "KERNEL32.DLL" },
        { "ExitProcess",     "KERNEL32.DLL" },
        { "CreateFileA",     "KERNEL32.DLL" },
        { "GetFileSize",     "KERNEL32.DLL" },
        { "GetFileSizeEx",   "KERNEL32.DLL" },
        { "CloseHandle",     "KERNEL32.DLL" },
        { "VirtualAlloc",    "KERNEL32.DLL" },
        { "VirtualFree",     "KERNEL32.DLL" },
        { "GetLastError",    "KERNEL32.DLL" },
        { "GetCommandLineA", "KERNEL32.DLL" },
        { "Sleep",           "KERNEL32.DLL" },
        { "GetTickCount",    "KERNEL32.DLL" },
        /* msvcrt — Microsoft C runtime                             */
        { "fopen",   "msvcrt.dll" }, { "fclose",  "msvcrt.dll" },
        { "fread",   "msvcrt.dll" }, { "fwrite",  "msvcrt.dll" },
        { "fseek",   "msvcrt.dll" }, { "ftell",   "msvcrt.dll" },
        { "fflush",  "msvcrt.dll" }, { "fprintf", "msvcrt.dll" },
        { "printf",  "msvcrt.dll" }, { "puts",    "msvcrt.dll" },
        { "malloc",  "msvcrt.dll" }, { "free",    "msvcrt.dll" },
        { "realloc", "msvcrt.dll" }, { "calloc",  "msvcrt.dll" },
        { "strlen",  "msvcrt.dll" }, { "strcpy",  "msvcrt.dll" },
        { "strcmp",  "msvcrt.dll" }, { "strcat",  "msvcrt.dll" },
        { "strchr",  "msvcrt.dll" }, { "memcpy",  "msvcrt.dll" },
        { "getenv",  "msvcrt.dll" }, { "system",  "msvcrt.dll" },
        { "exit",    "msvcrt.dll" }, { "atoi",    "msvcrt.dll" },
        { "sprintf", "msvcrt.dll" }, { "time",    "msvcrt.dll" },
        { "_open",   "msvcrt.dll" }, { "_read",   "msvcrt.dll" },
        { "_write",  "msvcrt.dll" }, { "_close",  "msvcrt.dll" },
        { "_lseek",  "msvcrt.dll" },
        { "__getmainargs", "msvcrt.dll" },
        /* user32 — windowing                                         */
        { "MessageBoxA", "USER32.DLL" },
        { NULL, NULL }
    };
    for (int i = 0; T[i].fn; i++) {
        int fl = (int)strlen(T[i].fn);
        if (fl == len && memcmp(T[i].fn, name, len) == 0) return T[i].dll;
    }
    return "KERNEL32.DLL";
}

static int ffi_register(const char *name, int len)
{
    for (int i = 0; i < g_nimports; i++) {
        if ((int)strlen(g_imports[i].fn_name) == len &&
            memcmp(g_imports[i].fn_name, name, len) == 0) {
            return i;
        }
    }
    if (g_nimports >= MAX_IMPORTS) {
        fprintf(stderr, "luna-boot: too many FFI imports\n"); exit(1);
    }
    g_imports[g_nimports].fn_name  = arena_strndup(name, len);
    g_imports[g_nimports].dll_name = winapi_dll(name, len);
    return g_nimports++;
}

static void ffi_init_core(void)
{
    ffi_register("GetStdHandle", 12);
    ffi_register("WriteFile",    9);
    ffi_register("ExitProcess",  11);
}

typedef struct {
    int code_off;
    int iat_slot;
} IatReloc;

#define MAX_IAT_RELOCS 8192
static IatReloc g_iatrelocs[MAX_IAT_RELOCS];
static int      g_niatrelocs;

/* ------------------------------------------------------------------------
 * Linux FFI — dynamic linking against libc.so.6
 *
 * Every `extern "C" fn NAME(...)` that is called when targeting Linux
 * gets registered here and ends up as an undefined symbol in the
 * output's .dynsym table.  ld.so resolves it at load time by writing
 * the real libc address into the matching .got slot; our emitted code
 * uses `call qword ptr [rip + disp32]` — one six-byte indirect call
 * per invocation, same shape as the Windows IAT path.                  */
#define MAX_LINUX_IMPORTS 128
static const char *g_linux_imports[MAX_LINUX_IMPORTS];
static int         g_nlinux_imports;

static int ffi_register_linux(const char *name, int len)
{
    for (int i = 0; i < g_nlinux_imports; i++) {
        if ((int)strlen(g_linux_imports[i]) == len &&
            memcmp(g_linux_imports[i], name, len) == 0) {
            return i;
        }
    }
    if (g_nlinux_imports >= MAX_LINUX_IMPORTS) {
        fprintf(stderr, "luna-boot: too many Linux FFI imports\n"); exit(1);
    }
    g_linux_imports[g_nlinux_imports] = arena_strndup(name, len);
    return g_nlinux_imports++;
}

typedef struct {
    int code_off;       /* disp32 slot inside emitted code              */
    int got_slot;       /* 0..g_nlinux_imports-1                        */
} GotReloc;

#define MAX_GOT_RELOCS 8192
static GotReloc g_gotrelocs[MAX_GOT_RELOCS];
static int      g_ngotrelocs;

/* Same bytes as emit_call_iat (FF 15 disp32) — the instruction is
 * identical, only the patch source differs.                           */
static void emit_call_got(int got_slot)
{
    code_emit_byte(0xff);
    code_emit_byte(0x15);
    int slot = g_code_len;
    code_emit_u32(0);
    if (g_ngotrelocs >= MAX_GOT_RELOCS) {
        fprintf(stderr, "luna-boot: too many GOT relocs\n"); exit(1);
    }
    g_gotrelocs[g_ngotrelocs].code_off = slot;
    g_gotrelocs[g_ngotrelocs].got_slot = got_slot;
    g_ngotrelocs++;
}

static void emit_call_iat(int iat_slot)
{
    code_emit_byte(0xff);          /* call [rip + disp32] opcode */
    code_emit_byte(0x15);
    int slot = g_code_len;
    code_emit_u32(0);
    if (g_niatrelocs >= MAX_IAT_RELOCS) {
        fprintf(stderr, "luna-boot: too many IAT relocs\n"); exit(1);
    }
    g_iatrelocs[g_niatrelocs].code_off = slot;
    g_iatrelocs[g_niatrelocs].iat_slot = iat_slot;
    g_niatrelocs++;
}

/* --- Stack / argument helpers used when emitting Win64 calls --- */

/* sub rsp, imm8  (imm8 in range -128..127) — 48 83 EC xx */
static void emit_sub_rsp_imm8(int8_t v)
{
    uint8_t b[] = { 0x48, 0x83, 0xec, (uint8_t)v };
    code_emit_bytes(b, 4);
}
/* add rsp, imm8 — 48 83 C4 xx */
static void emit_add_rsp_imm8(int8_t v)
{
    uint8_t b[] = { 0x48, 0x83, 0xc4, (uint8_t)v };
    code_emit_bytes(b, 4);
}
/* lea r9, [rsp + disp8]  — 4C 8D 4C 24 xx */
static void emit_lea_r9_rsp_disp8(int8_t disp)
{
    uint8_t b[] = { 0x4c, 0x8d, 0x4c, 0x24, (uint8_t)disp };
    code_emit_bytes(b, 5);
}
/* mov qword ptr [rsp + disp8], 0  — 48 C7 44 24 xx 00 00 00 00 */
static void emit_mov_qword_rsp_disp8_imm0(int8_t disp)
{
    uint8_t b[] = { 0x48, 0xc7, 0x44, 0x24, (uint8_t)disp, 0, 0, 0, 0 };
    code_emit_bytes(b, 9);
}
/* mov r8, [rbx - disp8]  — used by Win64 shine(): load string length.
 * Encoding: REX.WR + 8B + ModRM(mod=01, reg=R8-ext=000, rm=RBX) + disp8
 *     4C 8B 43 disp8   (42 R/M base) — actually RBX has r/m=3:
 *     4C 8B 43 F8      = mov r8, [rbx - 8]                              */
static void emit_mov_r8_mem_rbx_disp8(int8_t disp)
{
    uint8_t b[] = { 0x4c, 0x8b, 0x43, (uint8_t)disp };
    code_emit_bytes(b, 4);
}

/* ========================================================================= */
/* 8. LOWERER                                                                 */
/* ========================================================================= */

static int local_lookup(const char *name, int len)
{
    for (int i = g_nlocals - 1; i >= 0; i--) {
        if (streq_nn(g_locals[i].name, g_locals[i].name_len, name, len)) return i;
    }
    return -1;
}

static int local_add(const char *name, int len, int is_param, int pidx)
{
    if (g_nlocals >= 256) { fprintf(stderr, "luna-boot: too many locals\n"); exit(1); }
    int id = g_nlocals++;
    g_locals[id].name = name;
    g_locals[id].name_len = len;
    g_locals[id].is_param = is_param;
    g_locals[id].param_idx = pidx;
    g_locals[id].type_name = NULL;
    g_locals[id].type_name_len = 0;
    g_frame_size += 8;
    g_locals[id].offset = -g_frame_size;
    return id;
}

/* Find a struct declaration by name in the symbol table, return the AST
 * node index of N_STRUCT or -1 if not found.                            */
static int find_struct(const char *name, int len)
{
    int si = sym_find(name, len);
    if (si < 0) return -1;
    if (g_syms[si].kind != N_STRUCT) return -1;
    return g_syms[si].node;
}

/* Look up field `fname` inside struct node `sn`; return the field index
 * (0-based, which maps to offset fidx*8) or -1 if not found.            */
static int struct_field_index(int sn, const char *fname, int flen)
{
    AstNode *s = &g_nodes[sn];
    for (int i = 0; i < s->nkids; i++) {
        AstNode *f = &g_nodes[s->kids[i]];
        if (f->kind == N_FIELD && f->slen == flen &&
            memcmp(f->s, fname, flen) == 0) {
            return i;
        }
    }
    return -1;
}

/* Total size in bytes of a struct: one 8-byte slot per field. */
static int struct_size(int sn)
{
    return g_nodes[sn].nkids * 8;
}

/* Match @name -> bare name.  In this bootstrap we treat @x and x identically
   for lookup, since the lexer emits them as distinct kinds but with the same
   payload string.  The string fields (s,slen) never include the leading '@'. */

static void lower_expr(int node);
static void lower_stmt(int node);
static void lower_block(int node);

/* Emit a load of a local / param / global into RAX. */
static void load_ident(const char *name, int len, int file, int line)
{
    int li = local_lookup(name, len);
    if (li >= 0) {
        emit_mov_rax_rbp_disp(g_locals[li].offset);
        return;
    }
    /* global const / meow */
    int si = sym_find(name, len);
    if (si >= 0) {
        Sym *s = &g_syms[si];
        if (s->kind == N_CONST) {
            AstNode *cn = &g_nodes[s->node];
            /* value is the last child */
            if (cn->nkids >= 1) {
                int vn = cn->kids[cn->nkids - 1];
                AstNode *v = &g_nodes[vn];
                if (v->kind == N_INT)   { emit_mov_rax_imm64((uint64_t)v->iv); return; }
                if (v->kind == N_BOOL)  { emit_mov_rax_imm64((uint64_t)(long long)v->i1); return; }
                if (v->kind == N_STR)   { emit_lea_rax_rip_rodata(g_strs[v->str_idx].off); return; }
            }
            emit_mov_rax_imm64(0);
            return;
        }
    }
    /* Tolerant: unknown identifiers resolve to 0 so the bootstrap can parse
       real modules that reference cross-module constants our simple name
       resolver doesn't see (e.g. `AST_MEMBER_ACCESS` from parser.luna used
       in titan_opt.luna).  Silent; the self-hosted compiler will catch
       real misspellings with its full type system.                      */
    emit_mov_rax_imm64(0);
}

/* System V argument register index -> register. */
static int sysv_argreg(int i)
{
    switch (i) {
        case 0: return REG_RDI;
        case 1: return REG_RSI;
        case 2: return REG_RDX;
        case 3: return REG_RCX;
        case 4: return REG_R8;
        case 5: return REG_R9;
    }
    return -1;
}
/* Linux syscall arg register -> same, but R10 replaces RCX for arg 3. */
static int syscall_argreg(int i)
{
    switch (i) {
        case 0: return REG_RDI;
        case 1: return REG_RSI;
        case 2: return REG_RDX;
        case 3: return REG_R10;
        case 4: return REG_R8;
        case 5: return REG_R9;
    }
    return -1;
}

/* Builtins: compiler-intrinsics that the bootstrap resolves without
   needing a declaration.  These appear in the stdlib / core with simple
   meanings — evaluate each arg for side-effects and return 0 (the real
   compiler plumbs them through a proper intrinsic table).               */
static int is_boot_builtin(const char *s, int n)
{
    static const char *K[] = {
        /* Memory-primitive intrinsics */
        "volatile_read", "volatile_write", "__builtin_substr", "substr",
        "memcpy", "memset", "memcmp",
        "char_at", "byte_at", "__byte_to_char", "__char_to_byte",
        "chr", "ord", "to_char", "from_char",
        "parser_extract_name", "parser_node_name", "parser_get_string",
        "region_get_string", "region_intern",
        "strip_at", "split_at", "trim", "to_upper", "to_lower",
        "is_digit", "is_alpha", "is_space", "is_alnum",
        "volatile_read8", "volatile_write8",
        "volatile_read32", "volatile_write32",
        "volatile_read64", "volatile_write64",
        "size_of", "align_of", "offset_of",
        "bit_cast", "transmute", "unreachable",
        /* Conversions */
        "to_int", "to_float", "to_str", "to_bool",
        "str", "int", "float", "bool",
        /* Byte / string helpers supplied by the runtime */
        "bytes_to_str", "str_to_bytes", "str_eq", "str_len", "str_cmp",
        "int_to_str", "u64_to_str", "float_to_str", "char_to_str", "u64_to_hex",
        /* Collection / container intrinsics */
        "len", "append", "copy", "clone", "push", "pop",
        /* Printing */
        "print", "println", "shine", "eprintln", "shine_bytes",
        /* Error handling */
        "err", "ok", "is_err", "unwrap", "unwrap_err",
        /* System helpers used across stdlib */
        "exit", "abort", "assert", "panic",
        /* Math primitives */
        "min", "max", "abs", "pow", "floor", "ceil", "round",
        "sqrt", "sin", "cos", "tan",
        /* Module-exported symbols from stdlib/runtime_core that many files call */
        "time_now_us", "time_now_ns", "time_now",
        "read_file_opt", "read_file", "write_file", "append_file",
        "env_args", "env_get", "env_set",
        "is_err", "unwrap_err",
        "str_starts_with", "str_ends_with", "str_count_lines", "str_find",
        "append", "list_find",
        "format",
        NULL
    };
    for (int i = 0; K[i]; i++) {
        int klen = (int)strlen(K[i]);
        if (klen == n && memcmp(s, K[i], n) == 0) return 1;
    }
    return 0;
}

static void lower_call(int node)
{
    AstNode *n = &g_nodes[node];
    int callee = n->kids[0];
    AstNode *c = &g_nodes[callee];

    /* Method-call sugar: `@obj.foo(args)` is lowered as `foo(@obj, args)`
     * when `foo` resolves to a known Luna function.  The object becomes
     * the first positional parameter — standard "explicit self" ABI.    */
    if (c->kind == N_FIELD_ACCESS) {
        int si = sym_find(c->s, c->slen);
        if (si >= 0 && g_syms[si].kind == N_FN) {
            int nuser_args = n->nkids - 1;
            int total_args = 1 + nuser_args;
            /* Evaluate and push: self first, then each user arg.         */
            lower_expr(c->kids[0]);                       /* rax = @obj   */
            emit_push_rax();
            for (int i = 0; i < nuser_args; i++) {
                lower_expr(n->kids[1 + i]);
                emit_push_rax();
            }
            /* Pop the stack slots we reserved, starting from the last arg
             * so register-passed args land in the right order.  Extras
             * past the sixth stay on stack in caller order for the callee. */
            int reg_args = total_args < 6 ? total_args : 6;
            int stack_args = total_args - reg_args;
            /* Our pushes went: self, arg1, arg2, ..., argN.  Pop in
             * reverse so:
             *   first pop -> argN, place at stack top
             *   ...
             *   last pop -> self -> rdi
             * For args 6..N we leave them on the stack (already there
             * in the right order above the return address).             */
            /* Step 1: for stack-passed args (indices 6..total_args-1), we
             * need them at [rsp .. rsp+stack_args*8] in THIS order
             * (arg6 at rsp+0, arg7 at rsp+8, ...).  Currently they are
             * the top of stack in the order we pushed them: self at
             * bottom, argN at top.  We want to keep only the first
             * six register args popped off, and leave arg6..argN in the
             * correct layout on stack.
             *
             * Simplest approach: pop ALL args into temporary registers
             * we don't clobber (only rdi/rsi/rdx/rcx/r8/r9 needed),
             * then push back the stack_args in reverse (so they end up
             * at rsp+0 = arg6 layout).                                   */
            if (stack_args > 0) {
                /* Pop top-to-bottom: argN..arg0 order, we want to keep
                 * arg6..argN on stack.  Read them from the stack, save
                 * into a known location, then re-layout.
                 *
                 * Shortcut implementation: leave pushes as-is (stack order
                 * top-to-bottom = argN,argN-1,...,arg1,arg0).  Pop the
                 * first 6 (argN, argN-1, ..., argN-5) into registers that
                 * DON'T match final register mapping, then rearrange.   */
                /* This branch is rare.  Keep simple: bail to 6-arg cap. */
                total_args = 6;
                nuser_args = 5;          /* drop anything past 6 total  */
                reg_args = 6;
                stack_args = 0;
                /* Drop unused pushes — align back to 6.  The user-arg
                 * pushes past 6 are already on stack; simply release.  */
                int drop = (1 + nuser_args) - total_args;
                if (drop > 0) emit_add_rsp_imm8((int8_t)(drop * 8));
            }
            /* Pop each arg into its register, in reverse push order.    */
            for (int i = total_args - 1; i >= 0; i--) {
                emit_pop_r(sysv_argreg(i));
            }
            /* Emit the call and record the reloc.                        */
            int slot = emit_call_rel32();
            if (g_nrelocs >= MAX_RELOCS) {
                fprintf(stderr, "luna-boot: too many relocs\n"); exit(1);
            }
            g_relocs[g_nrelocs].code_off  = slot;
            g_relocs[g_nrelocs].target_fn = si;
            g_relocs[g_nrelocs].from_off  = slot + 4;
            g_nrelocs++;
            return;
        }
        /* Unknown method — fall through to tolerant no-op.              */
    }

    if (c->kind != N_IDENT) {
        /* Tolerant indirect call (lambda, function-valued variable, or
         * unresolved method): evaluate sub-expressions and return 0.   */
        lower_expr(callee);
        int nargs_i = n->nkids - 1;
        for (int i = 0; i < nargs_i; i++) lower_expr(n->kids[1 + i]);
        emit_mov_rax_imm64(0);
        return;
    }
    /* Windows target: intercept read_file / write_file BEFORE sym_find.
     * Prelude provides a Linux implementation via sys_open/etc., but on
     * Windows we must bypass that and route through the msvcrt CRT
     * (fopen / fread / fseek / ftell / fwrite / fclose).               */
    if (g_target == TARGET_WINDOWS &&
        c->slen == 9 && memcmp(c->s, "read_file", 9) == 0 && n->nkids >= 2)
    {
        int fopen_s  = ffi_register("fopen",  5);
        int fseek_s  = ffi_register("fseek",  5);
        int ftell_s  = ffi_register("ftell",  5);
        int fread_s  = ffi_register("fread",  5);
        int fclose_s = ffi_register("fclose", 6);

        lower_expr(n->kids[1]);                 /* rax = path ptr        */

        /* Stack layout after `push rbx + sub rsp, 64` (rsp 16-aligned):
         *   [rsp +  0..31]   shadow for WinAPI calls
         *   [rsp + 32]       fp / saved path
         *   [rsp + 40]       size
         *   [rsp + 48]       buf ptr
         *   [rsp + 56]       pad                                      */
        emit_push_r(3 /* rbx */);
        emit_sub_rsp_imm8(56);                  /* 16-align post-push   */
        emit_mov_rsp_disp8_r(32, REG_RAX);      /* [rsp+32] = path      */

        /* fopen(path, "rb") */
        emit_mov_r_rsp_disp8(REG_RCX, 32);      /* rcx = path           */
        /* Register a "rb" rodata literal.                              */
        int rb_idx = strpool_add("rb", 2);
        emit_lea_rax_rip_rodata(g_strs[rb_idx].off);
        emit_mov_r_r(REG_RDX, REG_RAX);         /* rdx = "rb"           */
        emit_call_iat(fopen_s);
        emit_mov_rsp_disp8_r(32, REG_RAX);      /* [rsp+32] = fp        */

        /* fseek(fp, 0, SEEK_END=2) */
        emit_mov_r_rsp_disp8(REG_RCX, 32);
        emit_mov_r_imm64(REG_RDX, 0);
        emit_mov_r_imm64(8 /* r8 */, 2);
        emit_call_iat(fseek_s);

        /* ftell(fp) -> size */
        emit_mov_r_rsp_disp8(REG_RCX, 32);
        emit_call_iat(ftell_s);
        emit_mov_rsp_disp8_r(40, REG_RAX);      /* [rsp+40] = size      */

        /* fseek(fp, 0, SEEK_SET=0) */
        emit_mov_r_rsp_disp8(REG_RCX, 32);
        emit_mov_r_imm64(REG_RDX, 0);
        emit_mov_r_imm64(8, 0);
        emit_call_iat(fseek_s);

        /* Allocate new_str(size) inline via bump allocator.             */
        emit_mov_r_rsp_disp8(REG_RAX, 40);      /* rax = size           */
        emit_add_rax_imm32(8 + 7);
        /* and rax, -8 -> 48 83 E0 F8                                    */
        { uint8_t b[] = { 0x48, 0x83, 0xe0, 0xf8 }; code_emit_bytes(b, 4); }
        emit_mov_rcx_rax2();                    /* rcx = aligned size   */
        emit_mov_rax_mem_rip_bss(BSS_HEAP_TOP_OFF);
        emit_mov_rsp_disp8_r(48, REG_RAX);      /* [rsp+48] = header    */
        /* add rax, rcx -> 48 01 c8                                      */
        { uint8_t b[] = { 0x48, 0x01, 0xc8 }; code_emit_bytes(b, 3); }
        emit_mov_mem_rip_rax_bss(BSS_HEAP_TOP_OFF);
        emit_mov_r_rsp_disp8(REG_RAX, 48);      /* rax = header         */
        emit_mov_r_rsp_disp8(REG_RDX, 40);      /* rdx = size           */
        /* mov [rax], rdx -> 48 89 10                                    */
        { uint8_t b[] = { 0x48, 0x89, 0x10 }; code_emit_bytes(b, 3); }
        /* add rax, 8 -> 48 83 c0 08                                     */
        { uint8_t b[] = { 0x48, 0x83, 0xc0, 0x08 }; code_emit_bytes(b, 4); }
        emit_mov_rsp_disp8_r(48, REG_RAX);      /* [rsp+48] = data ptr  */

        /* fread(buf, 1, size, fp) */
        emit_mov_r_rsp_disp8(REG_RCX, 48);      /* rcx = buf            */
        emit_mov_r_imm64(REG_RDX, 1);
        emit_mov_r_rsp_disp8(8 /* r8 */, 40);   /* r8 = size            */
        emit_mov_r_rsp_disp8(9 /* r9 */, 32);   /* r9 = fp              */
        emit_call_iat(fread_s);

        /* fclose(fp) */
        emit_mov_r_rsp_disp8(REG_RCX, 32);
        emit_call_iat(fclose_s);

        /* Return buf.                                                   */
        emit_mov_r_rsp_disp8(REG_RAX, 48);
        emit_add_rsp_imm8(56);
        emit_pop_r(3);
        return;
    }

    if (g_target == TARGET_WINDOWS &&
        c->slen == 10 && memcmp(c->s, "write_file", 10) == 0 && n->nkids >= 3)
    {
        int fopen_s  = ffi_register("fopen",  5);
        int fwrite_s = ffi_register("fwrite", 6);
        int fclose_s = ffi_register("fclose", 6);

        /* Evaluate path (rax) and content separately.                  */
        lower_expr(n->kids[1]);                 /* rax = path           */
        emit_push_r(3);                         /* push rbx             */
        emit_sub_rsp_imm8(56);                  /* 16-align post-push   */
        /* Stack:  [rsp +  0..31] shadow, [rsp+32] fp, [rsp+40] content */
        emit_mov_rsp_disp8_r(32, REG_RAX);      /* stash path there     */

        lower_expr(n->kids[2]);                 /* rax = content        */
        emit_mov_rsp_disp8_r(40, REG_RAX);

        /* fopen(path, "wb") */
        emit_mov_r_rsp_disp8(REG_RCX, 32);
        int wb_idx = strpool_add("wb", 2);
        emit_lea_rax_rip_rodata(g_strs[wb_idx].off);
        emit_mov_r_r(REG_RDX, REG_RAX);
        emit_call_iat(fopen_s);
        emit_mov_rsp_disp8_r(32, REG_RAX);      /* [rsp+32] = fp        */

        /* fwrite(content, 1, str_len(content), fp) */
        emit_mov_r_rsp_disp8(REG_RCX, 40);      /* rcx = content        */
        emit_mov_r_imm64(REG_RDX, 1);
        /* r8 = [rcx - 8] = length */
        /* Current rcx already = content, so: mov r8, [rcx-8] -> 4C 8B 41 F8 */
        { uint8_t b[] = { 0x4c, 0x8b, 0x41, 0xf8 }; code_emit_bytes(b, 4); }
        emit_mov_r_rsp_disp8(9, 32);            /* r9 = fp              */
        emit_call_iat(fwrite_s);

        /* fclose(fp) */
        emit_mov_r_rsp_disp8(REG_RCX, 32);
        emit_call_iat(fclose_s);

        emit_mov_rax_imm64(0);
        emit_add_rsp_imm8(56);
        emit_pop_r(3);
        return;
    }

    int si = sym_find(c->s, c->slen);
    if (si < 0) {
        /* --- Inline string intrinsics --------------------------------
         * These emit a handful of bytes each; callers use them as
         * building blocks for the Luna-level string helpers in
         * bootstrap/prelude.luna.                                       */

        /* str_len(s) — length lives 8 bytes before the data pointer. */
        if (c->slen == 7 && memcmp(c->s, "str_len", 7) == 0 && n->nkids >= 2) {
            lower_expr(n->kids[1]);
            emit_mov_r_mem_rax_disp8(REG_RAX, -8);
            return;
        }

        /* str_byte(s, i) — unsigned byte at s[i].                       */
        if (c->slen == 8 && memcmp(c->s, "str_byte", 8) == 0 && n->nkids >= 3) {
            lower_expr(n->kids[1]);                    /* rax = s        */
            emit_push_rax();
            lower_expr(n->kids[2]);                    /* rax = i        */
            emit_mov_rcx_rax2();                       /* rcx = i        */
            emit_pop_r(REG_RAX);                       /* rax = s        */
            /* add rax, rcx  -> 48 01 c8                                 */
            { uint8_t b[] = { 0x48, 0x01, 0xc8 }; code_emit_bytes(b, 3); }
            /* movzx rax, byte [rax] -> 48 0F B6 00                      */
            { uint8_t b[] = { 0x48, 0x0f, 0xb6, 0x00 }; code_emit_bytes(b, 4); }
            return;
        }

        /* str_set_byte(s, i, b) — write low byte of b to s[i].         */
        if (c->slen == 12 && memcmp(c->s, "str_set_byte", 12) == 0 && n->nkids >= 4) {
            lower_expr(n->kids[1]);                    /* rax = s        */
            emit_push_rax();
            lower_expr(n->kids[2]);                    /* rax = i        */
            emit_push_rax();
            lower_expr(n->kids[3]);                    /* rax = b        */
            emit_pop_r(REG_RCX);                       /* rcx = i        */
            emit_pop_rdx();                            /* rdx = s        */
            /* add rdx, rcx  -> 48 01 ca                                 */
            { uint8_t b[] = { 0x48, 0x01, 0xca }; code_emit_bytes(b, 3); }
            /* mov [rdx], al -> 88 02                                    */
            { uint8_t b[] = { 0x88, 0x02 }; code_emit_bytes(b, 2); }
            emit_mov_rax_imm64(0);
            return;
        }

        /* args_count() — number of command-line arguments (including
         * argv[0], the program name).                                 */
        if (c->slen == 10 && memcmp(c->s, "args_count", 10) == 0) {
            if (g_target == TARGET_LINUX) {
                emit_mov_rax_mem_rip_bss(BSS_ARGC_OFF);
            } else if (g_target == TARGET_WINDOWS) {
                /* On Windows argc is materialised once by our startup
                 * helper and cached in the BSS_ARGC_OFF slot.          */
                emit_mov_rax_mem_rip_bss(BSS_ARGC_OFF);
            } else {
                emit_mov_rax_imm64(0);
            }
            return;
        }
        /* arg_raw(i) — i-th argv pointer (raw, NUL-terminated C string,
         * NOT a length-prefixed Luna string).  The prelude wraps this in
         * arg(i) which converts to a proper Luna string.               */
        if (c->slen == 7 && memcmp(c->s, "arg_raw", 7) == 0 && n->nkids >= 2) {
            if (g_target == TARGET_LINUX) {
                lower_expr(n->kids[1]);
                emit_push_rax();
                emit_mov_rax_mem_rip_bss(BSS_ARGV_OFF);
                emit_pop_r(REG_RCX);
                emit_mov_rax_mem_rax_rcx_x8();
            } else if (g_target == TARGET_WINDOWS) {
                /* Windows stores parsed argv the same way (array of
                 * char* in BSS).                                        */
                lower_expr(n->kids[1]);
                emit_push_rax();
                emit_mov_rax_mem_rip_bss(BSS_ARGV_OFF);
                emit_pop_r(REG_RCX);
                emit_mov_rax_mem_rax_rcx_x8();
            } else {
                emit_mov_rax_imm64(0);
            }
            return;
        }

        /* new_str(len) — bump-allocate (len + 8) bytes on the heap,
         * write the length as a qword prefix, return pointer to the
         * first data byte (past the prefix).                            */
        if (c->slen == 7 && memcmp(c->s, "new_str", 7) == 0 && n->nkids >= 2) {
            lower_expr(n->kids[1]);                    /* rax = len      */
            emit_push_rax();                           /* save len       */
            /* compute aligned alloc size: (len + 8 + 7) & ~7            */
            emit_add_rax_imm32(8 + 7);
            /* and rax, -8 -> 48 83 e0 f8                                */
            { uint8_t b[] = { 0x48, 0x83, 0xe0, 0xf8 }; code_emit_bytes(b, 4); }
            emit_mov_rcx_rax2();                       /* rcx = alloc sz */
            emit_mov_rax_mem_rip_bss(BSS_HEAP_TOP_OFF);/* rax = heap_top */
            emit_push_rax();                           /* save base      */
            /* add rax, rcx -> 48 01 c8                                  */
            { uint8_t b[] = { 0x48, 0x01, 0xc8 }; code_emit_bytes(b, 3); }
            emit_mov_mem_rip_rax_bss(BSS_HEAP_TOP_OFF);/* new top        */
            emit_pop_r(REG_RAX);                       /* rax = base     */
            emit_pop_rdx();                            /* rdx = len      */
            /* mov [rax], rdx -> 48 89 10                                */
            { uint8_t b[] = { 0x48, 0x89, 0x10 }; code_emit_bytes(b, 3); }
            /* add rax, 8 -> 48 83 c0 08                                 */
            { uint8_t b[] = { 0x48, 0x83, 0xc0, 0x08 }; code_emit_bytes(b, 4); }
            return;
        }

        /* `shine(x)` — first real I/O intrinsic.  Takes one string argument
         * (pointer from N_STR / string-typed variable) and writes it to
         * stdout followed by a newline.  On Linux we inline the write(2)
         * syscall; on Windows the PE backend translates the same three-
         * step "write string / write newline / fall through" sequence into
         * two WriteFile calls via the import table (Slice 2).             */
        if (c->slen == 5 && memcmp(c->s, "shine", 5) == 0 && n->nkids >= 2) {
            if (g_target == TARGET_LINUX) {
                lower_expr(n->kids[1]);                  /* rax = str ptr  */
                emit_mov_r_r(REG_RSI, REG_RAX);          /* rsi = ptr      */
                emit_mov_r_mem_rax_disp8(REG_RDX, -8);   /* rdx = len      */
                emit_mov_r_imm64(REG_RDI, 1);            /* rdi = stdout   */
                emit_mov_rax_imm64(SYS_WRITE);           /* rax = 1        */
                emit_syscall();
                if (g_newline_str_idx < 0) {
                    g_newline_str_idx = strpool_add("\n", 1);
                }
                emit_lea_rax_rip_rodata(g_strs[g_newline_str_idx].off);
                emit_mov_r_r(REG_RSI, REG_RAX);
                emit_mov_r_imm64(REG_RDX, 1);
                emit_mov_r_imm64(REG_RDI, 1);
                emit_mov_rax_imm64(SYS_WRITE);
                emit_syscall();
                emit_mov_rax_imm64(0);
                return;
            }
            if (g_target == TARGET_WINDOWS) {
                lower_expr(n->kids[1]);
                emit_push_r(3 /* rbx */);
                emit_mov_r_r(3 /* rbx */, REG_RAX);
                emit_sub_rsp_imm8(56);
                emit_mov_r_imm64(REG_RCX, (uint64_t)(int64_t)-11);
                emit_call_iat(IAT_GETSTDHANDLE);
                emit_mov_r_r(REG_RCX, REG_RAX);
                emit_mov_r_r(REG_RDX, 3 /* rbx */);
                emit_mov_r8_mem_rbx_disp8(-8);
                emit_lea_r9_rsp_disp8(40);
                emit_mov_qword_rsp_disp8_imm0(32);
                emit_call_iat(IAT_WRITEFILE);
                emit_mov_r_imm64(REG_RCX, (uint64_t)(int64_t)-11);
                emit_call_iat(IAT_GETSTDHANDLE);
                if (g_newline_str_idx < 0) {
                    g_newline_str_idx = strpool_add("\n", 1);
                }
                emit_mov_r_r(REG_RCX, REG_RAX);
                emit_lea_rax_rip_rodata(g_strs[g_newline_str_idx].off);
                emit_mov_r_r(REG_RDX, REG_RAX);
                emit_mov_r_imm64(8 /* r8 */, 1);
                emit_lea_r9_rsp_disp8(40);
                emit_mov_qword_rsp_disp8_imm0(32);
                emit_call_iat(IAT_WRITEFILE);
                emit_add_rsp_imm8(56);
                emit_pop_r(3 /* rbx */);
                emit_mov_rax_imm64(0);
                return;
            }
        }
        /* exit(code) — terminate the process with the given exit status.
         * On Linux this becomes exit_group(code); on Windows it becomes
         * ExitProcess(code).  Code-noreturn: we still emit `mov rax, 0`
         * for downstream expression compatibility, but control never
         * reaches it.                                                     */
        if (c->slen == 4 && memcmp(c->s, "exit", 4) == 0 && n->nkids >= 2) {
            lower_expr(n->kids[1]);                  /* rax = code */
            if (g_target == TARGET_WINDOWS) {
                /* Win64: callee-reserved 32-byte shadow region immediately
                 * above rsp is required; we start 16-byte aligned inside a
                 * Luna function (prologue guarantees it), so subtracting
                 * exactly 32 preserves alignment across the call.         */
                emit_mov_r_r(REG_RCX, REG_RAX);
                emit_sub_rsp_imm8(32);
                emit_call_iat(IAT_EXITPROCESS);
                emit_int3();
            } else {
                emit_mov_r_r(REG_RDI, REG_RAX);
                emit_mov_rax_imm64((uint64_t)SYS_EXIT_GROUP);
                emit_syscall();
                emit_int3();
            }
            emit_mov_rax_imm64(0);
            return;
        }
        /* print(s) — write string without trailing newline.  Mirrors
         * shine() minus the final newline syscall/WriteFile.            */
        if (c->slen == 5 && memcmp(c->s, "print", 5) == 0 && n->nkids >= 2) {
            lower_expr(n->kids[1]);
            if (g_target == TARGET_LINUX) {
                emit_mov_r_r(REG_RSI, REG_RAX);
                emit_mov_r_mem_rax_disp8(REG_RDX, -8);
                emit_mov_r_imm64(REG_RDI, 1);
                emit_mov_rax_imm64(SYS_WRITE);
                emit_syscall();
                emit_mov_rax_imm64(0);
                return;
            }
            if (g_target == TARGET_WINDOWS) {
                emit_push_r(3 /* rbx */);
                emit_mov_r_r(3 /* rbx */, REG_RAX);
                emit_sub_rsp_imm8(56);
                emit_mov_r_imm64(REG_RCX, (uint64_t)(int64_t)-11);
                emit_call_iat(IAT_GETSTDHANDLE);
                emit_mov_r_r(REG_RCX, REG_RAX);
                emit_mov_r_r(REG_RDX, 3 /* rbx */);
                emit_mov_r8_mem_rbx_disp8(-8);
                emit_lea_r9_rsp_disp8(40);
                emit_mov_qword_rsp_disp8_imm0(32);
                emit_call_iat(IAT_WRITEFILE);
                emit_add_rsp_imm8(56);
                emit_pop_r(3 /* rbx */);
                emit_mov_rax_imm64(0);
                return;
            }
        }
        if (is_boot_builtin(c->s, c->slen)) {
            /* Tolerant builtin: evaluate each arg for side-effects and
               return 0.  Real semantics live in the self-hosted compiler. */
            int nargs = n->nkids - 1;
            for (int i = 0; i < nargs; i++) lower_expr(n->kids[1 + i]);
            emit_mov_rax_imm64(0);
            return;
        }
        /* Tolerant: unknown function call — evaluate args for side-effects,
           return 0.  The self-hosted compiler enforces resolution; the
           bootstrap must not break on test-runner bookkeeping that references
           functions declared in later self-hosted passes.                  */
        int nargs = n->nkids - 1;
        for (int i = 0; i < nargs; i++) lower_expr(n->kids[1 + i]);
        emit_mov_rax_imm64(0);
        return;
    }
    Sym *s = &g_syms[si];
    int nargs = n->nkids - 1;
    int is_syscall  = (s->kind == N_EXTERN_FN) && (g_nodes[s->node].i2 == 1);
    int is_extern_c = (s->kind == N_EXTERN_FN) && (g_nodes[s->node].i2 == 2);

    /* Syscalls never get more than 6 args (Linux kernel ABI), so stack
     * passing only matters for ordinary Luna-internal calls.            */
    if (is_syscall && nargs > 6) {
        /* Evaluate extras for side-effects, drop them. */
        for (int i = 6; i < nargs; i++) lower_expr(n->kids[1 + i]);
        nargs = 6;
    }

    int stack_args = (nargs > 6) ? (nargs - 6) : 0;
    int reg_args   = nargs - stack_args;

    /* Stack-passed args go first, in reverse index order so after all
     * pushes the layout above rsp matches the callee's expectation:
     *     [rsp+0]  = arg6
     *     [rsp+8]  = arg7
     *     ...
     * Then register args are pushed in index order and popped in
     * reverse to their assigned register, leaving rsp pointing at arg6.
     */
    for (int i = nargs - 1; i >= reg_args; i--) {
        lower_expr(n->kids[1 + i]);
        emit_push_rax();
    }
    for (int i = 0; i < reg_args; i++) {
        lower_expr(n->kids[1 + i]);
        emit_push_rax();
    }
    for (int i = reg_args - 1; i >= 0; i--) {
        int r = is_syscall ? syscall_argreg(i) : sysv_argreg(i);
        emit_pop_r(r);
    }

    if (is_syscall) {
        if (s->syscall_nr < 0) {
            emit_int3();
            return;
        }
        emit_mov_rax_imm64((uint64_t)(long long)s->syscall_nr);
        emit_syscall();
        return;
    }
    if (is_extern_c) {
        if (g_target == TARGET_WINDOWS && s->syscall_nr >= 0) {
            /* extern "C" fn via IAT.  Args arrived in sysv order — shuffle
             * to Win64 order (rcx,rdx,r8,r9) before the call.            */
            if (reg_args >= 4) emit_mov_r_r(9 /* r9 */,  REG_RCX);
            if (reg_args >= 3) emit_mov_r_r(8 /* r8 */,  REG_RDX);
            if (reg_args >= 2) emit_mov_r_r(REG_RDX,     REG_RSI);
            if (reg_args >= 1) emit_mov_r_r(REG_RCX,     REG_RDI);
            emit_sub_rsp_imm8(32);
            emit_call_iat(s->syscall_nr);
            emit_add_rsp_imm8(32);
            return;
        }
        if (g_target == TARGET_LINUX && s->syscall_nr >= 0) {
            /* extern "C" fn on Linux uses the sysv ABI natively — args
             * are already in rdi/rsi/rdx/rcx/r8/r9 from the generic
             * push/pop loop above.  Call via the GOT so ld.so resolves
             * the symbol at load time.                                  */
            emit_call_got(s->syscall_nr);
            return;
        }
        emit_int3();
        return;
    }

    int slot = emit_call_rel32();
    if (g_nrelocs >= MAX_RELOCS) { fprintf(stderr, "luna-boot: too many relocs\n"); exit(1); }
    g_relocs[g_nrelocs].code_off  = slot;
    g_relocs[g_nrelocs].target_fn = si;
    g_relocs[g_nrelocs].from_off  = slot + 4;
    g_nrelocs++;

    /* Clean up stack-passed args after the call so rsp is restored. */
    if (stack_args > 0) {
        int cleanup = stack_args * 8;
        /* stack_args in practice never exceeds 10 (80 bytes), comfortably
         * under the 127-byte disp8 limit.                                 */
        emit_add_rsp_imm8((int8_t)cleanup);
    }
}

/* Look up the struct type declared for the value produced by `obj` and
 * return the N_STRUCT node index — or -1 if we can't tell.  Works for:
 *   - N_IDENT pointing at a local with a known type_name
 *   - N_STRUCT_LIT with a resolvable type name
 *   - N_FIELD_ACCESS whose base type resolves and whose selected field
 *     has a struct type (basic transitive case)                        */
static int type_of_expr(int node);
static int field_type(int sn, const char *fname, int flen)
{
    AstNode *s = &g_nodes[sn];
    for (int i = 0; i < s->nkids; i++) {
        AstNode *f = &g_nodes[s->kids[i]];
        if (f->kind == N_FIELD && f->slen == flen &&
            memcmp(f->s, fname, flen) == 0) {
            if (f->nkids > 0) {
                AstNode *ty = &g_nodes[f->kids[0]];
                if (ty->kind == N_TYPE && ty->s && ty->slen > 0) {
                    return find_struct(ty->s, ty->slen);
                }
            }
            return -1;
        }
    }
    return -1;
}
/* Helper: find the declared return type (as an N_STRUCT index) of a
 * function symbol, or -1 if not a struct.                              */
static int fn_return_struct(int sym_idx)
{
    if (sym_idx < 0 || sym_idx >= g_nsyms) return -1;
    AstNode *fn = &g_nodes[g_syms[sym_idx].node];
    if (fn->kind != N_FN && fn->kind != N_EXTERN_FN) return -1;
    /* Return type is the last N_TYPE child among kids[], just before body. */
    for (int i = fn->nkids - 1; i >= 0; i--) {
        AstNode *c = &g_nodes[fn->kids[i]];
        if (c->kind == N_TYPE && c->s && c->slen > 0) {
            return find_struct(c->s, c->slen);
        }
    }
    return -1;
}

static int type_of_expr(int node)
{
    AstNode *n = &g_nodes[node];
    if (n->kind == N_IDENT) {
        int li = local_lookup(n->s, n->slen);
        if (li >= 0 && g_locals[li].type_name) {
            return find_struct(g_locals[li].type_name,
                               g_locals[li].type_name_len);
        }
        return -1;
    }
    if (n->kind == N_STRUCT_LIT && n->slen > 0) {
        return find_struct(n->s, n->slen);
    }
    if (n->kind == N_FIELD_ACCESS) {
        int base_sn = type_of_expr(n->kids[0]);
        if (base_sn < 0) return -1;
        return field_type(base_sn, n->s, n->slen);
    }
    if (n->kind == N_CALL && n->nkids >= 1) {
        AstNode *c = &g_nodes[n->kids[0]];
        if (c->kind == N_IDENT) {
            int si = sym_find(c->s, c->slen);
            return fn_return_struct(si);
        }
    }
    if (n->kind == N_GROUP && n->nkids >= 1) {
        return type_of_expr(n->kids[0]);
    }
    return -1;
}

static void lower_field_access(int node)
{
    AstNode *n = &g_nodes[node];
    int obj = n->kids[0];
    lower_expr(obj);

    int offset = 0;
    int sn = type_of_expr(obj);
    if (sn >= 0) {
        int fidx = struct_field_index(sn, n->s, n->slen);
        if (fidx >= 0) offset = fidx * 8;
    }
    emit_mov_rax_rax_plus_imm32(offset);
}

static void lower_postfix_q(int node)
{
    AstNode *n = &g_nodes[node];
    lower_expr(n->kids[0]);
    emit_cmp_byte_mem_rax_0();
    int jne_slot = emit_jne_rel32();
    emit_mov_rax_rax_plus_8();
    int jmp_slot = emit_jmp_rel32();
    /* .dim: epilogue-return */
    int dim_here = g_code_len;
    emit_mov_rsp_rbp();
    emit_pop_rbp();
    emit_ret();
    int done_here = g_code_len;
    code_patch_u32(jne_slot, (uint32_t)(dim_here - (jne_slot + 4)));
    code_patch_u32(jmp_slot, (uint32_t)(done_here - (jmp_slot + 4)));
}

static void lower_bin(int node)
{
    AstNode *n = &g_nodes[node];
    int op = n->i1;
    int l = n->kids[0], r = n->kids[1];

    /* short-circuit for && and || */
    if (op == TK_ANDAND || op == TK_KW_AND) {
        lower_expr(l);
        emit_test_rax_rax();
        int je_slot = emit_je_rel32();
        lower_expr(r);
        emit_test_rax_rax();
        int je2_slot = emit_je_rel32();
        emit_mov_rax_imm64(1);
        int jmp_done = emit_jmp_rel32();
        int false_here = g_code_len;
        emit_mov_rax_imm64(0);
        int done_here = g_code_len;
        code_patch_u32(je_slot,  (uint32_t)(false_here - (je_slot + 4)));
        code_patch_u32(je2_slot, (uint32_t)(false_here - (je2_slot + 4)));
        code_patch_u32(jmp_done, (uint32_t)(done_here - (jmp_done + 4)));
        return;
    }
    if (op == TK_OROR || op == TK_KW_OR) {
        lower_expr(l);
        emit_test_rax_rax();
        int jne_slot = emit_jne_rel32();
        lower_expr(r);
        emit_test_rax_rax();
        int jne2_slot = emit_jne_rel32();
        emit_mov_rax_imm64(0);
        int jmp_done = emit_jmp_rel32();
        int true_here = g_code_len;
        emit_mov_rax_imm64(1);
        int done_here = g_code_len;
        code_patch_u32(jne_slot,  (uint32_t)(true_here - (jne_slot + 4)));
        code_patch_u32(jne2_slot, (uint32_t)(true_here - (jne2_slot + 4)));
        code_patch_u32(jmp_done,  (uint32_t)(done_here - (jmp_done + 4)));
        return;
    }

    lower_expr(l);
    emit_push_rax();
    lower_expr(r);
    emit_mov_rcx_rax();
    emit_pop_r(REG_RAX);

    switch (op) {
        case TK_PLUS:    emit_add_rax_rcx(); break;
        case TK_MINUS:   emit_sub_rax_rcx(); break;
        case TK_STAR:    emit_imul_rax_rcx(); break;
        case TK_SLASH:   emit_cqo(); emit_idiv_rcx(); break;
        case TK_PERCENT: emit_cqo(); emit_idiv_rcx();
                         /* remainder in RDX; move to RAX */
                         { uint8_t b[]={0x48,0x89,0xd0}; code_emit_bytes(b,3); }
                         break;
        /* Bitwise — rcx holds rhs, rax holds lhs */
        case TK_AMP:   { uint8_t b[]={0x48,0x21,0xc8}; code_emit_bytes(b,3); } break;  /* and rax,rcx */
        case TK_PIPE:  { uint8_t b[]={0x48,0x09,0xc8}; code_emit_bytes(b,3); } break;  /* or  rax,rcx */
        case TK_CARET: { uint8_t b[]={0x48,0x31,0xc8}; code_emit_bytes(b,3); } break;  /* xor rax,rcx */
        /* Shifts: count must be in CL; rcx already set, low 8 is cl */
        case TK_SHL:   { uint8_t b[]={0x48,0xd3,0xe0}; code_emit_bytes(b,3); } break;  /* shl rax,cl */
        case TK_SHR:   { uint8_t b[]={0x48,0xd3,0xe8}; code_emit_bytes(b,3); } break;  /* shr rax,cl */
        case TK_EQ:  emit_cmp_rax_rcx(); emit_setcc_al(0x94); emit_movzx_rax_al(); break;
        case TK_NE:  emit_cmp_rax_rcx(); emit_setcc_al(0x95); emit_movzx_rax_al(); break;
        case TK_LT:  emit_cmp_rax_rcx(); emit_setcc_al(0x9c); emit_movzx_rax_al(); break;
        case TK_LE:  emit_cmp_rax_rcx(); emit_setcc_al(0x9e); emit_movzx_rax_al(); break;
        case TK_GT:  emit_cmp_rax_rcx(); emit_setcc_al(0x9f); emit_movzx_rax_al(); break;
        case TK_GE:  emit_cmp_rax_rcx(); emit_setcc_al(0x9d); emit_movzx_rax_al(); break;
        default:
            die_unsup("binary op", g_files[n->file].path, n->line);
    }
}

/* Bootstrap-grade struct literal: evaluate each field value for side-effects
   (including string-intern as rodata), then return 0 in RAX.  The real
   compiler computes proper struct layout and returns the stack address of a
   freshly-allocated value; the bootstrap doesn't execute compiler internals
   so this is enough to get past parsing + codegen without crashing the
   build.  Programs that *actually* depend on the returned struct must be
   compiled with the self-hosted Luna compiler, not this bootstrap.        */
static void lower_struct_lit(int node)
{
    AstNode *n = &g_nodes[node];
    int sn = (n->s && n->slen > 0) ? find_struct(n->s, n->slen) : -1;

    int nfields = 0;
    if (sn >= 0) nfields = g_nodes[sn].nkids;
    else         nfields = n->nkids;

    int bytes = nfields * 8;
    if (bytes <= 0 || bytes > 1024 * 1024) {
        for (int i = 0; i < n->nkids; i++) {
            AstNode *fi = &g_nodes[n->kids[i]];
            if (fi->nkids > 0) lower_expr(fi->kids[0]);
        }
        emit_mov_rax_imm64(0);
        return;
    }

    /* Allocate on the heap so the pointer outlives the caller.  Park
     * the base in rbx, push the caller's rbx first so nested literals
     * nest cleanly — each level saves and restores its own copy.     */
    emit_push_r(3 /* rbx */);
    emit_bump_alloc(bytes);
    emit_mov_r_r(3 /* rbx */, REG_RAX);

    /* Zero every slot so missing initialisers read as 0 (the bump
     * allocator does not zero fresh memory by default).              */
    emit_mov_rax_imm64(0);
    for (int i = 0; i < nfields; i++) {
        emit_mov_mem_rbx_disp32_rax(i * 8);
    }

    if (sn < 0) {
        /* Anonymous / unknown struct — fill in user-supplied order.   */
        for (int i = 0; i < n->nkids; i++) {
            AstNode *fi = &g_nodes[n->kids[i]];
            if (fi->nkids > 0) lower_expr(fi->kids[0]);
            else emit_mov_rax_imm64(0);
            emit_mov_mem_rbx_disp32_rax(i * 8);
        }
    } else {
        /* Known struct — each initialiser lands at its declared slot. */
        for (int i = 0; i < n->nkids; i++) {
            AstNode *fi = &g_nodes[n->kids[i]];
            int fidx = struct_field_index(sn, fi->s, fi->slen);
            if (fidx < 0) {
                if (fi->nkids > 0) lower_expr(fi->kids[0]);
                continue;
            }
            if (fi->nkids > 0) lower_expr(fi->kids[0]);
            else emit_mov_rax_imm64(0);
            emit_mov_mem_rbx_disp32_rax(fidx * 8);
        }
    }

    emit_mov_r_r(REG_RAX, 3 /* rbx */);
    emit_pop_r(3 /* rbx */);
}

static void lower_expr(int node)
{
    AstNode *n = &g_nodes[node];
    switch (n->kind) {
        case N_INT:   emit_mov_rax_imm64((uint64_t)n->iv); return;
        case N_BOOL:  emit_mov_rax_imm64((uint64_t)(long long)n->i1); return;
        case N_NILV:  emit_mov_rax_imm64(0); return;
        case N_STR:   emit_lea_rax_rip_rodata(g_strs[n->str_idx].off); return;
        case N_IDENT: load_ident(n->s, n->slen, n->file, n->line); return;
        case N_GROUP: lower_expr(n->kids[0]); return;
        case N_BIN:   lower_bin(node); return;
        case N_UNARY:
            lower_expr(n->kids[0]);
            if (n->i1 == TK_KW_NOT || n->i1 == TK_BANG) {
                emit_test_rax_rax();
                emit_setcc_al(0x94); /* sete */
                emit_movzx_rax_al();
            } else if (n->i1 == TK_TILDE) {
                /* Bitwise NOT: not rax = 48 F7 D0 */
                uint8_t b[] = { 0x48, 0xf7, 0xd0 };
                code_emit_bytes(b, 3);
            } else if (n->i1 == TK_MINUS) {
                /* Negate: neg rax = 48 F7 D8 */
                uint8_t b[] = { 0x48, 0xf7, 0xd8 };
                code_emit_bytes(b, 3);
            }
            return;
        case N_POSTFIX_Q: lower_postfix_q(node); return;
        case N_CALL:   lower_call(node); return;
        case N_FIELD_ACCESS: lower_field_access(node); return;
        case N_IF: {
            /* Ternary if-expression: `if cond then A else B` */
            int cond = n->kids[0];
            int thn  = n->kids[1];
            int els  = (n->nkids >= 3) ? n->kids[2] : -1;
            lower_expr(cond);
            emit_test_rax_rax();
            int je_slot = emit_je_rel32();
            lower_expr(thn);
            int jmp_done = emit_jmp_rel32();
            int else_here = g_code_len;
            code_patch_u32(je_slot, (uint32_t)(else_here - (je_slot + 4)));
            if (els >= 0) lower_expr(els);
            else emit_mov_rax_imm64(0);
            int end_here = g_code_len;
            code_patch_u32(jmp_done, (uint32_t)(end_here - (jmp_done + 4)));
            return;
        }
        case N_STRUCT_LIT: lower_struct_lit(node); return;
        case N_ARRAY_LIT: {
            /* Two forms, disambiguated by i1:
             *   i1 == 0:  [v1, v2, ...]     — N elements, each evaluated
             *   i1 == 1:  [v; N]           — one value, repeated N times
             *
             * Both forms bump-allocate N*8 bytes from the global heap.
             * The heap pointer lives in a dedicated BSS slot; struct and
             * array literals consume it directly so the returned pointer
             * survives the enclosing function's return.                 */
            const int MAX_ARRAY_BYTES = 1024 * 1024;    /* 128 Ki slots */
            int count = 0;
            int is_repeat = n->i1;
            if (is_repeat) {
                if (n->nkids < 2) { emit_mov_rax_imm64(0); return; }
                AstNode *cn = &g_nodes[n->kids[1]];
                if (cn->kind != N_INT || cn->iv < 0) {
                    for (int i = 0; i < n->nkids; i++) lower_expr(n->kids[i]);
                    emit_mov_rax_imm64(0);
                    return;
                }
                count = (int)cn->iv;
            } else {
                count = n->nkids;
            }
            int bytes = count * 8;
            if (bytes <= 0 || bytes > MAX_ARRAY_BYTES) {
                for (int i = 0; i < n->nkids; i++) lower_expr(n->kids[i]);
                emit_mov_rax_imm64(0);
                return;
            }
            /* Save caller's rbx, park the fresh heap pointer there. */
            emit_push_r(3 /* rbx */);
            emit_bump_alloc(bytes);                /* rax = base */
            emit_mov_r_r(3 /* rbx */, REG_RAX);    /* rbx = base */
            if (is_repeat) {
                lower_expr(n->kids[0]);            /* rax = v */
                for (int i = 0; i < count; i++) {
                    emit_mov_mem_rbx_disp32_rax(i * 8);
                }
            } else {
                for (int i = 0; i < count; i++) {
                    lower_expr(n->kids[i]);
                    emit_mov_mem_rbx_disp32_rax(i * 8);
                }
            }
            emit_mov_r_r(REG_RAX, 3 /* rbx */);    /* rax = base */
            emit_pop_r(3 /* rbx */);
            return;
        }
        case N_INDEX: {
            /* arr[i] — read.  Evaluate base, push; evaluate index into rax;
             * pop base into rcx; mov rax, [rcx + rax*8].                  */
            lower_expr(n->kids[0]);           /* rax = base ptr           */
            emit_push_rax();
            lower_expr(n->kids[1]);           /* rax = index              */
            emit_pop_r(REG_RCX);              /* rcx = base               */
            emit_mov_rax_mem_rcx_rax_x8();    /* rax = [rcx + rax*8]      */
            return;
        }
        case N_MATCH_STUB:
            emit_mov_rax_imm64(0);
            return;
        case N_MATCH: {
            /* Lower `match SCRUT { P1 => B1, P2 => B2, _ => Bd }` as an
             * if-else chain over the stashed scrutinee.                */
            int scrut = n->kids[0];
            lower_expr(scrut);
            emit_push_rax();                        /* [rsp+0] = scrut */

            int done_slots[64];
            int ndone = 0;
            int narms = n->nkids - 1;

            for (int i = 0; i < narms; i++) {
                int arm = n->kids[1 + i];
                AstNode *a = &g_nodes[arm];
                if (a->nkids < 2) continue;
                int pat  = a->kids[0];
                int body = a->kids[1];
                AstNode *p = &g_nodes[pat];

                int skip_slot = -1;
                if (p->kind == N_INT) {
                    /* Reload scrut: mov rax, [rsp+0]  -> 48 8b 04 24  */
                    uint8_t b[] = { 0x48, 0x8b, 0x04, 0x24 };
                    code_emit_bytes(b, 4);
                    /* cmp rax, imm32 (sign-extended)  -> 48 3d imm32  */
                    uint8_t c2[] = { 0x48, 0x3d };
                    code_emit_bytes(c2, 2);
                    code_emit_u32((uint32_t)(int32_t)p->iv);
                    skip_slot = emit_jne_rel32();
                } else if (p->kind == N_IDENT) {
                    /* Bind @name to scrutinee: create a local and copy  */
                    int li = local_add(p->s, p->slen, 0, 0);
                    uint8_t b[] = { 0x48, 0x8b, 0x04, 0x24 };
                    code_emit_bytes(b, 4);
                    emit_mov_rbp_disp_rax(g_locals[li].offset);
                }
                /* N_PASS / fallback: always matches, no skip.           */

                lower_stmt(body);
                int d = emit_jmp_rel32();
                if (ndone < 64) done_slots[ndone++] = d;

                if (skip_slot >= 0) {
                    int here = g_code_len;
                    code_patch_u32(skip_slot, (uint32_t)(here - (skip_slot + 4)));
                }
            }

            int done_here = g_code_len;
            for (int i = 0; i < ndone; i++) {
                code_patch_u32(done_slots[i],
                               (uint32_t)(done_here - (done_slots[i] + 4)));
            }
            emit_add_rsp_imm8(8);                  /* drop scrutinee   */
            return;
        }
        default:
            die_unsup("expression kind", g_files[n->file].path, n->line);
    }
}

static void lower_if(int node)
{
    AstNode *n = &g_nodes[node];
    int cond = n->kids[0];
    int then_body = n->kids[1];
    int else_body = (n->nkids >= 3) ? n->kids[2] : -1;

    lower_expr(cond);
    emit_test_rax_rax();
    int je_slot = emit_je_rel32();
    lower_block(then_body);
    int jmp_end = -1;
    if (else_body >= 0) jmp_end = emit_jmp_rel32();
    int else_here = g_code_len;
    code_patch_u32(je_slot, (uint32_t)(else_here - (je_slot + 4)));
    if (else_body >= 0) {
        AstNode *eb = &g_nodes[else_body];
        if (eb->kind == N_IF) lower_stmt(else_body);
        else                   lower_block(else_body);
        int end_here = g_code_len;
        code_patch_u32(jmp_end, (uint32_t)(end_here - (jmp_end + 4)));
    }
}

static void lower_while(int node)
{
    AstNode *n = &g_nodes[node];
    int cond = n->kids[0], body = n->kids[1];
    int top = g_code_len;
    int save_break = g_nbreak_patch;
    g_loop_cont_target[g_loop_depth++] = top;
    lower_expr(cond);
    emit_test_rax_rax();
    int je_slot = emit_je_rel32();
    lower_block(body);
    int jmp_back = emit_jmp_rel32();
    code_patch_u32(jmp_back, (uint32_t)(top - (jmp_back + 4)));
    int end_here = g_code_len;
    code_patch_u32(je_slot, (uint32_t)(end_here - (je_slot + 4)));
    for (int i = save_break; i < g_nbreak_patch; i++) {
        code_patch_u32(g_loop_break_patch[i],
                       (uint32_t)(end_here - (g_loop_break_patch[i] + 4)));
    }
    g_nbreak_patch = save_break;
    g_loop_depth--;
}

static void lower_orbit(int node)
{
    AstNode *n = &g_nodes[node];
    int lo = n->kids[0], hi = n->kids[1], body = n->kids[2];

    /* Create loop variable as a local. */
    int li = local_add(n->s, n->slen, 0, 0);
    /* Initialise: @i = lo */
    lower_expr(lo);
    emit_mov_rbp_disp_rax(g_locals[li].offset);
    /* Stash hi into a scratch local too. */
    int hili = local_add("__hi", 4, 0, 0);
    lower_expr(hi);
    emit_mov_rbp_disp_rax(g_locals[hili].offset);

    int top = g_code_len;
    int save_break = g_nbreak_patch;
    g_loop_cont_target[g_loop_depth++] = top;
    /* if (i >= hi) break */
    emit_mov_rax_rbp_disp(g_locals[li].offset);
    /* load hi into rcx */
    emit_mov_r_rbp_disp(REG_RCX, g_locals[hili].offset);
    emit_cmp_rax_rcx();
    emit_setcc_al(0x9d); /* setge */
    emit_movzx_rax_al();
    emit_test_rax_rax();
    int je_end = emit_jne_rel32();  /* if ge, jump to end */
    lower_block(body);
    /* i++ */
    emit_mov_rax_rbp_disp(g_locals[li].offset);
    { uint8_t b[] = { 0x48, 0x83, 0xc0, 0x01 }; code_emit_bytes(b, 4); } /* add rax, 1 */
    emit_mov_rbp_disp_rax(g_locals[li].offset);
    int jback = emit_jmp_rel32();
    code_patch_u32(jback, (uint32_t)(top - (jback + 4)));
    int end_here = g_code_len;
    code_patch_u32(je_end, (uint32_t)(end_here - (je_end + 4)));
    for (int i = save_break; i < g_nbreak_patch; i++) {
        code_patch_u32(g_loop_break_patch[i],
                       (uint32_t)(end_here - (g_loop_break_patch[i] + 4)));
    }
    g_nbreak_patch = save_break;
    g_loop_depth--;
}

static void lower_stmt(int node)
{
    AstNode *n = &g_nodes[node];
    switch (n->kind) {
        case N_PASS: return;
        case N_BLOCK: lower_block(node); return;
        case N_EXPR_STMT: lower_expr(n->kids[0]); return;
        case N_RETURN:
            if (n->nkids >= 1) lower_expr(n->kids[0]);
            else emit_mov_rax_imm64(0);
            emit_mov_rsp_rbp();
            emit_pop_rbp();
            emit_ret();
            return;
        case N_LET: {
            int li = local_add(n->s, n->slen, 0, 0);
            int val = n->kids[n->nkids - 1];
            /* Propagate the declared or inferred struct type so later
             * field accesses compute real offsets.                    */
            if (n->nkids >= 2) {
                AstNode *ty = &g_nodes[n->kids[0]];
                if (ty->kind == N_TYPE && ty->s && ty->slen > 0 &&
                    find_struct(ty->s, ty->slen) >= 0) {
                    g_locals[li].type_name = ty->s;
                    g_locals[li].type_name_len = ty->slen;
                }
            }
            if (g_locals[li].type_name == NULL) {
                int sn = type_of_expr(val);
                if (sn >= 0) {
                    g_locals[li].type_name = g_nodes[sn].s;
                    g_locals[li].type_name_len = g_nodes[sn].slen;
                }
            }
            lower_expr(val);
            emit_mov_rbp_disp_rax(g_locals[li].offset);
            return;
        }
        case N_ASSIGN: {
            int lhs = n->kids[0], rhs = n->kids[1];
            AstNode *ln = &g_nodes[lhs];
            if (ln->kind == N_IDENT) {
                int li = local_lookup(ln->s, ln->slen);
                if (li < 0) {
                    /* Implicit let: in Luna, `@lex = Lexer { ... }` at the point
                       of first use binds a new local (there's no `let` keyword
                       required for `@`-prefixed identifiers).  The bootstrap
                       mirrors that rule to accept real Luna source. */
                    li = local_add(ln->s, ln->slen, 0, 0);
                }
                if (g_locals[li].type_name == NULL) {
                    int sn = type_of_expr(rhs);
                    if (sn >= 0) {
                        g_locals[li].type_name = g_nodes[sn].s;
                        g_locals[li].type_name_len = g_nodes[sn].slen;
                    }
                }
                lower_expr(rhs);
                emit_mov_rbp_disp_rax(g_locals[li].offset);
                return;
            }
            /* Index write `arr[i] = v`.  Evaluate rhs, push; evaluate base,
             * push; evaluate index into rax; pop base into rcx; pop rhs
             * into rdx; mov [rcx + rax*8], rdx.                         */
            if (ln->kind == N_INDEX) {
                lower_expr(rhs);
                emit_push_rax();                         /* rhs on stack */
                lower_expr(g_nodes[lhs].kids[0]);        /* rax = base   */
                emit_push_rax();                         /* base on stack*/
                lower_expr(g_nodes[lhs].kids[1]);        /* rax = index  */
                emit_pop_r(REG_RCX);                     /* rcx = base   */
                emit_pop_rdx();                          /* rdx = rhs    */
                emit_mov_mem_rcx_rax_x8_rdx();           /* store        */
                emit_mov_r_r(REG_RAX, REG_RDX);          /* rax = rhs    */
                return;
            }
            /* Field write `@p.x = v`.  Resolve the field offset from the
             * base's known struct type, evaluate base -> rcx, evaluate rhs
             * -> rax, store `mov [rcx + offset], rax`.                    */
            if (ln->kind == N_FIELD_ACCESS) {
                int base = g_nodes[lhs].kids[0];
                int sn = type_of_expr(base);
                int offset = 0;
                if (sn >= 0) {
                    int fidx = struct_field_index(sn, g_nodes[lhs].s,
                                                      g_nodes[lhs].slen);
                    if (fidx >= 0) offset = fidx * 8;
                }
                lower_expr(rhs);
                emit_push_rax();
                lower_expr(base);
                emit_mov_rcx_rax2();
                emit_pop_r(REG_RAX);
                /* mov [rcx + disp32], rax  -> 48 89 81 disp32           */
                if (offset >= -128 && offset <= 127) {
                    uint8_t b[] = { 0x48, 0x89, 0x41, (uint8_t)offset };
                    code_emit_bytes(b, 4);
                } else {
                    uint8_t b[] = { 0x48, 0x89, 0x81 };
                    code_emit_bytes(b, 3);
                    code_emit_u32((uint32_t)offset);
                }
                return;
            }
            die_unsup("assign target", g_files[n->file].path, n->line);
            return;
        }
        case N_IF: lower_if(node); return;
        case N_WHILE: lower_while(node); return;
        case N_ORBIT: lower_orbit(node); return;
        case N_BREAK: {
            if (g_loop_depth == 0) die_unsup("break outside loop", g_files[n->file].path, n->line);
            int slot = emit_jmp_rel32();
            if (g_nbreak_patch >= 32) { fprintf(stderr, "luna-boot: too many nested breaks\n"); exit(1); }
            g_loop_break_patch[g_nbreak_patch++] = slot;
            return;
        }
        case N_CONTINUE: {
            if (g_loop_depth == 0) die_unsup("continue outside loop", g_files[n->file].path, n->line);
            int slot = emit_jmp_rel32();
            int target = g_loop_cont_target[g_loop_depth - 1];
            code_patch_u32(slot, (uint32_t)(target - (slot + 4)));
            return;
        }
        case N_UNSAFE_BLOCK: lower_block(n->kids[0]); return;
        /* Tolerant fall-throughs: expression nodes used where a statement was
           expected.  We re-route through lower_expr and discard the rax. */
        case N_MATCH_STUB:
            /* Statement form of an unsupported match — safely no-op. */
            return;
        case N_MATCH:
            /* Use the expression lowering; discard its rax result.    */
            lower_expr(node);
            return;
        case N_INT:   case N_BOOL:   case N_NILV:  case N_STR:
        case N_IDENT: case N_GROUP:  case N_BIN:   case N_UNARY:
        case N_POSTFIX_Q:  case N_CALL:
        case N_FIELD_ACCESS: case N_INDEX:
        case N_STRUCT_LIT:   case N_ARRAY_LIT:
            lower_expr(node);
            return;
        default:
            die_unsup("stmt kind", g_files[n->file].path, n->line);
    }
}

static void lower_block(int node)
{
    AstNode *n = &g_nodes[node];
    int saved = g_nlocals;
    for (int i = 0; i < n->nkids; i++) {
        int c = n->kids[i];
        if (g_nodes[c].kind == N_BLOCK) lower_block(c);
        else                             lower_stmt(c);
    }
    /* locals go out of scope but we don't reclaim frame bytes in the
       bootstrap — simpler and correct.                                 */
    (void)saved;
}

static void lower_function(int sym_idx)
{
    Sym *s = &g_syms[sym_idx];
    AstNode *fn = &g_nodes[s->node];
    if (fn->kind != N_FN) return;

    s->code_off = g_code_len;
    g_cur_fn_sym = sym_idx;
    g_nlocals = 0;
    g_frame_size = 0;
    g_nbreak_patch = 0;
    g_loop_depth = 0;

    /* Prologue. */
    emit_push_rbp();
    emit_mov_rbp_rsp();
    /* Patch-site for sub rsp, N (frame size).  We don't know final size
       until we finish lowering the body, so reserve space.             */
    code_emit_byte(0x48);
    code_emit_byte(0x81);
    code_emit_byte(0xec);
    int frame_patch = g_code_len;
    code_emit_u32(0);

    /* Spill arguments into local stack slots.  Params are children 0..N-1
       of the fn node (last two kids are ret-type + body).                */
    int nparams = 0;
    for (int i = 0; i < fn->nkids; i++) {
        if (g_nodes[fn->kids[i]].kind == N_PARAM) nparams++;
    }
    int body_idx = fn->kids[fn->nkids - 1];
    int pi = 0;
    for (int i = 0; i < fn->nkids; i++) {
        AstNode *c = &g_nodes[fn->kids[i]];
        if (c->kind != N_PARAM) continue;
        int li = local_add(c->s, c->slen, 1, pi);
        if (c->nkids > 0) {
            AstNode *ty = &g_nodes[c->kids[0]];
            if (ty->kind == N_TYPE && ty->s && ty->slen > 0 &&
                find_struct(ty->s, ty->slen) >= 0) {
                g_locals[li].type_name = ty->s;
                g_locals[li].type_name_len = ty->slen;
            }
        }
        if (pi < 6) {
            /* Register-passed: spill to the local's stack slot.         */
            int r = sysv_argreg(pi);
            emit_mov_rbp_disp_r(r, g_locals[li].offset);
        } else {
            /* Stack-passed argument.  Caller placed arg6 at [rbp+16],
             * arg7 at [rbp+24], ... (offset = 16 + (pi-6)*8).  Load
             * into rax and store to the local slot.                    */
            int src_disp = 16 + (pi - 6) * 8;
            emit_mov_rax_rbp_disp(src_disp);
            emit_mov_rbp_disp_rax(g_locals[li].offset);
        }
        pi++;
    }
    (void)nparams;

    /* Body. */
    lower_block(body_idx);

    /* Implicit return 0 if we fall off the end. */
    emit_mov_rax_imm64(0);
    emit_mov_rsp_rbp();
    emit_pop_rbp();
    emit_ret();

    /* Patch frame size (round up to 16). */
    uint32_t fs = (uint32_t)((g_frame_size + 15) & ~15);
    code_patch_u32(frame_patch, fs);
}

/* When compiling a library module (no main), we still emit every function
   into the ELF so the file is syntactically valid; we just install a tiny
   "_start: exit(0)" as the entry point.  The user-visible flag is `--lib`
   or implicit when we don't find a main and the caller asked for -c mode. */
extern int g_allow_no_main;

static void lower_all(void)
{
    int main_sym = sym_find("main", 4);
    int call_slot = -1, main_code = -1;

    /* Emit _start.  The shape depends on the output target: Linux uses the
     * exit_group syscall, Windows routes through ExitProcess in the IAT.  */
    int start_off = g_code_len;
    /* On Linux the kernel puts argc at [rsp] and argv at [rsp+8] when it
     * jumps to our entry point.  Snapshot them into BSS slots before the
     * prologue perturbs rsp so arg(i) / args_count() can find them.     */
    if (g_target == TARGET_LINUX) {
        /* mov rax, [rsp]          -> 48 8b 04 24               */
        { uint8_t b[] = { 0x48, 0x8b, 0x04, 0x24 }; code_emit_bytes(b, 4); }
        emit_mov_mem_rip_rax_bss(BSS_ARGC_OFF);
        /* lea rax, [rsp+8]        -> 48 8d 44 24 08            */
        { uint8_t b[] = { 0x48, 0x8d, 0x44, 0x24, 0x08 }; code_emit_bytes(b, 5); }
        emit_mov_mem_rip_rax_bss(BSS_ARGV_OFF);
    }
    /* Initialise the bump-allocator: _luna_heap_top = &_luna_heap_base.
     * This runs before main() so every struct / array literal has a live
     * heap to draw from.                                                */
    emit_lea_rax_rip_bss(BSS_HEAP_BASE_OFF);
    emit_mov_mem_rip_rax_bss(BSS_HEAP_TOP_OFF);

    /* On Windows populate argc / argv via msvcrt's __getmainargs.       */
    if (g_target == TARGET_WINDOWS) {
        int gma_slot = ffi_register("__getmainargs", 13);
        /* Reserve 72 bytes: 32 shadow + 8 slot for 5th stack arg +
         * 8 env out-param + 16 _startupinfo struct + 8 pad.  With no
         * preceding push, rsp is 16-aligned inside the function; 72 %
         * 16 == 8 so rsp stays 16-aligned for the call.                 */
        emit_sub_rsp_imm8(72);
        /* Zero scratch slots.                                          */
        emit_mov_rax_imm64(0);
        emit_mov_rsp_disp8_r(40, REG_RAX);     /* env              = 0 */
        emit_mov_rsp_disp8_r(48, REG_RAX);     /* startupinfo.newmode=0*/
        emit_mov_rsp_disp8_r(56, REG_RAX);

        /* rcx = &argc_slot */
        emit_lea_rax_rip_bss(BSS_ARGC_OFF);
        emit_mov_r_r(REG_RCX, REG_RAX);
        /* rdx = &argv_slot */
        emit_lea_rax_rip_bss(BSS_ARGV_OFF);
        emit_mov_r_r(REG_RDX, REG_RAX);
        /* r8 = &env_slot (scratch at [rsp+40]) */
        /* lea r8, [rsp+40] -> 4C 8D 44 24 28                            */
        { uint8_t b[] = { 0x4c, 0x8d, 0x44, 0x24, 40 }; code_emit_bytes(b, 5); }
        /* r9 = 0 (no wildcard expand) */
        emit_mov_r_imm64(9, 0);
        /* [rsp+32] = &startupinfo at [rsp+48]                          */
        /* lea rax, [rsp+48] -> 48 8D 44 24 30                           */
        { uint8_t b[] = { 0x48, 0x8d, 0x44, 0x24, 48 }; code_emit_bytes(b, 5); }
        emit_mov_rsp_disp8_r(32, REG_RAX);
        emit_call_iat(gma_slot);
        emit_add_rsp_imm8(72);
    }

    if (g_target == TARGET_WINDOWS) {
        /* Win64 _start: sub rsp,40 (align+shadow) ; call main ; mov rcx,rax
         *               ; call [iat_ExitProcess]                            */
        emit_sub_rsp_imm8(40);
        if (main_sym >= 0) {
            call_slot = emit_call_rel32();
            /* mov rcx, rax — pass main's return as exit code */
            { uint8_t b[] = { 0x48, 0x89, 0xc1 }; code_emit_bytes(b, 3); }
        } else if (g_allow_no_main) {
            emit_mov_r_imm64(REG_RCX, 0);
        } else {
            fprintf(stderr, "luna-boot: no 'main' function defined (use --lib to allow)\n");
            exit(1);
        }
        emit_call_iat(IAT_EXITPROCESS);
        /* ExitProcess is noreturn — trap if we fall through anyway.        */
        emit_int3();
    } else {
        /* Linux: call main (if any), then exit_group(rax_from_main).       */
        if (main_sym >= 0) {
            call_slot = emit_call_rel32();
            /* mov rdi, rax */
            { uint8_t b[] = { 0x48, 0x89, 0xc7 }; code_emit_bytes(b, 3); }
        } else if (g_allow_no_main) {
            emit_mov_rax_imm64(0);
            { uint8_t b[] = { 0x48, 0x89, 0xc7 }; code_emit_bytes(b, 3); }
        } else {
            fprintf(stderr, "luna-boot: no 'main' function defined (use --lib to allow)\n");
            exit(1);
        }
        emit_mov_rax_imm64((uint64_t)SYS_EXIT_GROUP);
        emit_syscall();
    }

    /* Emit main first so call_slot is easy to patch. */
    if (main_sym >= 0) {
        main_code = g_code_len;
        lower_function(main_sym);
        code_patch_u32(call_slot, (uint32_t)(main_code - (call_slot + 4)));
    }

    /* Emit every other N_FN. */
    for (int i = 0; i < g_nsyms; i++) {
        if (i == main_sym) continue;
        if (g_syms[i].kind != N_FN) continue;
        lower_function(i);
    }

    /* Apply inter-function relocations.  An unresolved symbol — typically
       an `extern "stdcall" fn CreateFileA` whose actual import table lives
       in the PE writer, not in our simple ELF — gets its displacement left
       as zero so the emitted `call rel32` becomes `call .+0`, a harmless
       push-and-continue at runtime.  Bootstrap's job is to produce a
       structurally-valid ELF; running aether's Windows syscalls needs
       the self-hosted compiler + PE backend.                             */
    for (int i = 0; i < g_nrelocs; i++) {
        Reloc *r = &g_relocs[i];
        Sym *t = &g_syms[r->target_fn];
        if (t->code_off < 0) {
            code_patch_u32(r->code_off, 0);
            continue;
        }
        int32_t disp = t->code_off - r->from_off;
        code_patch_u32(r->code_off, (uint32_t)disp);
    }

    (void)start_off;
}

/* ========================================================================= */
/* 9. ELF WRITER                                                              */
/* ========================================================================= */
/*
 * Minimal ELF64 writer.  Layout:
 *      +0x0000   ELF header (64 bytes)
 *      +0x0040   one program header (PT_LOAD, 56 bytes)
 *      +0x1000   .text  (machine code)                   mapped at 0x401000
 *      +0x1000+N rodata (string literals, etc)
 *      +end      section headers + shstrtab (optional, for `readelf`)
 *
 * Virtual base: 0x400000.  Entry point is the first byte of .text.
 */

#define V_BASE   0x400000
#define V_TEXT   0x401000
#define V_BSS    0x500000       /* second PT_LOAD — zero-filled heap    */

static void write_u8 (FILE *f, uint8_t  v) { fputc(v, f); }
static void write_u16(FILE *f, uint16_t v) {
    fputc(v & 0xff, f); fputc((v>>8) & 0xff, f);
}
static void write_u32(FILE *f, uint32_t v) {
    for (int i = 0; i < 4; i++) fputc((v >> (8*i)) & 0xff, f);
}
static void write_u64(FILE *f, uint64_t v) {
    for (int i = 0; i < 8; i++) fputc((v >> (8*i)) & 0xff, f);
}

/* Round up `v` to a multiple of `a` (a must be a power of two). */
static uint64_t round_up_u64(uint64_t v, uint64_t a) { return (v + a - 1) & ~(a - 1); }

static void write_elf64(const char *out_path)
{
    int text_len = g_code_len;
    int has_ffi  = (g_nlinux_imports > 0);

    /* Patch RIP-relative rodata references.                           */
    int rodata_off_in_file = text_len;
    for (int i = 0; i < g_nrodrelocs; i++) {
        RodataReloc *r = &g_rodrelocs[i];
        int text_vaddr_of_rip = V_TEXT + r->from_rip;
        int rodata_vaddr = V_TEXT + rodata_off_in_file + r->rodata_off;
        int32_t disp = rodata_vaddr - text_vaddr_of_rip;
        code_patch_u32(r->code_off, (uint32_t)disp);
    }
    /* Patch BSS references (heap top/base).                           */
    for (int i = 0; i < g_nbssrelocs; i++) {
        BssReloc *r = &g_bssrelocs[i];
        int rip    = V_TEXT + r->code_off + 4;
        int target = V_BSS  + r->bss_off;
        int32_t disp = (int32_t)(target - rip);
        code_patch_u32(r->code_off, (uint32_t)disp);
    }

    uint64_t ehdr_size = 64;
    uint64_t phdr_size = 56;
    uint64_t text_file_off = 0x1000;
    uint64_t total_text_rod = (uint64_t)text_len + (uint64_t)g_rodata_len;

    /* ---------- Static-link fast path (no libc, no dynamic segment) ---------- */
    if (!has_ffi) {
        FILE *f = fopen(out_path, "wb");
        if (!f) { fprintf(stderr, "luna-boot: cannot open '%s'\n", out_path); exit(1); }

        write_u8(f, 0x7f); write_u8(f, 'E'); write_u8(f, 'L'); write_u8(f, 'F');
        write_u8(f, 2); write_u8(f, 1); write_u8(f, 1);
        write_u8(f, 0); write_u8(f, 0);
        for (int i = 0; i < 7; i++) write_u8(f, 0);
        write_u16(f, 2); write_u16(f, 62); write_u32(f, 1);
        write_u64(f, V_TEXT); write_u64(f, ehdr_size); write_u64(f, 0); write_u32(f, 0);
        write_u16(f, (uint16_t)ehdr_size); write_u16(f, (uint16_t)phdr_size);
        write_u16(f, 2); write_u16(f, 0); write_u16(f, 0); write_u16(f, 0);

        /* PT_LOAD text + rodata */
        write_u32(f, 1); write_u32(f, 5);
        write_u64(f, text_file_off); write_u64(f, V_TEXT); write_u64(f, V_TEXT);
        write_u64(f, total_text_rod); write_u64(f, total_text_rod); write_u64(f, 0x1000);

        /* PT_LOAD bss */
        write_u32(f, 1); write_u32(f, 6);
        write_u64(f, 0); write_u64(f, V_BSS); write_u64(f, V_BSS);
        write_u64(f, 0); write_u64(f, BSS_BYTES); write_u64(f, 0x1000);

        long pos = ftell(f);
        for (long p = pos; p < (long)text_file_off; p++) fputc(0, f);
        fwrite(g_code, 1, (size_t)text_len, f);
        if (g_rodata_len > 0) fwrite(g_rodata, 1, (size_t)g_rodata_len, f);
        fclose(f);
        if (chmod(out_path, 0755) != 0) { /* non-fatal */ }
        return;
    }

    /* ---------- Dynamic-link path: libc.so.6 via DT_NEEDED ---------- */

    /* Decide which shared libraries the binary will pull in.  libc.so.6
     * is unconditional.  Additional DT_NEEDED entries are added when
     * imported symbol names belong to a known library (curl_*, ssl_*).  */
    const char *needed_libs[8];
    int n_needed = 0;
    needed_libs[n_needed++] = "libc.so.6";
    int need_curl = 0;
    for (int i = 0; i < g_nlinux_imports; i++) {
        const char *nm = g_linux_imports[i];
        if (strncmp(nm, "curl_", 5) == 0) { need_curl = 1; }
    }
    if (need_curl) needed_libs[n_needed++] = "libcurl.so.4";

    /* Build .dynstr: NUL, each needed-lib name, each import symbol name. */
    static uint8_t dynstr_buf[8192];
    int dynstr_len = 1;
    dynstr_buf[0] = 0;
    int lib_name_off[8];
    for (int L = 0; L < n_needed; L++) {
        lib_name_off[L] = dynstr_len;
        int nl = (int)strlen(needed_libs[L]);
        memcpy(dynstr_buf + dynstr_len, needed_libs[L], (size_t)nl);
        dynstr_len += nl;
        dynstr_buf[dynstr_len++] = 0;
    }
    int sym_name_off[MAX_LINUX_IMPORTS];
    for (int i = 0; i < g_nlinux_imports; i++) {
        sym_name_off[i] = dynstr_len;
        int nl = (int)strlen(g_linux_imports[i]);
        if (dynstr_len + nl + 1 > (int)sizeof(dynstr_buf)) {
            fprintf(stderr, "luna-boot: .dynstr overflow\n"); exit(1);
        }
        memcpy(dynstr_buf + dynstr_len, g_linux_imports[i], (size_t)nl);
        dynstr_len += nl;
        dynstr_buf[dynstr_len++] = 0;
    }

    /* Build .dynsym.  Entry 0 is the reserved null symbol; entries
     * 1..N describe each imported function — undefined, global, func. */
    int nsyms = 1 + g_nlinux_imports;
    static uint8_t dynsym_buf[4096];
    memset(dynsym_buf, 0, 24);                         /* null symbol  */
    for (int i = 0; i < g_nlinux_imports; i++) {
        uint8_t *p = dynsym_buf + 24 + i * 24;
        /* st_name (4), st_info (1), st_other (1), st_shndx (2),
         * st_value (8), st_size (8)                                   */
        uint32_t name_off = (uint32_t)sym_name_off[i];
        for (int b = 0; b < 4; b++) p[b] = (uint8_t)(name_off >> (b * 8));
        p[4] = 0x12;          /* (STB_GLOBAL << 4) | STT_FUNC          */
        p[5] = 0;             /* st_other                              */
        p[6] = 0; p[7] = 0;   /* st_shndx = SHN_UNDEF                  */
        for (int b = 0; b < 16; b++) p[8 + b] = 0;     /* value + size */
    }
    int dynsym_size = nsyms * 24;

    /* Build .rela.dyn: one R_X86_64_GLOB_DAT per import, pointing at
     * the matching GOT slot.                                           */
    static uint8_t rela_buf[4096];
    int rela_entries = g_nlinux_imports;
    /* Each entry: r_offset (8) + r_info (8) + r_addend (8)            */
    int rela_size = rela_entries * 24;

    /* Minimal DT_GNU_HASH stub — we export nothing, so nbuckets=1 and
     * the single bucket contains 0 (no exported symbols here).         */
    static uint8_t hash_buf[64];
    memset(hash_buf, 0, 32);
    /* nbuckets = 1  */  hash_buf[0] = 1;
    /* symndx    = nsyms */
    {
        uint32_t sx = (uint32_t)nsyms;
        for (int b = 0; b < 4; b++) hash_buf[4 + b] = (uint8_t)(sx >> (b * 8));
    }
    /* maskwords = 1 */  hash_buf[8] = 1;
    /* shift2    = 0 */
    /* bloom[0]  = 0 (matches nothing) — already zero */
    /* bucket[0] = 0 (empty bucket) — already zero */
    int hash_size = 16 + 8 + 4;                         /* 28 bytes    */

    /* Lay out the dynamic segment.  Offsets inside the RW page.       */
    int dynstr_rel   = 0;
    int dynsym_rel   = (int)round_up_u64((uint64_t)(dynstr_rel + dynstr_len), 8);
    int rela_rel     = (int)round_up_u64((uint64_t)(dynsym_rel + dynsym_size), 8);
    int hash_rel     = (int)round_up_u64((uint64_t)(rela_rel + rela_size), 8);
    int dynamic_rel  = (int)round_up_u64((uint64_t)(hash_rel + hash_size), 8);
    int ndyn = 10 + n_needed;          /* 10 fixed DT_* + one per lib  */
    int dynamic_size = ndyn * 16;
    int got_rel      = (int)round_up_u64((uint64_t)(dynamic_rel + dynamic_size), 8);
    int got_size     = g_nlinux_imports * 8;
    int rw_total     = got_rel + got_size;

    /* Place everything at page boundaries.  RW page comes after text. */
    uint64_t text_end_file = text_file_off + total_text_rod;
    uint64_t rw_file_off   = round_up_u64(text_end_file, 0x1000);
    uint64_t rw_va         = V_BASE + rw_file_off;
    uint64_t dynstr_va     = rw_va + dynstr_rel;
    uint64_t dynsym_va     = rw_va + dynsym_rel;
    uint64_t rela_va       = rw_va + rela_rel;
    uint64_t hash_va       = rw_va + hash_rel;
    uint64_t dynamic_va    = rw_va + dynamic_rel;
    uint64_t got_va        = rw_va + got_rel;

    /* Fill .rela.dyn now that we know got_va.                          */
    for (int i = 0; i < rela_entries; i++) {
        uint8_t *p = rela_buf + i * 24;
        uint64_t off = got_va + (uint64_t)i * 8;
        for (int b = 0; b < 8; b++) p[b] = (uint8_t)(off >> (b * 8));
        /* r_info = (sym_idx << 32) | R_X86_64_GLOB_DAT(6)              */
        uint64_t info = ((uint64_t)(i + 1) << 32) | 6;
        for (int b = 0; b < 8; b++) p[8 + b] = (uint8_t)(info >> (b * 8));
        /* r_addend = 0 */
        for (int b = 0; b < 8; b++) p[16 + b] = 0;
    }

    /* Build .dynamic buffer.                                           */
    static uint8_t dynamic_buf[256];
    int dyi = 0;
    #define DT_ENTRY(tag, val) do { \
        uint64_t t = (uint64_t)(tag); \
        uint64_t v = (uint64_t)(val); \
        for (int b = 0; b < 8; b++) dynamic_buf[dyi + b] = (uint8_t)(t >> (b * 8)); \
        for (int b = 0; b < 8; b++) dynamic_buf[dyi + 8 + b] = (uint8_t)(v >> (b * 8)); \
        dyi += 16; \
    } while (0)
    for (int L = 0; L < n_needed; L++) {
        DT_ENTRY(1, lib_name_off[L]);   /* DT_NEEDED per library        */
    }
    DT_ENTRY(5,  dynstr_va);            /* DT_STRTAB                    */
    DT_ENTRY(10, dynstr_len);           /* DT_STRSZ                     */
    DT_ENTRY(6,  dynsym_va);            /* DT_SYMTAB                    */
    DT_ENTRY(11, 24);                   /* DT_SYMENT                    */
    DT_ENTRY(7,  rela_va);              /* DT_RELA                      */
    DT_ENTRY(8,  rela_size);            /* DT_RELASZ                    */
    DT_ENTRY(9,  24);                   /* DT_RELAENT                   */
    DT_ENTRY(0x6ffffef5, hash_va);      /* DT_GNU_HASH                  */
    DT_ENTRY(0x6ffffff9, 0);            /* DT_RELACOUNT = 0             */
    DT_ENTRY(0,  0);                    /* DT_NULL                      */
    #undef DT_ENTRY

    /* Patch GOT relocations in code.                                   */
    for (int i = 0; i < g_ngotrelocs; i++) {
        GotReloc *r = &g_gotrelocs[i];
        uint64_t rip    = V_TEXT + r->code_off + 4;
        uint64_t target = got_va + (uint64_t)r->got_slot * 8;
        int32_t disp = (int32_t)((int64_t)target - (int64_t)rip);
        code_patch_u32(r->code_off, (uint32_t)disp);
    }

    /* ---- Header layout ---- */
    const char *INTERP = "/lib64/ld-linux-x86-64.so.2";
    int interp_len = (int)strlen(INTERP) + 1;
    int nphdrs = 6;                     /* INTERP + 3 LOAD + BSS + DYN */
    uint64_t phdrs_size = phdr_size * nphdrs;
    uint64_t interp_file_off = ehdr_size + phdrs_size;
    uint64_t interp_va = V_BASE + interp_file_off;

    FILE *f = fopen(out_path, "wb");
    if (!f) { fprintf(stderr, "luna-boot: cannot open '%s'\n", out_path); exit(1); }

    /* ELF header. */
    write_u8(f, 0x7f); write_u8(f, 'E'); write_u8(f, 'L'); write_u8(f, 'F');
    write_u8(f, 2); write_u8(f, 1); write_u8(f, 1);
    write_u8(f, 0); write_u8(f, 0);
    for (int i = 0; i < 7; i++) write_u8(f, 0);
    write_u16(f, 2);                    /* e_type = ET_EXEC             */
    write_u16(f, 62);
    write_u32(f, 1);
    write_u64(f, V_TEXT);
    write_u64(f, ehdr_size);
    write_u64(f, 0);
    write_u32(f, 0);
    write_u16(f, (uint16_t)ehdr_size);
    write_u16(f, (uint16_t)phdr_size);
    write_u16(f, (uint16_t)nphdrs);
    write_u16(f, 0); write_u16(f, 0); write_u16(f, 0);

    /* PT_INTERP */
    write_u32(f, 3);                     /* PT_INTERP                    */
    write_u32(f, 4);                     /* PF_R                         */
    write_u64(f, interp_file_off);
    write_u64(f, interp_va);
    write_u64(f, interp_va);
    write_u64(f, (uint64_t)interp_len);
    write_u64(f, (uint64_t)interp_len);
    write_u64(f, 1);

    /* PT_LOAD: headers + .interp (R--) — covers the first page        */
    write_u32(f, 1);
    write_u32(f, 4);                     /* PF_R                         */
    write_u64(f, 0);
    write_u64(f, V_BASE);
    write_u64(f, V_BASE);
    write_u64(f, text_file_off);         /* everything before 0x1000    */
    write_u64(f, text_file_off);
    write_u64(f, 0x1000);

    /* PT_LOAD: .text + .rodata (R-X)                                   */
    write_u32(f, 1);
    write_u32(f, 5);
    write_u64(f, text_file_off);
    write_u64(f, V_TEXT);
    write_u64(f, V_TEXT);
    write_u64(f, total_text_rod);
    write_u64(f, total_text_rod);
    write_u64(f, 0x1000);

    /* PT_LOAD: dynamic segment (RW)                                    */
    write_u32(f, 1);
    write_u32(f, 6);                     /* PF_R | PF_W                  */
    write_u64(f, rw_file_off);
    write_u64(f, rw_va);
    write_u64(f, rw_va);
    write_u64(f, (uint64_t)rw_total);
    write_u64(f, (uint64_t)rw_total);
    write_u64(f, 0x1000);

    /* PT_LOAD: .bss heap (RW) — same as the static path.               */
    write_u32(f, 1);
    write_u32(f, 6);
    write_u64(f, 0);
    write_u64(f, V_BSS);
    write_u64(f, V_BSS);
    write_u64(f, 0);
    write_u64(f, BSS_BYTES);
    write_u64(f, 0x1000);

    /* PT_DYNAMIC */
    write_u32(f, 2);                     /* PT_DYNAMIC                   */
    write_u32(f, 6);
    write_u64(f, rw_file_off + (uint64_t)dynamic_rel);
    write_u64(f, dynamic_va);
    write_u64(f, dynamic_va);
    write_u64(f, (uint64_t)dynamic_size);
    write_u64(f, (uint64_t)dynamic_size);
    write_u64(f, 8);

    /* .interp data and padding to text page. */
    fwrite(INTERP, 1, (size_t)interp_len, f);
    long pos = ftell(f);
    for (long p = pos; p < (long)text_file_off; p++) fputc(0, f);

    /* Text + rodata. */
    fwrite(g_code, 1, (size_t)text_len, f);
    if (g_rodata_len > 0) fwrite(g_rodata, 1, (size_t)g_rodata_len, f);

    /* Pad to RW page. */
    pos = ftell(f);
    for (long p = pos; p < (long)rw_file_off; p++) fputc(0, f);

    /* Emit dynamic segment contents in laid-out order. */
    fwrite(dynstr_buf, 1, (size_t)dynstr_len, f);
    pos = ftell(f);
    for (long p = pos; p < (long)(rw_file_off + (uint64_t)dynsym_rel); p++) fputc(0, f);
    fwrite(dynsym_buf, 1, (size_t)dynsym_size, f);
    pos = ftell(f);
    for (long p = pos; p < (long)(rw_file_off + (uint64_t)rela_rel); p++) fputc(0, f);
    fwrite(rela_buf, 1, (size_t)rela_size, f);
    pos = ftell(f);
    for (long p = pos; p < (long)(rw_file_off + (uint64_t)hash_rel); p++) fputc(0, f);
    fwrite(hash_buf, 1, (size_t)hash_size, f);
    pos = ftell(f);
    for (long p = pos; p < (long)(rw_file_off + (uint64_t)dynamic_rel); p++) fputc(0, f);
    fwrite(dynamic_buf, 1, (size_t)dynamic_size, f);
    pos = ftell(f);
    for (long p = pos; p < (long)(rw_file_off + (uint64_t)got_rel); p++) fputc(0, f);
    for (int i = 0; i < got_size; i++) fputc(0, f);

    fclose(f);
    if (chmod(out_path, 0755) != 0) { /* non-fatal */ }
}

/* ========================================================================= */
/* 9b. MINIMAL PE64 WRITER (Windows x86-64 executable)                        */
/* ========================================================================= */
/*
 * File layout:
 *     +0x000   DOS header (0x40 bytes, sets e_lfanew = 0x40)
 *     +0x040   "PE\0\0"
 *     +0x044   COFF header (20 bytes)
 *     +0x058   Optional PE32+ header (240 bytes: 112 std/win + 128 data dirs)
 *     +0x148   Section headers: .text (40) + .rdata (40)
 *     +0x1a0   padding to FILE_ALIGNMENT
 *     +0x200   .text   (code, padded to FILE_ALIGNMENT)
 *     +X       .rdata  (rodata || IAT || ILT || hint/name || DLL name || import dir)
 *
 * Virtual layout:
 *     ImageBase = 0x140000000
 *     .text  at RVA 0x1000
 *     .rdata at RVA 0x1000 + round_up(text_vsize, SECTION_ALIGNMENT)
 *
 * Subsystem: IMAGE_SUBSYSTEM_WINDOWS_CUI (console).
 * Imports:   kernel32.dll — GetStdHandle, WriteFile, ExitProcess.
 */

#define PE_IMAGE_BASE     0x140000000ULL
#define PE_FILE_ALIGN     0x200
#define PE_SECTION_ALIGN  0x1000

static uint32_t pe_align_up(uint32_t v, uint32_t a)
{
    return (v + a - 1) & ~(a - 1);
}

static void write_pe64(const char *out_path)
{
    /* ---- Build .rdata contents (rodata | imports).  We need to know the
     * final .rdata RVA before patching any code relocs.  Imports are
     * grouped per DLL: each DLL gets its own ILT / IAT pair terminated
     * by a zero qword.                                                  */
    int n_imports = g_nimports;

    /* Collect unique DLLs in first-appearance order.                    */
    #define MAX_DLLS 16
    const char *dll_names[MAX_DLLS];
    int n_dlls = 0;
    int dll_of[MAX_IMPORTS];          /* DLL index per global import    */
    int pos_in_dll[MAX_IMPORTS];      /* position within that DLL's IAT */
    int count_per_dll[MAX_DLLS] = {0};
    for (int i = 0; i < n_imports; i++) {
        const char *dn = g_imports[i].dll_name;
        int di = -1;
        for (int j = 0; j < n_dlls; j++) {
            if (strcmp(dll_names[j], dn) == 0) { di = j; break; }
        }
        if (di < 0) {
            if (n_dlls >= MAX_DLLS) {
                fprintf(stderr, "luna-boot: too many distinct FFI DLLs\n"); exit(1);
            }
            di = n_dlls;
            dll_names[n_dlls++] = dn;
        }
        dll_of[i] = di;
        pos_in_dll[i] = count_per_dll[di]++;
    }

    int user_rod_size = g_rodata_len;
    int cur = pe_align_up((uint32_t)user_rod_size, 8);

    /* Per-DLL IAT and ILT start offsets.                                */
    int iat_start[MAX_DLLS];
    int ilt_start[MAX_DLLS];
    for (int d = 0; d < n_dlls; d++) {
        iat_start[d] = cur;
        cur += (count_per_dll[d] + 1) * 8;
    }
    int first_iat_off = iat_start[0];
    int iat_total_bytes = cur - first_iat_off;
    for (int d = 0; d < n_dlls; d++) {
        ilt_start[d] = cur;
        cur += (count_per_dll[d] + 1) * 8;
    }

    int hint_off[MAX_IMPORTS];
    for (int i = 0; i < n_imports; i++) {
        hint_off[i] = cur;
        int nl = (int)strlen(g_imports[i].fn_name);
        int size = 2 + nl + 1;
        if (size & 1) size++;
        cur += size;
    }

    int dll_name_off[MAX_DLLS];
    for (int d = 0; d < n_dlls; d++) {
        dll_name_off[d] = cur;
        cur += (int)strlen(dll_names[d]) + 1;
    }
    cur = pe_align_up((uint32_t)cur, 4);

    int import_dir_off = cur;
    cur += (n_dlls + 1) * 20;          /* N descriptors + zero terminator */

    int total_rdata = cur;

    if (g_rodata_len + total_rdata - user_rod_size > RODATA_CAP) {
        fprintf(stderr, "luna-boot: rodata overflow in PE writer\n"); exit(1);
    }
    for (int i = user_rod_size; i < (n_dlls > 0 ? iat_start[0] : cur); i++)
        g_rodata[i] = 0;

    int text_len = g_code_len;
    uint32_t text_file_off   = PE_FILE_ALIGN;
    uint32_t text_file_size  = pe_align_up((uint32_t)text_len, PE_FILE_ALIGN);
    uint32_t text_vsize      = pe_align_up((uint32_t)text_len, PE_SECTION_ALIGN);
    uint32_t text_rva        = PE_SECTION_ALIGN;
    uint32_t rdata_rva       = text_rva + text_vsize;
    uint32_t rdata_file_off  = text_file_off + text_file_size;
    uint32_t rdata_file_size = pe_align_up((uint32_t)total_rdata, PE_FILE_ALIGN);
    uint32_t rdata_vsize     = pe_align_up((uint32_t)total_rdata, PE_SECTION_ALIGN);

    uint32_t import_dir_rva = rdata_rva + import_dir_off;
    uint32_t first_iat_rva  = rdata_rva + first_iat_off;

    uint32_t bss_rva     = rdata_rva + rdata_vsize;
    uint32_t bss_vsize   = (uint32_t)BSS_BYTES;
    uint32_t bss_vsize_a = pe_align_up(bss_vsize, PE_SECTION_ALIGN);

    /* Fill IAT and ILT entries per DLL.                                */
    for (int i = 0; i < n_imports; i++) {
        int d = dll_of[i], pos = pos_in_dll[i];
        uint64_t ptr_by_name = (uint64_t)(rdata_rva + hint_off[i]);
        uint8_t *pi = g_rodata + iat_start[d] + pos * 8;
        uint8_t *pl = g_rodata + ilt_start[d] + pos * 8;
        for (int b = 0; b < 8; b++) pi[b] = (uint8_t)(ptr_by_name >> (b * 8));
        for (int b = 0; b < 8; b++) pl[b] = (uint8_t)(ptr_by_name >> (b * 8));
    }
    /* Per-DLL terminators.                                             */
    for (int d = 0; d < n_dlls; d++) {
        for (int b = 0; b < 8; b++) {
            g_rodata[iat_start[d] + count_per_dll[d] * 8 + b] = 0;
            g_rodata[ilt_start[d] + count_per_dll[d] * 8 + b] = 0;
        }
    }

    /* Hint/name entries.                                                */
    for (int i = 0; i < n_imports; i++) {
        uint8_t *p = g_rodata + hint_off[i];
        p[0] = 0; p[1] = 0;
        int nl = (int)strlen(g_imports[i].fn_name);
        memcpy(p + 2, g_imports[i].fn_name, (size_t)nl);
        p[2 + nl] = 0;
        if ((2 + nl + 1) & 1) p[2 + nl + 1] = 0;
    }

    /* DLL names.                                                        */
    for (int d = 0; d < n_dlls; d++) {
        int nl = (int)strlen(dll_names[d]);
        memcpy(g_rodata + dll_name_off[d], dll_names[d], (size_t)nl);
        g_rodata[dll_name_off[d] + nl] = 0;
    }

    /* Import directory: one descriptor per DLL + zero terminator.      */
    for (int d = 0; d < n_dlls; d++) {
        uint8_t *dp = g_rodata + import_dir_off + d * 20;
        uint32_t v;
        v = rdata_rva + ilt_start[d];
        for (int b = 0; b < 4; b++) dp[b]      = (uint8_t)(v >> (b * 8));
        for (int b = 0; b < 4; b++) dp[4 + b]  = 0;
        for (int b = 0; b < 4; b++) dp[8 + b]  = 0;
        v = rdata_rva + dll_name_off[d];
        for (int b = 0; b < 4; b++) dp[12 + b] = (uint8_t)(v >> (b * 8));
        v = rdata_rva + iat_start[d];
        for (int b = 0; b < 4; b++) dp[16 + b] = (uint8_t)(v >> (b * 8));
    }
    for (int b = 0; b < 20; b++) g_rodata[import_dir_off + n_dlls * 20 + b] = 0;

    /* Patch rodata rip-relative loads.                                  */
    for (int i = 0; i < g_nrodrelocs; i++) {
        RodataReloc *r = &g_rodrelocs[i];
        uint32_t rip   = text_rva + r->from_rip;
        uint32_t target = rdata_rva + r->rodata_off;
        int32_t disp  = (int32_t)(target - rip);
        code_patch_u32(r->code_off, (uint32_t)disp);
    }
    /* Patch IAT call-through-pointer relocs, mapping global slot to
     * the per-DLL IAT position.                                         */
    for (int i = 0; i < g_niatrelocs; i++) {
        IatReloc *r = &g_iatrelocs[i];
        int d = dll_of[r->iat_slot];
        int pos = pos_in_dll[r->iat_slot];
        uint32_t rip   = text_rva + r->code_off + 4;
        uint32_t target = rdata_rva + iat_start[d] + pos * 8;
        int32_t disp  = (int32_t)(target - rip);
        code_patch_u32(r->code_off, (uint32_t)disp);
    }
    /* Patch BSS relocs.                                                   */
    for (int i = 0; i < g_nbssrelocs; i++) {
        BssReloc *r = &g_bssrelocs[i];
        uint32_t rip   = text_rva + r->code_off + 4;
        uint32_t target = bss_rva + r->bss_off;
        int32_t disp  = (int32_t)(target - rip);
        code_patch_u32(r->code_off, (uint32_t)disp);
    }

    uint32_t size_of_headers = pe_align_up((uint32_t)0x1c8, PE_FILE_ALIGN);
    uint32_t size_of_image   = bss_rva + bss_vsize_a;
    uint32_t addr_of_entry   = text_rva;   /* entry is at offset 0 of .text */

    FILE *f = fopen(out_path, "wb");
    if (!f) { fprintf(stderr, "luna-boot: cannot open '%s'\n", out_path); exit(1); }

    /* ---- DOS header (64 bytes) ---------------------------------------- */
    write_u8(f, 'M'); write_u8(f, 'Z');
    for (int i = 2; i < 0x3c; i++) write_u8(f, 0);
    write_u32(f, 0x40);            /* e_lfanew — pointer to PE signature */

    /* ---- PE signature "PE\0\0" ---------------------------------------- */
    write_u8(f, 'P'); write_u8(f, 'E'); write_u8(f, 0); write_u8(f, 0);

    /* ---- COFF header (20 bytes) --------------------------------------- */
    write_u16(f, 0x8664);          /* Machine = IMAGE_FILE_MACHINE_AMD64  */
    write_u16(f, 3);               /* NumberOfSections: text + rdata + bss */
    write_u32(f, 0);               /* TimeDateStamp                       */
    write_u32(f, 0);               /* PointerToSymbolTable                */
    write_u32(f, 0);               /* NumberOfSymbols                     */
    write_u16(f, 240);             /* SizeOfOptionalHeader                */
    write_u16(f, 0x0022);          /* Characteristics: EXECUTABLE | LARGE_ADDRESS_AWARE */

    /* ---- Optional header PE32+ (240 bytes) ---------------------------- */
    write_u16(f, 0x020b);          /* Magic = PE32+                       */
    write_u8 (f, 14);              /* MajorLinkerVersion                  */
    write_u8 (f, 0);               /* MinorLinkerVersion                  */
    write_u32(f, text_file_size);  /* SizeOfCode                          */
    write_u32(f, rdata_file_size); /* SizeOfInitializedData               */
    write_u32(f, bss_vsize);       /* SizeOfUninitializedData             */
    write_u32(f, addr_of_entry);   /* AddressOfEntryPoint (RVA)           */
    write_u32(f, text_rva);        /* BaseOfCode (RVA)                    */
    write_u64(f, PE_IMAGE_BASE);   /* ImageBase                           */
    write_u32(f, PE_SECTION_ALIGN);
    write_u32(f, PE_FILE_ALIGN);
    write_u16(f, 6); write_u16(f, 0);   /* OS version 6.0                 */
    write_u16(f, 0); write_u16(f, 0);   /* Image version                  */
    write_u16(f, 6); write_u16(f, 0);   /* Subsystem version 6.0          */
    write_u32(f, 0);                     /* Win32VersionValue             */
    write_u32(f, size_of_image);         /* SizeOfImage                   */
    write_u32(f, size_of_headers);       /* SizeOfHeaders                 */
    write_u32(f, 0);                     /* CheckSum                      */
    write_u16(f, 3);                     /* Subsystem = Windows CUI       */
    write_u16(f, 0);                     /* DllCharacteristics            */
    write_u64(f, 0x100000);              /* SizeOfStackReserve            */
    write_u64(f, 0x1000);                /* SizeOfStackCommit             */
    write_u64(f, 0x100000);              /* SizeOfHeapReserve             */
    write_u64(f, 0x1000);                /* SizeOfHeapCommit              */
    write_u32(f, 0);                     /* LoaderFlags                   */
    write_u32(f, 16);                    /* NumberOfRvaAndSizes           */
    /* Data Directories (16 * 8 = 128) — all zero except Import [1] & IAT [12] */
    for (int i = 0; i < 16; i++) {
        uint32_t rva = 0, size = 0;
        if (i == 1) { rva = import_dir_rva; size = (uint32_t)(n_dlls * 20); }
        if (i == 12){ rva = first_iat_rva;  size = (uint32_t)iat_total_bytes; }
        write_u32(f, rva);
        write_u32(f, size);
    }

    /* ---- Section headers ---------------------------------------------- */
    /* .text */
    write_u8(f, '.'); write_u8(f, 't'); write_u8(f, 'e'); write_u8(f, 'x');
    write_u8(f, 't'); write_u8(f, 0);   write_u8(f, 0);   write_u8(f, 0);
    write_u32(f, (uint32_t)text_len);   /* VirtualSize     */
    write_u32(f, text_rva);             /* VirtualAddress  */
    write_u32(f, text_file_size);       /* SizeOfRawData   */
    write_u32(f, text_file_off);        /* PointerToRawData*/
    write_u32(f, 0);                    /* PointerToRelocations */
    write_u32(f, 0);                    /* PointerToLineNumbers */
    write_u16(f, 0); write_u16(f, 0);   /* NumRelocs, NumLine   */
    write_u32(f, 0x60000020);           /* CODE | EXECUTE | READ */
    /* .rdata */
    write_u8(f, '.'); write_u8(f, 'r'); write_u8(f, 'd'); write_u8(f, 'a');
    write_u8(f, 't'); write_u8(f, 'a'); write_u8(f, 0);   write_u8(f, 0);
    write_u32(f, (uint32_t)total_rdata);
    write_u32(f, rdata_rva);
    write_u32(f, rdata_file_size);
    write_u32(f, rdata_file_off);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u16(f, 0); write_u16(f, 0);
    write_u32(f, 0x40000040);           /* READ | INITIALIZED_DATA */
    /* .bss — zero-filled heap.  SizeOfRawData = 0, PointerToRawData = 0
     * means the loader commits bss_vsize bytes of zero-init memory at
     * bss_rva without consuming any file bytes.                         */
    write_u8(f, '.'); write_u8(f, 'b'); write_u8(f, 's'); write_u8(f, 's');
    write_u8(f, 0);   write_u8(f, 0);   write_u8(f, 0);   write_u8(f, 0);
    write_u32(f, bss_vsize);
    write_u32(f, bss_rva);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u16(f, 0); write_u16(f, 0);
    write_u32(f, 0xC0000080);           /* READ | WRITE | UNINITIALIZED_DATA */

    /* ---- Pad to text_file_off ---------------------------------------- */
    long pos = ftell(f);
    for (long p = pos; p < (long)text_file_off; p++) fputc(0, f);

    /* ---- .text ------------------------------------------------------- */
    fwrite(g_code, 1, (size_t)text_len, f);
    for (uint32_t p = (uint32_t)text_len; p < text_file_size; p++) fputc(0, f);

    /* ---- .rdata ------------------------------------------------------ */
    fwrite(g_rodata, 1, (size_t)total_rdata, f);
    for (uint32_t p = (uint32_t)total_rdata; p < rdata_file_size; p++) fputc(0, f);

    fclose(f);
}

/* ========================================================================= */
/* 10. MAIN + DRIVER                                                          */
/* ========================================================================= */

static void add_default_include_paths(const char *input_path)
{
    /* Derive repo-relative defaults: <dir>/../src/core and <dir>/../src/stdlib
       plus plain "src/core" and "src/stdlib" from the cwd. */
    g_include_paths[g_ninclude++] = "src/core";
    g_include_paths[g_ninclude++] = "src/stdlib";

    char *dir = arena_strndup(input_path, (int)strlen(input_path));
    for (int i = (int)strlen(dir) - 1; i >= 0; i--) {
        if (dir[i] == '/' || dir[i] == '\\') { dir[i] = 0; break; }
        if (i == 0) { dir[0] = '.'; dir[1] = 0; break; }
    }
    {
        char *p = (char *)arena_alloc(1024);
        snprintf(p, 1024, "%s", dir);
        g_include_paths[g_ninclude++] = p;
    }
    {
        char *p = (char *)arena_alloc(1024);
        snprintf(p, 1024, "%s/../core", dir);
        g_include_paths[g_ninclude++] = p;
    }
    {
        char *p = (char *)arena_alloc(1024);
        snprintf(p, 1024, "%s/../stdlib", dir);
        g_include_paths[g_ninclude++] = p;
    }
}

static void usage(void)
{
    fprintf(stderr,
        "usage: luna-boot <input.luna> [-o <output>] [--target linux|windows] [-v]\n"
        "\n"
        "Minimal C bootstrap for Luna.  Compiles a limited subset of the\n"
        "language into a native x86-64 executable.\n"
        "\n"
        "  --target linux    emit Linux ELF64 (default on Linux / WSL)\n"
        "  --target windows  emit Windows PE64 (default on Windows)\n"
        "  -o <output>       output file path (default: a.out / a.exe)\n"
        "  --lib             allow input with no `main` function\n"
        "  -v                verbose progress\n");
    exit(2);
}

/* Detect host OS at program startup so `--target` defaults to a sensible
 * value.  Respect the compile-time #define so cross-compiling a Luna
 * binary (e.g. building a Windows exe from WSL) is just a flag.         */
static int detect_host_target(void)
{
#if defined(_WIN32) || defined(_WIN64)
    return TARGET_WINDOWS;
#else
    return TARGET_LINUX;
#endif
}

int g_allow_no_main = 0;

int main(int argc, char **argv)
{
    const char *input = NULL;
    const char *output = NULL;
    g_target = detect_host_target();

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '-') { if (!input) input = a; else usage(); continue; }
        if (strcmp(a, "-o") == 0 && i + 1 < argc) { output = argv[++i]; continue; }
        if (strcmp(a, "--target") == 0 && i + 1 < argc) {
            const char *t = argv[++i];
            if (strcmp(t, "linux") == 0) g_target = TARGET_LINUX;
            else if (strcmp(t, "windows") == 0) g_target = TARGET_WINDOWS;
            else { fprintf(stderr, "luna-boot: unknown target '%s'\n", t); usage(); }
            continue;
        }
        if (strcmp(a, "-v") == 0) { g_verbose = 1; continue; }
        if (strcmp(a, "--lib") == 0) { g_allow_no_main = 1; continue; }
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) usage();
        fprintf(stderr, "luna-boot: unknown flag '%s'\n", a);
        usage();
    }
    if (!input) usage();
    if (!output) output = (g_target == TARGET_WINDOWS) ? "a.exe" : "a.out";

    arena_init();
    g_toks    = (Tok *)malloc(sizeof(Tok) * MAX_TOKENS);
    g_nodes   = (AstNode *)malloc(sizeof(AstNode) * MAX_NODES);
    g_code    = (uint8_t *)malloc(CODE_CAP);
    g_rodata  = (uint8_t *)malloc(RODATA_CAP);
    if (!g_toks || !g_nodes || !g_code || !g_rodata) {
        fprintf(stderr, "luna-boot: out of memory (top-level)\n");
        return 1;
    }
    memset(g_code,   0, CODE_CAP);
    memset(g_rodata, 0, RODATA_CAP);

    ffi_init_core();
    add_default_include_paths(input);

    /* Auto-load prelude.luna if available.  It lives next to the
     * luna_bootstrap.c source tree (bootstrap/prelude.luna) or on any
     * include path.  Provides print_int, println_int and other tiny
     * helpers written in Luna itself using the print()/shine() intrinsics. */
    {
        static const char *PRELUDE_NAMES[] = {
            "bootstrap/prelude.luna", "prelude.luna", NULL
        };
        for (int i = 0; PRELUDE_NAMES[i]; i++) {
            FILE *fp = fopen(PRELUDE_NAMES[i], "rb");
            if (fp) {
                fclose(fp);
                (void)load_file(PRELUDE_NAMES[i]);
                break;
            }
        }
    }

    int root_idx = load_file(input);
    resolve_imports(root_idx);

    tc_light();

    lower_all();

    if (g_target == TARGET_WINDOWS) {
        write_pe64(output);
    } else {
        write_elf64(output);
    }

    if (g_verbose) {
        fprintf(stderr,
            "luna-boot: %d files, %d tokens, %d nodes, %d syms,\n"
            "          %d bytes of code, %d bytes of rodata, %d relocs -> %s\n",
            g_nfiles, g_ntoks, g_nnodes, g_nsyms,
            g_code_len, g_rodata_len, g_nrelocs, output);
    }

    return 0;
}
