#include "hedon.h"

char* buffer;

void init_parser() {
  buffer = malloc(DEFAULT_BUFFER_SIZE);
}

void read_to_buffer(FILE* f) {
  char* bufferstart = buffer;
  for (;;) {
    char c = fgetc(f);
    if (c == EOF) break;
    *buffer = c;
    buffer++;
  }
  *buffer = '\0';
  buffer = bufferstart;
}

//
// Token
//

Token* new_token(TokenKind kind) {
  Token* t = malloc(sizeof(Token));
  t->kind = kind;
  return t;
}

Token* new_token_name(char* name) {
  Token* t = new_token(TOKEN_NAME);
  t->name = name;
  return t;
}

Token* new_token_quot(Stack* s) {
  Token* t = new_token(TOKEN_QUOT);
  t->quot = s;
  return t;
}

//
// parser
//

char bgetc() {
  char c = *buffer;
  buffer++;
  return c;
}
void bungetc() {
  buffer--;
}

void skip_spaces() {
  for (;;) {
    char c = bgetc();
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
    bungetc();
    break;
  }
}

Token* parse_name() {
  skip_spaces();
  char token[256] = {};
  int i;
  bool instrlit = false;
  for (i=0; ; i++) {
    if (i >= 256-1) error("hedon word length should be <256.");
    char c = bgetc();
    if (c == ' ' && !instrlit) break;
    if (c == '\n' || c == '\r' || c == '\t' || c == '\0' || c == EOF) break;
    if (c == '"') instrlit = !instrlit;
    token[i] = c;
  }
  if (strlen(token) == 0) return NULL;
  return new_token_name(strdup(token));
}

Token* parse_quot() {
  Stack* s = new_stack();
  for (;;) {
    Token* t = parse_token();
    if (t->kind == TOKEN_NAME && strcmp(t->name, ";") == 0) break;
    if (t->kind == TOKEN_NAME && strcmp(t->name, "]") == 0) break;
    push(s, t);
  }
  return new_token_quot(s);
}

Token* parse_rem() {
  for (;;) {
    char c = bgetc();
    if (c == '\n') break;
  }
  return NULL;
}

Token* parse_token() {
  Token* t = parse_name();
  if (t == NULL) return NULL;
  BUILTIN_PARSE_WORD("[", parse_quot);
  BUILTIN_PARSE_WORD("rem", parse_rem);
  return t;
}
