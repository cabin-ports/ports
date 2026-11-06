/* The C consumer: the tree-sitter runtime driven by a real grammar.  It
 * parses JSON, navigates nodes and fields, walks a tree cursor, runs
 * queries with captures, reparses after an edit and reads the changed
 * ranges, inspects error and missing nodes, and cancels a parse through
 * the progress callback.
 *
 * The runtime ships no grammar, so one is inlined below, between the
 * BEGIN and END markers: src/tree_sitter/parser.h and src/parser.c of
 * tree-sitter-json v0.24.8
 * (https://github.com/tree-sitter/tree-sitter-json, tag v0.24.8, commit
 * ee35a6ebefcef0c5c416c0d1ccec7370cfca5a24), taken from
 * https://github.com/tree-sitter/tree-sitter-json/archive/refs/tags/v0.24.8.tar.gz
 * (sha256 acf6e8362457e819ed8b613f2ad9a0e1b621a77556c296f3abea58f7880a9213).
 * Both files are generated (the grammar's package-lock.json pins
 * tree-sitter-cli 0.24.4) and are unmodified, except that
 * parser.c's `#include "tree_sitter/parser.h"` line is replaced by the
 * header itself.  The grammar is ABI version 14, which this runtime
 * accepts (TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION 13 through
 * TREE_SITTER_LANGUAGE_VERSION 15).  Its license:
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2014 Max Brunsfeld
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <tree_sitter/api.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tree_sitter_json() is local to this program, not exported from it. */
#define TREE_SITTER_HIDE_SYMBOLS

/* ===== BEGIN tree-sitter-json v0.24.8 src/tree_sitter/parser.h ===== */
#ifndef TREE_SITTER_PARSER_H_
#define TREE_SITTER_PARSER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define ts_builtin_sym_error ((TSSymbol)-1)
#define ts_builtin_sym_end 0
#define TREE_SITTER_SERIALIZATION_BUFFER_SIZE 1024

#ifndef TREE_SITTER_API_H_
typedef uint16_t TSStateId;
typedef uint16_t TSSymbol;
typedef uint16_t TSFieldId;
typedef struct TSLanguage TSLanguage;
#endif

typedef struct {
  TSFieldId field_id;
  uint8_t child_index;
  bool inherited;
} TSFieldMapEntry;

typedef struct {
  uint16_t index;
  uint16_t length;
} TSFieldMapSlice;

typedef struct {
  bool visible;
  bool named;
  bool supertype;
} TSSymbolMetadata;

typedef struct TSLexer TSLexer;

struct TSLexer {
  int32_t lookahead;
  TSSymbol result_symbol;
  void (*advance)(TSLexer *, bool);
  void (*mark_end)(TSLexer *);
  uint32_t (*get_column)(TSLexer *);
  bool (*is_at_included_range_start)(const TSLexer *);
  bool (*eof)(const TSLexer *);
  void (*log)(const TSLexer *, const char *, ...);
};

typedef enum {
  TSParseActionTypeShift,
  TSParseActionTypeReduce,
  TSParseActionTypeAccept,
  TSParseActionTypeRecover,
} TSParseActionType;

typedef union {
  struct {
    uint8_t type;
    TSStateId state;
    bool extra;
    bool repetition;
  } shift;
  struct {
    uint8_t type;
    uint8_t child_count;
    TSSymbol symbol;
    int16_t dynamic_precedence;
    uint16_t production_id;
  } reduce;
  uint8_t type;
} TSParseAction;

typedef struct {
  uint16_t lex_state;
  uint16_t external_lex_state;
} TSLexMode;

typedef union {
  TSParseAction action;
  struct {
    uint8_t count;
    bool reusable;
  } entry;
} TSParseActionEntry;

typedef struct {
  int32_t start;
  int32_t end;
} TSCharacterRange;

struct TSLanguage {
  uint32_t version;
  uint32_t symbol_count;
  uint32_t alias_count;
  uint32_t token_count;
  uint32_t external_token_count;
  uint32_t state_count;
  uint32_t large_state_count;
  uint32_t production_id_count;
  uint32_t field_count;
  uint16_t max_alias_sequence_length;
  const uint16_t *parse_table;
  const uint16_t *small_parse_table;
  const uint32_t *small_parse_table_map;
  const TSParseActionEntry *parse_actions;
  const char * const *symbol_names;
  const char * const *field_names;
  const TSFieldMapSlice *field_map_slices;
  const TSFieldMapEntry *field_map_entries;
  const TSSymbolMetadata *symbol_metadata;
  const TSSymbol *public_symbol_map;
  const uint16_t *alias_map;
  const TSSymbol *alias_sequences;
  const TSLexMode *lex_modes;
  bool (*lex_fn)(TSLexer *, TSStateId);
  bool (*keyword_lex_fn)(TSLexer *, TSStateId);
  TSSymbol keyword_capture_token;
  struct {
    const bool *states;
    const TSSymbol *symbol_map;
    void *(*create)(void);
    void (*destroy)(void *);
    bool (*scan)(void *, TSLexer *, const bool *symbol_whitelist);
    unsigned (*serialize)(void *, char *);
    void (*deserialize)(void *, const char *, unsigned);
  } external_scanner;
  const TSStateId *primary_state_ids;
};

static inline bool set_contains(TSCharacterRange *ranges, uint32_t len, int32_t lookahead) {
  uint32_t index = 0;
  uint32_t size = len - index;
  while (size > 1) {
    uint32_t half_size = size / 2;
    uint32_t mid_index = index + half_size;
    TSCharacterRange *range = &ranges[mid_index];
    if (lookahead >= range->start && lookahead <= range->end) {
      return true;
    } else if (lookahead > range->end) {
      index = mid_index;
    }
    size -= half_size;
  }
  TSCharacterRange *range = &ranges[index];
  return (lookahead >= range->start && lookahead <= range->end);
}

/*
 *  Lexer Macros
 */

#ifdef _MSC_VER
#define UNUSED __pragma(warning(suppress : 4101))
#else
#define UNUSED __attribute__((unused))
#endif

#define START_LEXER()           \
  bool result = false;          \
  bool skip = false;            \
  UNUSED                        \
  bool eof = false;             \
  int32_t lookahead;            \
  goto start;                   \
  next_state:                   \
  lexer->advance(lexer, skip);  \
  start:                        \
  skip = false;                 \
  lookahead = lexer->lookahead;

#define ADVANCE(state_value) \
  {                          \
    state = state_value;     \
    goto next_state;         \
  }

