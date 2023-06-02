#include "compiler.h"

#include "common.h"
#include "memory.h"
#include "scanner.h"
#include "slots.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef struct {
  Token current;
  Token previous;
  bool hadError;
  bool panicMode;
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_CONDITIONAL,
  PREC_OR,
  PREC_AND,
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

typedef struct {
  Token name;
  int depth;
  bool isCaptured;
} Local;

typedef struct {
  uint8_t index;
  bool isLocal;
} Upvalue;

typedef enum {
  TYPE_FUNCTION,
  TYPE_INITIALIZER,
  TYPE_METHOD,
  TYPE_SCRIPT
} FunctionType;

typedef struct Compiler {
  struct Compiler* enclosing;
  ObjFunction* function;
  FunctionType type;

  Table stringConstants;
  Local locals[UINT8_COUNT];
  int localCount;
  Upvalue upvalues[UINT8_COUNT];
  int scopeDepth;
  SlotUsage usage;

  int innermostLoopStart;
  int innermostLoopScopeDepth;
} Compiler;

typedef struct ClassCompiler {
  struct ClassCompiler* enclosing;
  bool hasSuperclass;
} ClassCompiler;

Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;

static Chunk* currentChunk() {
  return &current->function->chunk;
}

static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;
  fprintf(stderr, "[line %d] Error", token->line);

  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

static void error(const char* message) {
  errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

static void advance() {
  parser.previous = parser.current;

  while (true) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    errorAtCurrent(parser.current.start);
  }
}

static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitOp(OpCode op) {
  SlotUsage usage = getUsage(op);
  if (current->usage.delta + usage.peak > current->usage.peak)
    current->usage.peak = current->usage.delta + usage.peak;
  current->usage.delta += usage.delta;
  emitByte((uint8_t)op);
}

static void emitShort(uint16_t bytes) {
  emitByte((uint8_t)(bytes & 0xFF));
  emitByte((uint8_t)((bytes >> 8) & 0xFF));
}

static void emitLoop(int loopStart) {
  emitOp(OP_LOOP);
  int offset = currentChunk()->count - loopStart + 2;
  if (offset > UINT16_MAX) error("Loop body too large.");
  emitShort(offset);
}

static int emitJump(uint8_t jumpOp) {
  emitOp(jumpOp);
  emitByte(0xFF);
  emitByte(0xFF);
  return currentChunk()->count - 2;
}

static void emitReturn() {
  emitOp(current->type == TYPE_INITIALIZER ? OP_GET_THIS : OP_NIL);
  emitOp(OP_RETURN);
}

static uint16_t makeConstant(Value value) {
  int index = addConstant(currentChunk(), value);
  if (index == CONSTANTS_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint16_t)index;
}

static void emitConstant(Value value) {
  uint16_t index = makeConstant(value);
  emitOp(OP_CONSTANT);
  emitShort(index);
}

static void patchJump(int offset) {
  int jump = currentChunk()->count - offset - 2;

  if (jump > UINT16_MAX) error("Too much code to jump over.");

  currentChunk()->code[offset] = jump & 0xFF;
  currentChunk()->code[offset + 1] = (jump >> 8) & 0xFF;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
  compiler->enclosing = current;
  compiler->function = NULL;
  compiler->type = type;
  initTable(&compiler->stringConstants);
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  compiler->usage = (SlotUsage){0, 0};
  compiler->function = newFunction();
  current = compiler;
  if (type != TYPE_SCRIPT) {
    current->function->name =
        copyString(parser.previous.start, parser.previous.length);
  }

  Local* local = &current->locals[current->localCount++];
  local->depth = 0;
  local->isCaptured = false;
  if (type != TYPE_FUNCTION) {
    local->name.start = "this";
    local->name.length = 4;
  } else {
    local->name.start = "";
    local->name.length = 0;
  }

  compiler->innermostLoopStart = -1;
  compiler->innermostLoopScopeDepth = 0;
}

static ObjFunction* endCompiler() {
  emitReturn();
  ObjFunction* function = current->function;
  function->chunk.slots = current->usage.peak;
  freeTable(&current->stringConstants);

#ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(
        currentChunk(),
        function->name != NULL ? function->name->chars : "<script>");
  }
#endif

  current = current->enclosing;
  return function;
}