#define ADVANCE_MAP(...)                                              \
  {                                                                   \
    static const uint16_t map[] = { __VA_ARGS__ };                    \
    for (uint32_t i = 0; i < sizeof(map) / sizeof(map[0]); i += 2) {  \
      if (map[i] == lookahead) {                                      \
        state = map[i + 1];                                           \
        goto next_state;                                              \
      }                                                               \
    }                                                                 \
  }

#define SKIP(state_value) \
  {                       \
    skip = true;          \
    state = state_value;  \
    goto next_state;      \
  }

#define ACCEPT_TOKEN(symbol_value)     \
  result = true;                       \
  lexer->result_symbol = symbol_value; \
  lexer->mark_end(lexer);

#define END_STATE() return result;

/*
 *  Parse Table Macros
 */

#define SMALL_STATE(id) ((id) - LARGE_STATE_COUNT)

#define STATE(id) id

#define ACTIONS(id) id

#define SHIFT(state_value)            \
  {{                                  \
    .shift = {                        \
      .type = TSParseActionTypeShift, \
      .state = (state_value)          \
    }                                 \
  }}

#define SHIFT_REPEAT(state_value)     \
  {{                                  \
    .shift = {                        \
      .type = TSParseActionTypeShift, \
      .state = (state_value),         \
      .repetition = true              \
    }                                 \
  }}

#define SHIFT_EXTRA()                 \
  {{                                  \
    .shift = {                        \
      .type = TSParseActionTypeShift, \
      .extra = true                   \
    }                                 \
  }}

#define REDUCE(symbol_name, children, precedence, prod_id) \
  {{                                                       \
    .reduce = {                                            \
      .type = TSParseActionTypeReduce,                     \
      .symbol = symbol_name,                               \
      .child_count = children,                             \
      .dynamic_precedence = precedence,                    \
      .production_id = prod_id                             \
    },                                                     \
  }}

#define RECOVER()                    \
  {{                                 \
    .type = TSParseActionTypeRecover \
  }}

#define ACCEPT_INPUT()              \
  {{                                \
    .type = TSParseActionTypeAccept \
  }}

#ifdef __cplusplus
}
#endif

#endif  // TREE_SITTER_PARSER_H_
/* ===== END tree-sitter-json v0.24.8 src/tree_sitter/parser.h ===== */

/* ===== BEGIN tree-sitter-json v0.24.8 src/parser.c ===== */

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#define LANGUAGE_VERSION 14
#define STATE_COUNT 32
#define LARGE_STATE_COUNT 7
#define SYMBOL_COUNT 25
#define ALIAS_COUNT 0
#define TOKEN_COUNT 15
#define EXTERNAL_TOKEN_COUNT 0
#define FIELD_COUNT 2
#define MAX_ALIAS_SEQUENCE_LENGTH 4
#define PRODUCTION_ID_COUNT 2

enum ts_symbol_identifiers {
  anon_sym_LBRACE = 1,
  anon_sym_COMMA = 2,
  anon_sym_RBRACE = 3,
  anon_sym_COLON = 4,
  anon_sym_LBRACK = 5,
  anon_sym_RBRACK = 6,
  anon_sym_DQUOTE = 7,
  sym_string_content = 8,
  sym_escape_sequence = 9,
  sym_number = 10,
  sym_true = 11,
  sym_false = 12,
  sym_null = 13,
  sym_comment = 14,
  sym_document = 15,
  sym__value = 16,
  sym_object = 17,
  sym_pair = 18,
  sym_array = 19,
  sym_string = 20,
  aux_sym__string_content = 21,
  aux_sym_document_repeat1 = 22,
  aux_sym_object_repeat1 = 23,
  aux_sym_array_repeat1 = 24,
};

static const char * const ts_symbol_names[] = {
  [ts_builtin_sym_end] = "end",
  [anon_sym_LBRACE] = "{",
  [anon_sym_COMMA] = ",",
  [anon_sym_RBRACE] = "}",
  [anon_sym_COLON] = ":",
  [anon_sym_LBRACK] = "[",
  [anon_sym_RBRACK] = "]",
  [anon_sym_DQUOTE] = "\"",
  [sym_string_content] = "string_content",
  [sym_escape_sequence] = "escape_sequence",
  [sym_number] = "number",
  [sym_true] = "true",
  [sym_false] = "false",
  [sym_null] = "null",
  [sym_comment] = "comment",
  [sym_document] = "document",
  [sym__value] = "_value",
  [sym_object] = "object",
  [sym_pair] = "pair",
  [sym_array] = "array",
  [sym_string] = "string",
  [aux_sym__string_content] = "_string_content",
  [aux_sym_document_repeat1] = "document_repeat1",
  [aux_sym_object_repeat1] = "object_repeat1",
  [aux_sym_array_repeat1] = "array_repeat1",
};

static const TSSymbol ts_symbol_map[] = {
  [ts_builtin_sym_end] = ts_builtin_sym_end,
  [anon_sym_LBRACE] = anon_sym_LBRACE,
  [anon_sym_COMMA] = anon_sym_COMMA,
  [anon_sym_RBRACE] = anon_sym_RBRACE,
  [anon_sym_COLON] = anon_sym_COLON,
  [anon_sym_LBRACK] = anon_sym_LBRACK,
  [anon_sym_RBRACK] = anon_sym_RBRACK,
  [anon_sym_DQUOTE] = anon_sym_DQUOTE,
  [sym_string_content] = sym_string_content,
  [sym_escape_sequence] = sym_escape_sequence,
  [sym_number] = sym_number,
  [sym_true] = sym_true,
  [sym_false] = sym_false,
  [sym_null] = sym_null,
  [sym_comment] = sym_comment,
  [sym_document] = sym_document,
  [sym__value] = sym__value,
  [sym_object] = sym_object,
  [sym_pair] = sym_pair,
  [sym_array] = sym_array,
  [sym_string] = sym_string,
  [aux_sym__string_content] = aux_sym__string_content,
  [aux_sym_document_repeat1] = aux_sym_document_repeat1,
  [aux_sym_object_repeat1] = aux_sym_object_repeat1,
  [aux_sym_array_repeat1] = aux_sym_array_repeat1,
};

static const TSSymbolMetadata ts_symbol_metadata[] = {
  [ts_builtin_sym_end] = {
    .visible = false,
    .named = true,
  },
  [anon_sym_LBRACE] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_COMMA] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_RBRACE] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_COLON] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_LBRACK] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_RBRACK] = {
    .visible = true,
    .named = false,
  },
  [anon_sym_DQUOTE] = {
    .visible = true,
    .named = false,
  },
  [sym_string_content] = {
    .visible = true,
    .named = true,
  },
  [sym_escape_sequence] = {
    .visible = true,
    .named = true,
  },
  [sym_number] = {
    .visible = true,
    .named = true,
  },
  [sym_true] = {
    .visible = true,
    .named = true,
  },
  [sym_false] = {
    .visible = true,
    .named = true,
  },
  [sym_null] = {
    .visible = true,
    .named = true,
  },
  [sym_comment] = {
    .visible = true,
    .named = true,
  },
  [sym_document] = {
    .visible = true,
    .named = true,
  },
  [sym__value] = {
    .visible = false,
    .named = true,
    .supertype = true,
  },
  [sym_object] = {
    .visible = true,
    .named = true,
  },
  [sym_pair] = {
    .visible = true,
    .named = true,
  },
  [sym_array] = {
    .visible = true,
    .named = true,
  },
  [sym_string] = {
    .visible = true,
    .named = true,
  },
  [aux_sym__string_content] = {
    .visible = false,
    .named = false,
  },
  [aux_sym_document_repeat1] = {
    .visible = false,
    .named = false,
  },
  [aux_sym_object_repeat1] = {
    .visible = false,
    .named = false,
  },
  [aux_sym_array_repeat1] = {
    .visible = false,
    .named = false,
  },
};

enum ts_field_identifiers {
  field_key = 1,
  field_value = 2,
};

static const char * const ts_field_names[] = {
  [0] = NULL,
  [field_key] = "key",
  [field_value] = "value",
};

static const TSFieldMapSlice ts_field_map_slices[PRODUCTION_ID_COUNT] = {
  [1] = {.index = 0, .length = 2},
};

static const TSFieldMapEntry ts_field_map_entries[] = {
  [0] =
    {field_key, 0},
    {field_value, 2},
};

static const TSSymbol ts_alias_sequences[PRODUCTION_ID_COUNT][MAX_ALIAS_SEQUENCE_LENGTH] = {
  [0] = {0},
};

static const uint16_t ts_non_terminal_alias_map[] = {
  0,
};

static const TSStateId ts_primary_state_ids[STATE_COUNT] = {
  [0] = 0,
  [1] = 1,
  [2] = 2,
  [3] = 3,
  [4] = 4,
  [5] = 5,
  [6] = 6,
  [7] = 7,
  [8] = 8,
  [9] = 9,
  [10] = 10,
  [11] = 11,
  [12] = 12,
  [13] = 13,
  [14] = 14,
  [15] = 15,
  [16] = 16,
  [17] = 17,
  [18] = 18,
  [19] = 19,
  [20] = 20,
  [21] = 21,
  [22] = 22,
  [23] = 23,
  [24] = 24,
  [25] = 25,
  [26] = 26,
  [27] = 27,
  [28] = 28,
  [29] = 29,
  [30] = 30,
  [31] = 31,
};

static bool ts_lex(TSLexer *lexer, TSStateId state) {
  START_LEXER();
  eof = lexer->eof(lexer);
  switch (state) {
    case 0:
      if (eof) ADVANCE(21);
      ADVANCE_MAP(
        '"', 28,
        ',', 23,
        '-', 7,
        '/', 3,
        '0', 35,
        ':', 25,
        '[', 26,
        '\\', 18,
        ']', 27,
        'f', 8,
        'n', 17,
        't', 14,
        '{', 22,
        '}', 24,
      );
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(20);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(36);
      END_STATE();
    case 1:
      if (lookahead == '\n') SKIP(2);
      if (lookahead == '"') ADVANCE(28);
      if (lookahead == '/') ADVANCE(29);
      if (lookahead == '\\') ADVANCE(18);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') ADVANCE(32);
      if (lookahead != 0) ADVANCE(33);
      END_STATE();
    case 2:
      if (lookahead == '"') ADVANCE(28);
      if (lookahead == '/') ADVANCE(3);
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(2);
      END_STATE();
    case 3:
      if (lookahead == '*') ADVANCE(5);
      if (lookahead == '/') ADVANCE(43);
      END_STATE();
    case 4:
      if (lookahead == '*') ADVANCE(4);
      if (lookahead == '/') ADVANCE(42);
      if (lookahead != 0) ADVANCE(5);
      END_STATE();
    case 5:
      if (lookahead == '*') ADVANCE(4);
      if (lookahead != 0) ADVANCE(5);
      END_STATE();
    case 6:
      if (lookahead == '-') ADVANCE(19);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(38);
      END_STATE();
    case 7:
      if (lookahead == '0') ADVANCE(35);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(36);
      END_STATE();
    case 8:
      if (lookahead == 'a') ADVANCE(11);
      END_STATE();
    case 9:
      if (lookahead == 'e') ADVANCE(39);
      END_STATE();
    case 10:
      if (lookahead == 'e') ADVANCE(40);
      END_STATE();
    case 11:
      if (lookahead == 'l') ADVANCE(15);
      END_STATE();
    case 12:
      if (lookahead == 'l') ADVANCE(41);
      END_STATE();
    case 13:
      if (lookahead == 'l') ADVANCE(12);
      END_STATE();
    case 14:
      if (lookahead == 'r') ADVANCE(16);
      END_STATE();
    case 15:
      if (lookahead == 's') ADVANCE(10);
      END_STATE();
    case 16:
      if (lookahead == 'u') ADVANCE(9);
      END_STATE();
    case 17:
      if (lookahead == 'u') ADVANCE(13);
      END_STATE();
    case 18:
      ADVANCE_MAP(
        '"', 34,
        '/', 34,
        '\\', 34,
        'b', 34,
        'f', 34,
        'n', 34,
        'r', 34,
        't', 34,
        'u', 34,
      );
      END_STATE();
    case 19:
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(38);
      END_STATE();
    case 20:
      if (eof) ADVANCE(21);
      ADVANCE_MAP(
        '"', 28,
        ',', 23,
        '-', 7,
        '/', 3,
        '0', 35,
        ':', 25,
        '[', 26,
        ']', 27,
        'f', 8,
        'n', 17,
        't', 14,
        '{', 22,
        '}', 24,
      );
      if (('\t' <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') SKIP(20);
      if (('1' <= lookahead && lookahead <= '9')) ADVANCE(36);
      END_STATE();
    case 21:
      ACCEPT_TOKEN(ts_builtin_sym_end);
      END_STATE();
    case 22:
      ACCEPT_TOKEN(anon_sym_LBRACE);
      END_STATE();
    case 23:
      ACCEPT_TOKEN(anon_sym_COMMA);
      END_STATE();
    case 24:
      ACCEPT_TOKEN(anon_sym_RBRACE);
      END_STATE();
    case 25:
      ACCEPT_TOKEN(anon_sym_COLON);
      END_STATE();
    case 26:
      ACCEPT_TOKEN(anon_sym_LBRACK);
      END_STATE();
    case 27:
      ACCEPT_TOKEN(anon_sym_RBRACK);
      END_STATE();
    case 28:
      ACCEPT_TOKEN(anon_sym_DQUOTE);
      END_STATE();
    case 29:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '*') ADVANCE(31);
      if (lookahead == '/') ADVANCE(33);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(33);
      END_STATE();
    case 30:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '*') ADVANCE(30);
      if (lookahead == '/') ADVANCE(33);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(31);
      END_STATE();
    case 31:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '*') ADVANCE(30);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(31);
      END_STATE();
    case 32:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead == '/') ADVANCE(29);
      if (lookahead == '\t' ||
          (0x0b <= lookahead && lookahead <= '\r') ||
          lookahead == ' ') ADVANCE(32);
      if (lookahead != 0 &&
          (lookahead < '\t' || '\r' < lookahead) &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(33);
      END_STATE();
    case 33:
      ACCEPT_TOKEN(sym_string_content);
      if (lookahead != 0 &&
          lookahead != '\n' &&
          lookahead != '"' &&
          lookahead != '\\') ADVANCE(33);
      END_STATE();
    case 34:
      ACCEPT_TOKEN(sym_escape_sequence);
      END_STATE();
    case 35:
      ACCEPT_TOKEN(sym_number);
      if (lookahead == '.') ADVANCE(37);
      if (lookahead == 'E' ||
          lookahead == 'e') ADVANCE(6);
      END_STATE();
    case 36:
      ACCEPT_TOKEN(sym_number);
      if (lookahead == '.') ADVANCE(37);
      if (lookahead == 'E' ||
          lookahead == 'e') ADVANCE(6);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(36);
      END_STATE();
    case 37:
      ACCEPT_TOKEN(sym_number);
      if (lookahead == 'E' ||
          lookahead == 'e') ADVANCE(6);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(37);
      END_STATE();
    case 38:
      ACCEPT_TOKEN(sym_number);
      if (('0' <= lookahead && lookahead <= '9')) ADVANCE(38);
      END_STATE();
    case 39:
      ACCEPT_TOKEN(sym_true);
      END_STATE();
    case 40:
      ACCEPT_TOKEN(sym_false);
      END_STATE();
    case 41:
      ACCEPT_TOKEN(sym_null);
      END_STATE();
    case 42:
      ACCEPT_TOKEN(sym_comment);
      END_STATE();
    case 43:
      ACCEPT_TOKEN(sym_comment);
      if (lookahead != 0 &&
          lookahead != '\n') ADVANCE(43);
      END_STATE();
    default:
      return false;
  }
}