static void beginScope() {
  current->scopeDepth++;
}

static void endScope() {
  current->scopeDepth--;

  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
             current->scopeDepth) {
    if (current->locals[current->localCount - 1].isCaptured) {
      emitOp(OP_CLOSE_UPVALUE);
    } else {
      emitOp(OP_POP);
    }
    current->localCount--;
  }
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static uint16_t identifierConstant(Token* name) {
  ObjString* string = copyString(name->start, name->length);
  Value indexValue;

  if (tableGet(&current->stringConstants, OBJ_VAL(string), &indexValue))
    return (uint16_t)AS_NUMBER(indexValue);

  uint16_t index = makeConstant(OBJ_VAL(string));
  tableSet(
      &current->stringConstants, OBJ_VAL(string), NUMBER_VAL((double)index));
  return index;
}

static uint16_t globalIdentifier(Value identifier) {
  Value index;

  if (tableGet(&vm.globalNames, identifier, &index))
    return (uint16_t)AS_NUMBER(index);

  uint16_t newIndex = (uint16_t)vm.globalValues.count;
  push(identifier);
  writeValueArray(&vm.globalValues, UNDEFINED_VAL);
  tableSet(&vm.globalNames, identifier, NUMBER_VAL((double)newIndex));
  pop();
  return newIndex;
}

static bool identifiersEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (identifiersEqual(name, &local->name)) {
      if (local->depth == -1) {
        error("Can't read local variable in its own initializer.");
      }
      return i;
    }
  }

  return -1;
}

static int addUpvalue(Compiler* compiler, uint8_t index, bool isLocal) {
  int upvalueCount = compiler->function->upvalueCount;

  for (int i = 0; i < upvalueCount; i++) {
    Upvalue* upvalue = &compiler->upvalues[i];
    if (upvalue->index == index && upvalue->isLocal == isLocal) return i;
  }

  if (upvalueCount == UINT8_COUNT) {
    error("Too many closure variables in function.");
    return 0;
  }

  compiler->upvalues[upvalueCount].isLocal = isLocal;
  compiler->upvalues[upvalueCount].index = index;
  return compiler->function->upvalueCount++;
}

static int resolveUpvalue(Compiler* compiler, Token* name) {
  if (compiler->enclosing == NULL) return -1;

  int local = resolveLocal(compiler->enclosing, name);
  if (local != -1) {
    compiler->enclosing->locals[local].isCaptured = true;
    return addUpvalue(compiler, (uint8_t)local, true);
  }

  int upvalue = resolveUpvalue(compiler->enclosing, name);
  if (upvalue != -1) return addUpvalue(compiler, (uint8_t)upvalue, false);

  return -1;
}

static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = -1;
  local->isCaptured = false;
}

static void declareVariable() {
  if (current->scopeDepth == 0) return;

  Token* name = &parser.previous;
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) break;

    if (identifiersEqual(name, &local->name)) {
      error("Already a variable with this name in this scope.");
    }
  }

  addLocal(*name);
}

static uint16_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  declareVariable();
  if (current->scopeDepth > 0) return 0;

  return identifierConstant(&parser.previous);
}

static void markInitialized() {
  if (current->scopeDepth == 0) return;
  current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint16_t global) {
  if (current->scopeDepth > 0) {
    markInitialized();
    return;
  }

  emitOp(OP_DEFINE_GLOBAL);
  emitShort(globalIdentifier(currentChunk()->constants.values[global]));
}

static uint8_t argumentList() {
  uint8_t argCount = 0;
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      expression();
      if (argCount == 255) error("Can't have more than 255 arguments.");
      argCount++;
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
  return argCount;
}

static void and_(bool canAssign __attribute__((unused))) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);

  emitOp(OP_POP);
  parsePrecedence(PREC_AND);

  patchJump(endJump);
}

static void simplify(OpCode first, OpCode second, OpCode combined) {
  if (currentChunk()->code[currentChunk()->count - 1] == first) {
    current->usage.delta -= getUsage(first).delta;
    amendChunk(currentChunk(), 1);
    emitOp(combined);
  } else {
    emitOp(second);
  }
}