static const TSLexMode ts_lex_modes[STATE_COUNT] = {
  [0] = {.lex_state = 0},
  [1] = {.lex_state = 0},
  [2] = {.lex_state = 0},
  [3] = {.lex_state = 0},
  [4] = {.lex_state = 0},
  [5] = {.lex_state = 0},
  [6] = {.lex_state = 0},
  [7] = {.lex_state = 0},
  [8] = {.lex_state = 0},
  [9] = {.lex_state = 0},
  [10] = {.lex_state = 0},
  [11] = {.lex_state = 0},
  [12] = {.lex_state = 0},
  [13] = {.lex_state = 0},
  [14] = {.lex_state = 0},
  [15] = {.lex_state = 0},
  [16] = {.lex_state = 0},
  [17] = {.lex_state = 1},
  [18] = {.lex_state = 1},
  [19] = {.lex_state = 1},
  [20] = {.lex_state = 0},
  [21] = {.lex_state = 0},
  [22] = {.lex_state = 0},
  [23] = {.lex_state = 0},
  [24] = {.lex_state = 0},
  [25] = {.lex_state = 0},
  [26] = {.lex_state = 0},
  [27] = {.lex_state = 0},
  [28] = {.lex_state = 0},
  [29] = {.lex_state = 0},
  [30] = {.lex_state = 0},
  [31] = {.lex_state = 0},
};

static const uint16_t ts_parse_table[LARGE_STATE_COUNT][SYMBOL_COUNT] = {
  [0] = {
    [ts_builtin_sym_end] = ACTIONS(1),
    [anon_sym_LBRACE] = ACTIONS(1),
    [anon_sym_COMMA] = ACTIONS(1),
    [anon_sym_RBRACE] = ACTIONS(1),
    [anon_sym_COLON] = ACTIONS(1),
    [anon_sym_LBRACK] = ACTIONS(1),
    [anon_sym_RBRACK] = ACTIONS(1),
    [anon_sym_DQUOTE] = ACTIONS(1),
    [sym_escape_sequence] = ACTIONS(1),
    [sym_number] = ACTIONS(1),
    [sym_true] = ACTIONS(1),
    [sym_false] = ACTIONS(1),
    [sym_null] = ACTIONS(1),
    [sym_comment] = ACTIONS(3),
  },
  [1] = {
    [sym_document] = STATE(30),
    [sym__value] = STATE(2),
    [sym_object] = STATE(8),
    [sym_array] = STATE(8),
    [sym_string] = STATE(8),
    [aux_sym_document_repeat1] = STATE(2),
    [ts_builtin_sym_end] = ACTIONS(5),
    [anon_sym_LBRACE] = ACTIONS(7),
    [anon_sym_LBRACK] = ACTIONS(9),
    [anon_sym_DQUOTE] = ACTIONS(11),
    [sym_number] = ACTIONS(13),
    [sym_true] = ACTIONS(13),
    [sym_false] = ACTIONS(13),
    [sym_null] = ACTIONS(13),
    [sym_comment] = ACTIONS(3),
  },
  [2] = {
    [sym__value] = STATE(3),
    [sym_object] = STATE(8),
    [sym_array] = STATE(8),
    [sym_string] = STATE(8),
    [aux_sym_document_repeat1] = STATE(3),
    [ts_builtin_sym_end] = ACTIONS(15),
    [anon_sym_LBRACE] = ACTIONS(7),
    [anon_sym_LBRACK] = ACTIONS(9),
    [anon_sym_DQUOTE] = ACTIONS(11),
    [sym_number] = ACTIONS(13),
    [sym_true] = ACTIONS(13),
    [sym_false] = ACTIONS(13),
    [sym_null] = ACTIONS(13),
    [sym_comment] = ACTIONS(3),
  },
  [3] = {
    [sym__value] = STATE(3),
    [sym_object] = STATE(8),
    [sym_array] = STATE(8),
    [sym_string] = STATE(8),
    [aux_sym_document_repeat1] = STATE(3),
    [ts_builtin_sym_end] = ACTIONS(17),
    [anon_sym_LBRACE] = ACTIONS(19),
    [anon_sym_LBRACK] = ACTIONS(22),
    [anon_sym_DQUOTE] = ACTIONS(25),
    [sym_number] = ACTIONS(28),
    [sym_true] = ACTIONS(28),
    [sym_false] = ACTIONS(28),
    [sym_null] = ACTIONS(28),
    [sym_comment] = ACTIONS(3),
  },
  [4] = {
    [sym__value] = STATE(21),
    [sym_object] = STATE(8),
    [sym_array] = STATE(8),
    [sym_string] = STATE(8),
    [anon_sym_LBRACE] = ACTIONS(7),
    [anon_sym_LBRACK] = ACTIONS(9),
    [anon_sym_RBRACK] = ACTIONS(31),
    [anon_sym_DQUOTE] = ACTIONS(11),
    [sym_number] = ACTIONS(13),
    [sym_true] = ACTIONS(13),
    [sym_false] = ACTIONS(13),
    [sym_null] = ACTIONS(13),
    [sym_comment] = ACTIONS(3),
  },
  [5] = {
    [ts_builtin_sym_end] = ACTIONS(33),
    [anon_sym_LBRACE] = ACTIONS(33),
    [anon_sym_COMMA] = ACTIONS(33),
    [anon_sym_RBRACE] = ACTIONS(33),
    [anon_sym_COLON] = ACTIONS(33),
    [anon_sym_LBRACK] = ACTIONS(33),
    [anon_sym_RBRACK] = ACTIONS(33),
    [anon_sym_DQUOTE] = ACTIONS(33),
    [sym_number] = ACTIONS(33),
    [sym_true] = ACTIONS(33),
    [sym_false] = ACTIONS(33),
    [sym_null] = ACTIONS(33),
    [sym_comment] = ACTIONS(3),
  },
  [6] = {
    [ts_builtin_sym_end] = ACTIONS(35),
    [anon_sym_LBRACE] = ACTIONS(35),
    [anon_sym_COMMA] = ACTIONS(35),
    [anon_sym_RBRACE] = ACTIONS(35),
    [anon_sym_COLON] = ACTIONS(35),
    [anon_sym_LBRACK] = ACTIONS(35),
    [anon_sym_RBRACK] = ACTIONS(35),
    [anon_sym_DQUOTE] = ACTIONS(35),
    [sym_number] = ACTIONS(35),
    [sym_true] = ACTIONS(35),
    [sym_false] = ACTIONS(35),
    [sym_null] = ACTIONS(35),
    [sym_comment] = ACTIONS(3),
  },
};

static const uint16_t ts_small_parse_table[] = {
  [0] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(37), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [17] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(39), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [34] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(41), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [51] = 7,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(7), 1,
      anon_sym_LBRACE,
    ACTIONS(9), 1,
      anon_sym_LBRACK,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    STATE(29), 1,
      sym__value,
    STATE(8), 3,
      sym_object,
      sym_array,
      sym_string,
    ACTIONS(13), 4,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [78] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(43), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [95] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(45), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [112] = 7,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(7), 1,
      anon_sym_LBRACE,
    ACTIONS(9), 1,
      anon_sym_LBRACK,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    STATE(28), 1,
      sym__value,
    STATE(8), 3,
      sym_object,
      sym_array,
      sym_string,
    ACTIONS(13), 4,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [139] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(47), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [156] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(49), 11,
      ts_builtin_sym_end,
      anon_sym_LBRACE,
      anon_sym_COMMA,
      anon_sym_RBRACE,
      anon_sym_LBRACK,
      anon_sym_RBRACK,
      anon_sym_DQUOTE,
      sym_number,
      sym_true,
      sym_false,
      sym_null,
  [173] = 5,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    ACTIONS(51), 1,
      anon_sym_RBRACE,
    STATE(20), 1,
      sym_pair,
    STATE(31), 1,
      sym_string,
  [189] = 4,
    ACTIONS(53), 1,
      anon_sym_DQUOTE,
    ACTIONS(57), 1,
      sym_comment,
    STATE(18), 1,
      aux_sym__string_content,
    ACTIONS(55), 2,
      sym_string_content,
      sym_escape_sequence,
  [203] = 4,
    ACTIONS(57), 1,
      sym_comment,
    ACTIONS(59), 1,
      anon_sym_DQUOTE,
    STATE(19), 1,
      aux_sym__string_content,
    ACTIONS(61), 2,
      sym_string_content,
      sym_escape_sequence,
  [217] = 4,
    ACTIONS(57), 1,
      sym_comment,
    ACTIONS(63), 1,
      anon_sym_DQUOTE,
    STATE(19), 1,
      aux_sym__string_content,
    ACTIONS(65), 2,
      sym_string_content,
      sym_escape_sequence,
  [231] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(68), 1,
      anon_sym_COMMA,
    ACTIONS(70), 1,
      anon_sym_RBRACE,
    STATE(22), 1,
      aux_sym_object_repeat1,
  [244] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(72), 1,
      anon_sym_COMMA,
    ACTIONS(74), 1,
      anon_sym_RBRACK,
    STATE(24), 1,
      aux_sym_array_repeat1,
  [257] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(68), 1,
      anon_sym_COMMA,
    ACTIONS(76), 1,
      anon_sym_RBRACE,
    STATE(25), 1,
      aux_sym_object_repeat1,
  [270] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(11), 1,
      anon_sym_DQUOTE,
    STATE(27), 1,
      sym_pair,
    STATE(31), 1,
      sym_string,
  [283] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(72), 1,
      anon_sym_COMMA,
    ACTIONS(78), 1,
      anon_sym_RBRACK,
    STATE(26), 1,
      aux_sym_array_repeat1,
  [296] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(80), 1,
      anon_sym_COMMA,
    ACTIONS(83), 1,
      anon_sym_RBRACE,
    STATE(25), 1,
      aux_sym_object_repeat1,
  [309] = 4,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(85), 1,
      anon_sym_COMMA,
    ACTIONS(88), 1,
      anon_sym_RBRACK,
    STATE(26), 1,
      aux_sym_array_repeat1,
  [322] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(83), 2,
      anon_sym_COMMA,
      anon_sym_RBRACE,
  [330] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(90), 2,
      anon_sym_COMMA,
      anon_sym_RBRACE,
  [338] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(88), 2,
      anon_sym_COMMA,
      anon_sym_RBRACK,
  [346] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(92), 1,
      ts_builtin_sym_end,
  [353] = 2,
    ACTIONS(3), 1,
      sym_comment,
    ACTIONS(94), 1,
      anon_sym_COLON,
};