static void binary(bool canAssign __attribute__((unused))) {
  TokenType operatorType = parser.previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL: emitOp(OP_NOT_EQUAL); break;
    case TOKEN_EQUAL_EQUAL:
      simplify(OP_CONSTANT_ZERO, OP_EQUAL, OP_EQUAL_ZERO);
      break;
    case TOKEN_GREATER: emitOp(OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitOp(OP_GREATER_EQUAL); break;
    case TOKEN_LESS: emitOp(OP_LESS); break;
    case TOKEN_LESS_EQUAL: emitOp(OP_LESS_EQUAL); break;
    case TOKEN_PLUS: simplify(OP_CONSTANT_ONE, OP_ADD, OP_ADD_ONE); break;
    case TOKEN_MINUS:
      simplify(OP_CONSTANT_ONE, OP_SUBTRACT, OP_SUBTRACT_ONE);
      break;
    case TOKEN_STAR:
      simplify(OP_CONSTANT_TWO, OP_MULTIPLY, OP_MULTIPLY_TWO);
      break;
    case TOKEN_SLASH: emitOp(OP_DIVIDE); break;
    default: return;
  }
}

static void emitCall(uint8_t argCount) {
  emitOp(OP_CALL);
  emitByte(argCount);
  current->usage.delta -= argCount;
}

static void call(bool canAssign __attribute__((unused))) {
  emitCall(argumentList());
}

static void dot(bool canAssign) {
  consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
  uint16_t name = identifierConstant(&parser.previous);

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitOp(OP_SET_PROPERTY);
    emitShort(name);
  } else if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    emitOp(OP_INVOKE);
    emitShort(name);
    emitByte(argCount);
  } else {
    emitOp(OP_GET_PROPERTY);
    emitShort(name);
  }
}

static void literal(bool canAssign __attribute__((unused))) {
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitOp(OP_FALSE); break;
    case TOKEN_NIL: emitOp(OP_NIL); break;
    case TOKEN_TRUE: emitOp(OP_TRUE); break;
    default: return;
  }
}

static void conditional(bool canAssign __attribute__((unused))) {
  int thenJump = emitJump(OP_JUMP_IF_FALSE);

  parsePrecedence(PREC_CONDITIONAL);

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  current->usage.delta--;
  consume(
      TOKEN_COLON, "Expect ':' after then branch of conditional operator.");
  parsePrecedence(PREC_ASSIGNMENT);
  patchJump(elseJump);
}

static void grouping(bool canAssign __attribute__((unused))) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void number(bool canAssign __attribute__((unused))) {
  double value = strtod(parser.previous.start, NULL);
  if (value == 0) {
    emitOp(OP_CONSTANT_ZERO);
  } else if (value == 1) {
    emitOp(OP_CONSTANT_ONE);
  } else if (value == 2) {
    emitOp(OP_CONSTANT_TWO);
  } else if (value == 3) {
    emitOp(OP_CONSTANT_THREE);
  } else if (value == 4) {
    emitOp(OP_CONSTANT_FOUR);
  } else if (value == 5) {
    emitOp(OP_CONSTANT_FIVE);
  } else {
    emitConstant(NUMBER_VAL(value));
  }
}

static void or_(bool canAssign __attribute__((unused))) {
  int elseJump = emitJump(OP_JUMP_IF_FALSE);
  int endJump = emitJump(OP_JUMP);

  patchJump(elseJump);
  emitOp(OP_POP);

  parsePrecedence(PREC_OR);
  patchJump(endJump);
}

static void string(bool canAssign __attribute__((unused))) {
  emitConstant(OBJ_VAL(
      copyString(parser.previous.start + 1, parser.previous.length - 2)));
}