static const uint32_t ts_small_parse_table_map[] = {
  [SMALL_STATE(7)] = 0,
  [SMALL_STATE(8)] = 17,
  [SMALL_STATE(9)] = 34,
  [SMALL_STATE(10)] = 51,
  [SMALL_STATE(11)] = 78,
  [SMALL_STATE(12)] = 95,
  [SMALL_STATE(13)] = 112,
  [SMALL_STATE(14)] = 139,
  [SMALL_STATE(15)] = 156,
  [SMALL_STATE(16)] = 173,
  [SMALL_STATE(17)] = 189,
  [SMALL_STATE(18)] = 203,
  [SMALL_STATE(19)] = 217,
  [SMALL_STATE(20)] = 231,
  [SMALL_STATE(21)] = 244,
  [SMALL_STATE(22)] = 257,
  [SMALL_STATE(23)] = 270,
  [SMALL_STATE(24)] = 283,
  [SMALL_STATE(25)] = 296,
  [SMALL_STATE(26)] = 309,
  [SMALL_STATE(27)] = 322,
  [SMALL_STATE(28)] = 330,
  [SMALL_STATE(29)] = 338,
  [SMALL_STATE(30)] = 346,
  [SMALL_STATE(31)] = 353,
};

static const TSParseActionEntry ts_parse_actions[] = {
  [0] = {.entry = {.count = 0, .reusable = false}},
  [1] = {.entry = {.count = 1, .reusable = false}}, RECOVER(),
  [3] = {.entry = {.count = 1, .reusable = true}}, SHIFT_EXTRA(),
  [5] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_document, 0, 0, 0),
  [7] = {.entry = {.count = 1, .reusable = true}}, SHIFT(16),
  [9] = {.entry = {.count = 1, .reusable = true}}, SHIFT(4),
  [11] = {.entry = {.count = 1, .reusable = true}}, SHIFT(17),
  [13] = {.entry = {.count = 1, .reusable = true}}, SHIFT(8),
  [15] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_document, 1, 0, 0),
  [17] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0),
  [19] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(16),
  [22] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(4),
  [25] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(17),
  [28] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_document_repeat1, 2, 0, 0), SHIFT_REPEAT(8),
  [31] = {.entry = {.count = 1, .reusable = true}}, SHIFT(9),
  [33] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_string, 2, 0, 0),
  [35] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_string, 3, 0, 0),
  [37] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_object, 2, 0, 0),
  [39] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym__value, 1, 0, 0),
  [41] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_array, 2, 0, 0),
  [43] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_object, 3, 0, 0),
  [45] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_object, 4, 0, 0),
  [47] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_array, 3, 0, 0),
  [49] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_array, 4, 0, 0),
  [51] = {.entry = {.count = 1, .reusable = true}}, SHIFT(7),
  [53] = {.entry = {.count = 1, .reusable = false}}, SHIFT(5),
  [55] = {.entry = {.count = 1, .reusable = true}}, SHIFT(18),
  [57] = {.entry = {.count = 1, .reusable = false}}, SHIFT_EXTRA(),
  [59] = {.entry = {.count = 1, .reusable = false}}, SHIFT(6),
  [61] = {.entry = {.count = 1, .reusable = true}}, SHIFT(19),
  [63] = {.entry = {.count = 1, .reusable = false}}, REDUCE(aux_sym__string_content, 2, 0, 0),
  [65] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym__string_content, 2, 0, 0), SHIFT_REPEAT(19),
  [68] = {.entry = {.count = 1, .reusable = true}}, SHIFT(23),
  [70] = {.entry = {.count = 1, .reusable = true}}, SHIFT(11),
  [72] = {.entry = {.count = 1, .reusable = true}}, SHIFT(10),
  [74] = {.entry = {.count = 1, .reusable = true}}, SHIFT(14),
  [76] = {.entry = {.count = 1, .reusable = true}}, SHIFT(12),
  [78] = {.entry = {.count = 1, .reusable = true}}, SHIFT(15),
  [80] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_object_repeat1, 2, 0, 0), SHIFT_REPEAT(23),
  [83] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_object_repeat1, 2, 0, 0),
  [85] = {.entry = {.count = 2, .reusable = true}}, REDUCE(aux_sym_array_repeat1, 2, 0, 0), SHIFT_REPEAT(10),
  [88] = {.entry = {.count = 1, .reusable = true}}, REDUCE(aux_sym_array_repeat1, 2, 0, 0),
  [90] = {.entry = {.count = 1, .reusable = true}}, REDUCE(sym_pair, 3, 0, 1),
  [92] = {.entry = {.count = 1, .reusable = true}},  ACCEPT_INPUT(),
  [94] = {.entry = {.count = 1, .reusable = true}}, SHIFT(13),
};

#ifdef __cplusplus
extern "C" {
#endif
#ifdef TREE_SITTER_HIDE_SYMBOLS
#define TS_PUBLIC
#elif defined(_WIN32)
#define TS_PUBLIC __declspec(dllexport)
#else
#define TS_PUBLIC __attribute__((visibility("default")))
#endif

TS_PUBLIC const TSLanguage *tree_sitter_json(void) {
  static const TSLanguage language = {
    .version = LANGUAGE_VERSION,
    .symbol_count = SYMBOL_COUNT,
    .alias_count = ALIAS_COUNT,
    .token_count = TOKEN_COUNT,
    .external_token_count = EXTERNAL_TOKEN_COUNT,
    .state_count = STATE_COUNT,
    .large_state_count = LARGE_STATE_COUNT,
    .production_id_count = PRODUCTION_ID_COUNT,
    .field_count = FIELD_COUNT,
    .max_alias_sequence_length = MAX_ALIAS_SEQUENCE_LENGTH,
    .parse_table = &ts_parse_table[0][0],
    .small_parse_table = ts_small_parse_table,
    .small_parse_table_map = ts_small_parse_table_map,
    .parse_actions = ts_parse_actions,
    .symbol_names = ts_symbol_names,
    .field_names = ts_field_names,
    .field_map_slices = ts_field_map_slices,
    .field_map_entries = ts_field_map_entries,
    .symbol_metadata = ts_symbol_metadata,
    .public_symbol_map = ts_symbol_map,
    .alias_map = ts_non_terminal_alias_map,
    .alias_sequences = &ts_alias_sequences[0][0],
    .lex_modes = ts_lex_modes,
    .lex_fn = ts_lex,
    .primary_state_ids = ts_primary_state_ids,
  };
  return &language;
}
#ifdef __cplusplus
}
#endif
/* ===== END tree-sitter-json v0.24.8 src/parser.c ===== */

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,        \
                    __LINE__, #cond);                                     \
            exit(1);                                                      \
        }                                                                 \
    } while (0)

static bool text_is(const char *src, TSNode node, const char *want) {
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t len = ts_node_end_byte(node) - start;
    return strlen(want) == len && memcmp(src + start, want, len) == 0;
}

static bool type_is(TSNode node, const char *want) {
    return !ts_node_is_null(node) && strcmp(ts_node_type(node), want) == 0;
}

static bool sexp_is(TSTree *tree, const char *want) {
    char *got = ts_node_string(ts_tree_root_node(tree));
    const bool same = strcmp(got, want) == 0;
    if (!same) fprintf(stderr, "s-expression: %s\n", got);
    free(got);
    return same;
}

static uint32_t offset_of(const char *src, const char *needle) {
    const char *at = strstr(src, needle);
    CHECK(at != NULL);
    return (uint32_t)(at - src);
}

static TSTree *parse(TSParser *parser, TSTree *old, const char *src) {
    TSTree *tree = ts_parser_parse_string(parser, old, src, (uint32_t)strlen(src));
    CHECK(tree != NULL);
    return tree;
}

static const char *read_all(void *payload, uint32_t byte, TSPoint point,
                            uint32_t *bytes_read) {
    const char *src = payload;
    const size_t len = strlen(src);
    (void)point;
    *bytes_read = byte < len ? (uint32_t)(len - byte) : 0;
    return byte < len ? src + byte : "";
}

static bool cancel_parse(TSParseState *state) {
    ++*(unsigned *)state->payload;
    return true;
}

static void grammar(const TSLanguage *json) {
    const uint32_t abi = ts_language_abi_version(json);
    CHECK(abi == LANGUAGE_VERSION && abi == 14);
    CHECK(abi >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION &&
          abi <= TREE_SITTER_LANGUAGE_VERSION);
    CHECK(ts_language_symbol_count(json) == SYMBOL_COUNT);
    CHECK(ts_language_field_count(json) == FIELD_COUNT);
    const TSFieldId key = ts_language_field_id_for_name(json, "key", 3);
    CHECK(key != 0 &&
          strcmp(ts_language_field_name_for_id(json, key), "key") == 0);
    CHECK(ts_language_field_id_for_name(json, "nope", 4) == 0);
    const TSSymbol pair = ts_language_symbol_for_name(json, "pair", 4, true);
    CHECK(pair != 0 && strcmp(ts_language_symbol_name(json, pair), "pair") == 0);
}

static void navigate(TSParser *parser) {
    static const char src[] = "{\"a\": [1, true, null], \"b\": {\"c\": \"x\\n\"}}";
    TSTree *tree = parse(parser, NULL, src);
    CHECK(sexp_is(tree,
                  "(document (object "
                  "(pair key: (string (string_content)) "
                  "value: (array (number) (true) (null))) "
                  "(pair key: (string (string_content)) "
                  "value: (object (pair key: (string (string_content)) "
                  "value: (string (string_content) (escape_sequence)))))))"));
    const TSNode root = ts_tree_root_node(tree);
    CHECK(type_is(root, "document") && !ts_node_has_error(root));
    CHECK(ts_node_end_byte(root) == sizeof src - 1);

    const TSNode object = ts_node_named_child(root, 0);
    CHECK(type_is(object, "object") && ts_node_named_child_count(object) == 2);
    const TSNode pair = ts_node_named_child(object, 0);
    const TSNode key = ts_node_child_by_field_name(pair, "key", 3);
    const TSNode value = ts_node_child_by_field_name(pair, "value", 5);
    CHECK(type_is(key, "string") && text_is(src, key, "\"a\""));
    CHECK(type_is(value, "array") && text_is(src, value, "[1, true, null]"));
    CHECK(ts_node_eq(ts_node_parent(value), pair));
    CHECK(type_is(ts_node_named_child(value, 1), "true"));
    CHECK(type_is(ts_node_next_named_sibling(ts_node_named_child(value, 1)),
                  "null"));
    const TSNode escape = ts_node_named_descendant_for_byte_range(
        root, offset_of(src, "\\n"), offset_of(src, "\\n") + 2);
    CHECK(type_is(escape, "escape_sequence") && text_is(src, escape, "\\n"));
    CHECK(ts_node_start_point(escape).row == 0 &&
          ts_node_start_point(escape).column == offset_of(src, "\\n"));

    /* A depth-first walk with a tree cursor. */
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    unsigned named = 0, keys = 0, max_depth = 0;
    for (;;) {
        const TSNode node = ts_tree_cursor_current_node(&cursor);
        const char *field = ts_tree_cursor_current_field_name(&cursor);
        named += ts_node_is_named(node);
        keys += field != NULL && strcmp(field, "key") == 0;
        if (ts_tree_cursor_current_depth(&cursor) > max_depth)
            max_depth = ts_tree_cursor_current_depth(&cursor);
        if (ts_tree_cursor_goto_first_child(&cursor)) continue;
        while (!ts_tree_cursor_goto_next_sibling(&cursor)) {
            if (!ts_tree_cursor_goto_parent(&cursor)) goto walked;
        }
    }
walked:
    ts_tree_cursor_delete(&cursor);
    CHECK(named == 19 && keys == 3 && max_depth == 6);
    ts_tree_delete(tree);
}