static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  int arg = resolveLocal(current, &name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else if ((arg = resolveUpvalue(current, &name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = globalIdentifier(OBJ_VAL(copyString(name.start, name.length)));
    if (canAssign && match(TOKEN_EQUAL)) {
      expression();
      emitOp(OP_SET_GLOBAL);
      emitShort((uint16_t)arg);
    } else {
      emitOp(OP_GET_GLOBAL);
      emitShort((uint16_t)arg);
    }
    return;
  }

  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitOp(setOp);
    emitByte((uint8_t)arg);
  } else if (getOp == OP_GET_LOCAL && arg == 0) {
    emitOp(OP_GET_THIS);
  } else {
    emitOp(getOp);
    emitByte((uint8_t)arg);
  }
}

static void variable(bool canAssign __attribute__((unused))) {
  namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(const char* text) {
  Token token;
  token.start = text;
  token.length = (int)strlen(text);
  return token;
}

static void interpolate(bool canAssign __attribute__((unused))) {
  bool init = false;
  do {
    emitConstant(OBJ_VAL(
        copyString(parser.previous.start + 1, parser.previous.length - 3)));
    if (init)
      emitOp(OP_ADD);
    else
      init = true;
    if (*parser.current.start == '}') errorAtCurrent("Expect expression.");
    namedVariable(syntheticToken("str"), false);
    expression();
    emitCall(1);
    emitOp(OP_ADD);
  } while (match(TOKEN_INTERPOLATE));
  consume(TOKEN_STRING, "Expect end of string interpolation.");
  string(false);
  emitOp(OP_ADD);
}

static void super_(bool canAssign __attribute__((unused))) {
  if (currentClass == NULL) {
    error("Can't use 'super' outside of a class.");
  } else if (!currentClass->hasSuperclass) {
    error("Can't use 'super' in a class with no superclass.");
  }

  consume(TOKEN_DOT, "Expect '.' after 'super'.");
  consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
  uint16_t name = identifierConstant(&parser.previous);

  namedVariable(syntheticToken("this"), false);
  if (match(TOKEN_LEFT_PAREN)) {
    uint8_t argCount = argumentList();
    namedVariable(syntheticToken("super"), false);
    emitOp(OP_SUPER_INVOKE);
    emitShort(name);
    emitByte(argCount);
  } else {
    namedVariable(syntheticToken("super"), false);
    emitOp(OP_GET_SUPER);
    emitShort(name);
  }
}

static void this_(bool canAssign __attribute__((unused))) {
  if (currentClass == NULL) {
    error("Can't use 'this' outside of a class.");
    return;
  }

  variable(false);
}

static void unary(bool canAssign __attribute__((unused))) {
  TokenType operatorType = parser.previous.type;

  parsePrecedence(PREC_UNARY);
  switch (operatorType) {
    case TOKEN_BANG: emitOp(OP_NOT); break;
    case TOKEN_MINUS:
      simplify(OP_CONSTANT_ONE, OP_NEGATE, OP_CONSTANT_NEGATIVE_ONE);
      break;
    default: return;
  }
}

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
    [TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
    [TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
    [TOKEN_QUESTION] = {NULL, conditional, PREC_CONDITIONAL},
    [TOKEN_COLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
    [TOKEN_DOT] = {NULL, dot, PREC_CALL},
    [TOKEN_MINUS] = {unary, binary, PREC_TERM},
    [TOKEN_PLUS] = {NULL, binary, PREC_TERM},
    [TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
    [TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
    [TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
    [TOKEN_BANG] = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
    [TOKEN_INTERPOLATE] = {interpolate, NULL, PREC_NONE},
    [TOKEN_STRING] = {string, NULL, PREC_NONE},
    [TOKEN_NUMBER] = {number, NULL, PREC_NONE},
    [TOKEN_AND] = {NULL, and_, PREC_AND},
    [TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
    [TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
    [TOKEN_FALSE] = {literal, NULL, PREC_NONE},
    [TOKEN_FOR] = {NULL, NULL, PREC_NONE},
    [TOKEN_FUN] = {NULL, NULL, PREC_NONE},
    [TOKEN_IF] = {NULL, NULL, PREC_NONE},
    [TOKEN_NIL] = {literal, NULL, PREC_NONE},
    [TOKEN_OR] = {NULL, or_, PREC_OR},
    [TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
    [TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
    [TOKEN_SUPER] = {super_, NULL, PREC_NONE},
    [TOKEN_THIS] = {this_, NULL, PREC_NONE},
    [TOKEN_TRUE] = {literal, NULL, PREC_NONE},
    [TOKEN_VAR] = {NULL, NULL, PREC_NONE},
    [TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
    [TOKEN_EOF] = {NULL, NULL, PREC_NONE},
};

static void parsePrecedence(Precedence precedence) {
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  if (canAssign && match(TOKEN_EQUAL)) error("Invalid assignment target.");
}

static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

static void expression() {
  parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) declaration();

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
  Compiler compiler;
  initCompiler(&compiler, type);
  beginScope();

  consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
  if (!check(TOKEN_RIGHT_PAREN)) {
    do {
      current->function->arity++;
      if (current->function->arity > 255) {
        errorAtCurrent("Can't have more than 255 parameters.");
      }
      parseVariable("Expect parameter name.");
      defineVariable(0);
    } while (match(TOKEN_COMMA));
  }
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
  block();

  ObjFunction* function = endCompiler();
  uint16_t index = makeConstant(OBJ_VAL(function));
  emitOp(OP_CLOSURE);
  emitShort(index);

  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(compiler.upvalues[i].isLocal ? true : false);
    emitByte(compiler.upvalues[i].index);
  }
}

static void method() {
  consume(TOKEN_IDENTIFIER, "Expect method name.");
  uint16_t constant = identifierConstant(&parser.previous);

  FunctionType type = TYPE_METHOD;
  if (parser.previous.length == 4 &&
      memcmp(parser.previous.start, "init", 4) == 0) {
    type = TYPE_INITIALIZER;
  }

  function(type);
  emitOp(OP_METHOD);
  emitShort(constant);
}

static void classDeclaration() {
  consume(TOKEN_IDENTIFIER, "Expect class name.");
  Token className = parser.previous;
  uint16_t nameConstant = identifierConstant(&parser.previous);
  declareVariable();

  emitOp(OP_CLASS);
  emitShort(nameConstant);
  defineVariable(nameConstant);

  ClassCompiler classCompiler;
  classCompiler.hasSuperclass = false;
  classCompiler.enclosing = currentClass;
  currentClass = &classCompiler;

  if (match(TOKEN_LESS)) {
    consume(TOKEN_IDENTIFIER, "Expect superclass name.");
    variable(false);

    if (identifiersEqual(&className, &parser.previous)) {
      error("A class can't inherit from itself.");
    }

    beginScope();
    addLocal(syntheticToken("super"));
    defineVariable(0);

    namedVariable(className, false);
    emitOp(OP_INHERIT);
    classCompiler.hasSuperclass = true;
  }

  namedVariable(className, false);
  consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) method();
  consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
  emitOp(OP_POP);

  if (classCompiler.hasSuperclass) endScope();

  currentClass = currentClass->enclosing;
}

static void funDeclaration() {
  uint16_t global = parseVariable("Expect function name.");
  markInitialized();
  function(TYPE_FUNCTION);
  defineVariable(global);
}

static void varDeclaration() {
  uint16_t global = parseVariable("Expect variable name.");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    emitOp(OP_NIL);
  }
  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

  defineVariable(global);
}

static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  emitOp(OP_POP);
}

static void forStatement() {
  beginScope();
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");
  if (match(TOKEN_VAR)) {
    varDeclaration();
  } else if (!match(TOKEN_SEMICOLON)) {
    expressionStatement();
  }

  int surroundingLoopStart = current->innermostLoopStart;
  int surroundingLoopScopeDepth = current->innermostLoopScopeDepth;

  current->innermostLoopStart = currentChunk()->count;
  current->innermostLoopScopeDepth = current->scopeDepth;

  int exitJump = -1;
  if (!match(TOKEN_SEMICOLON)) {
    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitOp(OP_POP);
  }

  if (!match(TOKEN_RIGHT_PAREN)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = currentChunk()->count;
    expression();
    emitOp(OP_POP);
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

    emitLoop(current->innermostLoopStart);
    current->innermostLoopStart = incrementStart;
    patchJump(bodyJump);
  }

  statement();
  emitLoop(current->innermostLoopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitOp(OP_POP);
  }

  current->innermostLoopStart = surroundingLoopStart;
  current->innermostLoopScopeDepth = surroundingLoopScopeDepth;
  endScope();
}

static void ifStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitOp(OP_POP);
  statement();

  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  current->usage.delta++;
  emitOp(OP_POP);

  if (match(TOKEN_ELSE)) statement();
  patchJump(elseJump);
}

static void switchStatement() {
  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after value.");
  consume(TOKEN_LEFT_BRACE, "Expect '{' before switch cases.");

  int state = 0;
  int caseCount = 0;
  int caseCapacity = 0;
  int previousCaseSkip = -1;
  int* caseEnds = NULL;

  while (!match(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    if (match(TOKEN_CASE) || match(TOKEN_DEFAULT)) {
      TokenType caseType = parser.previous.type;

      if (state == 2)
        error("Can't have another case or default after the default case.");
      if (state == 1) {
        caseEnds[caseCount++] = emitJump(OP_JUMP);
        patchJump(previousCaseSkip);
        current->usage.delta++;
        emitOp(OP_POP);
      }
      if (caseType == TOKEN_CASE) {
        if (caseCount == caseCapacity) {
          int oldCapacity = caseCapacity;

          caseCapacity = GROW_CAPACITY(oldCapacity);
          caseEnds = GROW_ARRAY(int, caseEnds, oldCapacity, caseCapacity);
        }
        state = 1;
        emitOp(OP_DUP);
        expression();
        consume(TOKEN_COLON, "Expect ':' after case value.");
        emitOp(OP_EQUAL);
        previousCaseSkip = emitJump(OP_JUMP_IF_FALSE);
        current->usage.delta += 2;
        emitOp(OP_POP);
        emitOp(OP_POP);
      } else {
        state = 2;
        consume(TOKEN_COLON, "Expect ':' after default.");
        previousCaseSkip = -1;
        emitOp(OP_POP);
      }
    } else {
      if (state == 0) errorAtCurrent("Can't have statements before any case.");
      statement();
    }
  }
  if (state == 1) {
    caseEnds[caseCount++] = emitJump(OP_JUMP);
    patchJump(previousCaseSkip);
  }
  if (state < 2) emitOp(OP_POP);
  for (int i = 0; i < caseCount; i++) patchJump(caseEnds[i]);
  FREE_ARRAY(int*, caseEnds, caseCapacity);
}

static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after value.");
  emitOp(OP_PRINT);
}

static void returnStatement() {
  if (current->type == TYPE_SCRIPT) {
    error("Can't return from top-level code.");
  }

  if (match(TOKEN_SEMICOLON)) {
    emitReturn();
  } else {
    if (current->type == TYPE_INITIALIZER) {
      error("Can't return a value from an initializer.");
    }

    expression();
    consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
    emitOp(OP_RETURN);
  }
}

static void whileStatement() {
  int surroundingLoopStart = current->innermostLoopStart;
  int surroundingLoopScopeDepth = current->innermostLoopScopeDepth;

  current->innermostLoopStart = currentChunk()->count;
  current->innermostLoopScopeDepth = current->scopeDepth;

  consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitOp(OP_POP);
  statement();
  emitLoop(current->innermostLoopStart);

  patchJump(exitJump);
  emitOp(OP_POP);

  current->innermostLoopStart = surroundingLoopStart;
  current->innermostLoopScopeDepth = surroundingLoopScopeDepth;
}

static void continueStatement() {
  if (current->innermostLoopStart == -1)
    error("Can't use 'continue' outside of a loop.");
  consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
  for (int i = current->localCount - 1;
       i >= 0 && current->locals[i].depth > current->innermostLoopScopeDepth;
       i--)
    emitByte(OP_POP);
  emitLoop(current->innermostLoopStart);
}

static void synchronize() {
  parser.panicMode = false;

  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;
    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN: return;
      default:;
    }

    advance();
  }
}

static void declaration() {
  if (match(TOKEN_CLASS)) {
    classDeclaration();
  } else if (match(TOKEN_FUN)) {
    funDeclaration();
  } else if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }

  if (parser.panicMode) synchronize();
}

static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_FOR)) {
    forStatement();
  } else if (match(TOKEN_IF)) {
    ifStatement();
  } else if (match(TOKEN_SWITCH)) {
    switchStatement();
  } else if (match(TOKEN_RETURN)) {
    returnStatement();
  } else if (match(TOKEN_WHILE)) {
    whileStatement();
  } else if (match(TOKEN_CONTINUE)) {
    continueStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

ObjFunction* compile(const char* source) {
  initScanner(source);
  Compiler compiler;
  initCompiler(&compiler, TYPE_SCRIPT);

  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) declaration();

  ObjFunction* function = endCompiler();
  return parser.hadError ? NULL : function;
}

void markCompilerRoots() {
  Compiler* compiler = current;
  while (compiler != NULL) {
    markObject((Obj*)compiler->function);
    compiler = compiler->enclosing;
  }
}