static void queries(TSParser *parser, const TSLanguage *json) {
    static const char src[] = "{\"a\": [1, true, null], \"b\": {\"c\": \"x\\n\"}}";
    TSTree *tree = parse(parser, NULL, src);
    static const char pattern[] =
        "(pair key: (string (string_content) @key) value: (_) @value)";
    uint32_t error_offset = 0;
    TSQueryError error = TSQueryErrorNone;
    TSQuery *query = ts_query_new(json, pattern, sizeof pattern - 1,
                                  &error_offset, &error);
    CHECK(query != NULL && error == TSQueryErrorNone);
    CHECK(ts_query_pattern_count(query) == 1 && ts_query_capture_count(query) == 2);

    TSQueryCursor *cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));
    TSQueryMatch match;
    char keys[4] = "";
    size_t n = 0;
    while (ts_query_cursor_next_match(cursor, &match)) {
        CHECK(match.capture_count == 2 && n < 3);
        uint32_t len = 0;
        const char *name =
            ts_query_capture_name_for_id(query, match.captures[0].index, &len);
        CHECK(len == 3 && memcmp(name, "key", 3) == 0);
        keys[n++] = src[ts_node_start_byte(match.captures[0].node)];
        name = ts_query_capture_name_for_id(query, match.captures[1].index, &len);
        CHECK(len == 5 && memcmp(name, "value", 5) == 0);
    }
    CHECK(strcmp(keys, "abc") == 0);

    /* Only the array's elements, within the byte range of the array. */
    static const char items[] = "(array (_) @item)";
    TSQuery *item_query = ts_query_new(json, items, sizeof items - 1,
                                       &error_offset, &error);
    CHECK(item_query != NULL);
    ts_query_cursor_exec(cursor, item_query, ts_tree_root_node(tree));
    TSQueryMatch item;
    uint32_t capture_index = 0;
    unsigned count = 0;
    while (ts_query_cursor_next_capture(cursor, &item, &capture_index)) count++;
    CHECK(count == 3);
    ts_query_delete(item_query);

    /* A node type the grammar does not have. */
    static const char bogus[] = "(pair (bogus))";
    CHECK(ts_query_new(json, bogus, sizeof bogus - 1, &error_offset, &error) ==
          NULL);
    CHECK(error == TSQueryErrorNodeType && error_offset == 7);

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);
}

static void incremental(TSParser *parser) {
    static const char before[] = "{\"a\": [1, true, null], \"b\": 2}";
    static const char after[] = "{\"a\": [1, false, null], \"b\": 2}";
    TSTree *old_tree = parse(parser, NULL, before);
    const uint32_t start = offset_of(before, "true");
    TSInputEdit edit;
    edit.start_byte = start;
    edit.old_end_byte = start + 4;
    edit.new_end_byte = start + 5;
    edit.start_point.row = 0;
    edit.start_point.column = start;
    edit.old_end_point.row = 0;
    edit.old_end_point.column = start + 4;
    edit.new_end_point.row = 0;
    edit.new_end_point.column = start + 5;
    ts_tree_edit(old_tree, &edit);
    CHECK(ts_node_has_changes(ts_tree_root_node(old_tree)));

    TSTree *new_tree = parse(parser, old_tree, after);
    CHECK(!ts_node_has_error(ts_tree_root_node(new_tree)));
    CHECK(type_is(ts_node_named_descendant_for_byte_range(
                      ts_tree_root_node(new_tree), start, start + 5),
                  "false"));
    /* "b" sits after the edit and moved by one byte. */
    const TSNode b = ts_node_named_child(
        ts_node_named_child(ts_tree_root_node(new_tree), 0), 1);
    CHECK(text_is(after, ts_node_child_by_field_name(b, "key", 3), "\"b\""));

    uint32_t count = 0;
    TSRange *ranges = ts_tree_get_changed_ranges(old_tree, new_tree, &count);
    CHECK(count >= 1);
    CHECK(ranges[0].start_byte <= start &&
          ranges[count - 1].end_byte >= start + 5);
    CHECK(ranges[count - 1].end_byte <= offset_of(after, "\"b\""));
    free(ranges);
    ts_tree_delete(old_tree);
    ts_tree_delete(new_tree);
}

static void errors(TSParser *parser) {
    TSTree *missing = parse(parser, NULL, "[1, 2");
    CHECK(ts_node_has_error(ts_tree_root_node(missing)));
    CHECK(sexp_is(missing, "(document (array (number) (number) (MISSING \"]\")))"));
    const TSNode array = ts_node_named_child(ts_tree_root_node(missing), 0);
    const TSNode last = ts_node_child(array, ts_node_child_count(array) - 1);
    CHECK(ts_node_is_missing(last) && strcmp(ts_node_type(last), "]") == 0);
    ts_tree_delete(missing);

    TSTree *broken = parse(parser, NULL, "[1, @, 2]");
    const TSNode root = ts_tree_root_node(broken);
    CHECK(ts_node_has_error(root));
    const TSNode error = ts_node_descendant_for_byte_range(root, 4, 5);
    CHECK(ts_node_is_error(error) && strcmp(ts_node_type(error), "ERROR") == 0);
    ts_tree_delete(broken);
}

static void cancellation(TSParser *parser) {
    /* Long enough for the parser to check the callback. */
    enum { ITEMS = 4000 };
    static char src[2 * ITEMS + 2];
    src[0] = '[';
    for (int i = 0; i < ITEMS; i++) {
        src[1 + 2 * i] = '7';
        src[2 + 2 * i] = i + 1 < ITEMS ? ',' : ']';
    }
    TSInput input;
    input.payload = src;
    input.read = read_all;
    input.encoding = TSInputEncodingUTF8;
    input.decode = NULL;

    unsigned calls = 0;
    TSParseOptions options;
    options.payload = &calls;
    options.progress_callback = cancel_parse;
    CHECK(ts_parser_parse_with_options(parser, NULL, input, options) == NULL);
    CHECK(calls == 1);

    ts_parser_reset(parser);
    options.progress_callback = NULL;
    TSTree *tree = ts_parser_parse_with_options(parser, NULL, input, options);
    CHECK(tree != NULL && !ts_node_has_error(ts_tree_root_node(tree)));
    CHECK(ts_node_named_child_count(
              ts_node_named_child(ts_tree_root_node(tree), 0)) == ITEMS);
    ts_tree_delete(tree);
}

int main(void) {
    const TSLanguage *json = tree_sitter_json();
    grammar(json);

    TSParser *parser = ts_parser_new();
    CHECK(parser != NULL);
    CHECK(ts_parser_set_language(parser, json) && ts_parser_language(parser) == json);
    navigate(parser);
    queries(parser, json);
    incremental(parser);
    errors(parser);
    cancellation(parser);
    ts_parser_delete(parser);
    puts("ok");
    return 0;
}
