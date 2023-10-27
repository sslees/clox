#include "debug.h"

#include "object.h"
#include "value.h"
#include "vm.h"

#include <stdio.h>

char* getTokenName(TokenType token) {
  switch (token) {
    case TOKEN_LEFT_PAREN: return "TOKEN_LEFT_PAREN";
    case TOKEN_RIGHT_PAREN: return "TOKEN_RIGHT_PAREN";
    case TOKEN_LEFT_BRACE: return "TOKEN_LEFT_BRACE";
    case TOKEN_RIGHT_BRACE: return "TOKEN_RIGHT_BRACE";
    case TOKEN_QUESTION: return "TOKEN_QUESTION";
    case TOKEN_COLON: return "TOKEN_COLON";
    case TOKEN_COMMA: return "TOKEN_COMMA";
    case TOKEN_DOT: return "TOKEN_DOT";
    case TOKEN_MINUS: return "TOKEN_MINUS";
    case TOKEN_PLUS: return "TOKEN_PLUS";
    case TOKEN_SEMICOLON: return "TOKEN_SEMICOLON";
    case TOKEN_SLASH: return "TOKEN_SLASH";
    case TOKEN_STAR: return "TOKEN_STAR";
    case TOKEN_BANG: return "TOKEN_BANG";
    case TOKEN_BANG_EQUAL: return "TOKEN_BANG_EQUAL";
    case TOKEN_EQUAL: return "TOKEN_EQUAL";
    case TOKEN_EQUAL_EQUAL: return "TOKEN_EQUAL_EQUAL";
    case TOKEN_GREATER: return "TOKEN_GREATER";
    case TOKEN_GREATER_EQUAL: return "TOKEN_GREATER_EQUAL";
    case TOKEN_LESS: return "TOKEN_LESS";
    case TOKEN_LESS_EQUAL: return "TOKEN_LESS_EQUAL";
    case TOKEN_IDENTIFIER: return "TOKEN_IDENTIFIER";
    case TOKEN_INTERPOLATE: return "TOKEN_INTERPOLATE";
    case TOKEN_STRING: return "TOKEN_STRING";
    case TOKEN_NUMBER: return "TOKEN_NUMBER";
    case TOKEN_AND: return "TOKEN_AND";
    case TOKEN_CLASS: return "TOKEN_CLASS";
    case TOKEN_ELSE: return "TOKEN_ELSE";
    case TOKEN_FALSE: return "TOKEN_FALSE";
    case TOKEN_FOR: return "TOKEN_FOR";
    case TOKEN_FUN: return "TOKEN_FUN";
    case TOKEN_IF: return "TOKEN_IF";
    case TOKEN_NIL: return "TOKEN_NIL";
    case TOKEN_OR: return "TOKEN_OR";
    case TOKEN_PRINT: return "TOKEN_PRINT";
    case TOKEN_RETURN: return "TOKEN_RETURN";
    case TOKEN_SUPER: return "TOKEN_SUPER";
    case TOKEN_THIS: return "TOKEN_THIS";
    case TOKEN_TRUE: return "TOKEN_TRUE";
    case TOKEN_VAR: return "TOKEN_VAR";
    case TOKEN_WHILE: return "TOKEN_WHILE";
    case TOKEN_CASE: return "TOKEN_CASE";
    case TOKEN_DEFAULT: return "TOKEN_DEFAULT";
    case TOKEN_SWITCH: return "TOKEN_SWITCH";
    case TOKEN_CONTINUE: return "TOKEN_CONTINUE";
    case TOKEN_ERROR: return "TOKEN_ERROR";
    case TOKEN_EOF: return "TOKEN_EOF";
  }
  return NULL;
}

char* getOpName(OpCode op) {
  switch (op) {
    case OP_CONSTANT: return "OP_CONSTANT";
    case OP_NIL: return "OP_NIL";
    case OP_TRUE: return "OP_TRUE";
    case OP_FALSE: return "OP_FALSE";
    case OP_POP: return "OP_POP";
    case OP_GET_LOCAL: return "OP_GET_LOCAL";
    case OP_SET_LOCAL: return "OP_SET_LOCAL";
    case OP_GET_GLOBAL: return "OP_GET_GLOBAL";
    case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL";
    case OP_SET_GLOBAL: return "OP_SET_GLOBAL";
    case OP_GET_UPVALUE: return "OP_GET_UPVALUE";
    case OP_SET_UPVALUE: return "OP_SET_UPVALUE";
    case OP_GET_PROPERTY: return "OP_GET_PROPERTY";
    case OP_SET_PROPERTY: return "OP_SET_PROPERTY";
    case OP_GET_SUPER: return "OP_GET_SUPER";
    case OP_EQUAL: return "OP_EQUAL";
    case OP_GREATER: return "OP_GREATER";
    case OP_LESS: return "OP_LESS";
    case OP_ADD: return "OP_ADD";
    case OP_SUBTRACT: return "OP_SUBTRACT";
    case OP_MULTIPLY: return "OP_MULTIPLY";
    case OP_DIVIDE: return "OP_DIVIDE";
    case OP_NOT: return "OP_NOT";
    case OP_NEGATE: return "OP_NEGATE";
    case OP_PRINT: return "OP_PRINT";
    case OP_JUMP: return "OP_JUMP";
    case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE";
    case OP_LOOP: return "OP_LOOP";
    case OP_CALL: return "OP_CALL";
    case OP_INVOKE: return "OP_INVOKE";
    case OP_SUPER_INVOKE: return "OP_SUPER_INVOKE";
    case OP_CLOSURE: return "OP_CLOSURE";
    case OP_CLOSE_UPVALUE: return "OP_CLOSE_UPVALUE";
    case OP_RETURN: return "OP_RETURN";
    case OP_CLASS: return "OP_CLASS";
    case OP_INHERIT: return "OP_INHERIT";
    case OP_METHOD: return "OP_METHOD";
    case OP_CONSTANT_NEGATIVE_ONE: return "OP_CONSTANT_NEGATIVE_ONE";
    case OP_CONSTANT_ZERO: return "OP_CONSTANT_ZERO";
    case OP_CONSTANT_ONE: return "OP_CONSTANT_ONE";
    case OP_CONSTANT_TWO: return "OP_CONSTANT_TWO";
    case OP_CONSTANT_THREE: return "OP_CONSTANT_THREE";
    case OP_CONSTANT_FOUR: return "OP_CONSTANT_FOUR";
    case OP_CONSTANT_FIVE: return "OP_CONSTANT_FIVE";
    case OP_ADD_ONE: return "OP_ADD_ONE";
    case OP_SUBTRACT_ONE: return "OP_SUBTRACT_ONE";
    case OP_MULTIPLY_TWO: return "OP_MULTIPLY_TWO";
    case OP_EQUAL_ZERO: return "OP_EQUAL_ZERO";
    case OP_NOT_EQUAL: return "OP_NOT_EQUAL";
    case OP_GREATER_EQUAL: return "OP_GREATER_EQUAL";
    case OP_LESS_EQUAL: return "OP_LESS_EQUAL";
    case OP_GET_THIS: return "OP_GET_THIS";
    case OP_DUP: return "OP_DUP";
  }
  return NULL;
}

void disassembleChunk(Chunk* chunk, const char* name) {
  printf("== %s ==\n", name);

  for (int offset = 0; offset < chunk->count;) {
    offset = disassembleInstr(chunk, offset);
  }
}

static int constantInstr(OpCode op, Chunk* chunk, int offset) {
  uint16_t constant = chunk->code[offset + 1] | chunk->code[offset + 2] << 8;
  printf("%-16s %5d '", getOpName(op), constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 3;
}

static int globalInstr(OpCode op, Chunk* chunk, int offset) {
  uint16_t global = chunk->code[offset + 1] | chunk->code[offset + 2] << 8;
  printf("%-16s %5d '", getOpName(op), global);
  printValue(vm.globalValues.values[global]);
  printf("'\n");
  return offset + 3;
}

static int invokeInstr(OpCode op, Chunk* chunk, int offset) {
  uint16_t constant = chunk->code[offset + 1] | chunk->code[offset + 2] << 8;
  uint8_t argCount = chunk->code[offset + 3];
  printf("%-16s %5d '", getOpName(op), constant);
  printValue(chunk->constants.values[constant]);
  printf("' (%d args)\n", argCount);
  return offset + 4;
}

static int simpleInstr(OpCode op, int offset) {
  printf("%s\n", getOpName(op));
  return offset + 1;
}

static int byteInstr(OpCode op, Chunk* chunk, int offset) {
  uint8_t slot = chunk->code[offset + 1];
  printf("%-16s %5d\n", getOpName(op), slot);
  return offset + 2;
}

static int jumpInstr(OpCode op, int sign, Chunk* chunk, int offset) {
  uint16_t jump = chunk->code[offset + 1];
  jump |= (uint16_t)(chunk->code[offset + 2] << 8);
  printf("%-16s %5d -> %d\n", getOpName(op), offset, offset + 3 + sign * jump);
  return offset + 3;
}

int disassembleInstr(Chunk* chunk, int offset) {
  printf("%04d ", offset);
  int line = getLine(chunk, offset);
  if (offset > 0 && line == getLine(chunk, offset - 1)) {
    printf("   | ");
  } else {
    printf("%4d ", line);
  }

  OpCode opcode = chunk->code[offset];
  switch (opcode) {
    case OP_CONSTANT: return constantInstr(opcode, chunk, offset);
    case OP_NIL: return simpleInstr(opcode, offset);
    case OP_TRUE: return simpleInstr(opcode, offset);
    case OP_FALSE: return simpleInstr(opcode, offset);
    case OP_POP: return simpleInstr(opcode, offset);
    case OP_GET_LOCAL: return byteInstr(opcode, chunk, offset);
    case OP_SET_LOCAL: return byteInstr(opcode, chunk, offset);
    case OP_GET_GLOBAL: return globalInstr(opcode, chunk, offset);
    case OP_DEFINE_GLOBAL: return globalInstr(opcode, chunk, offset);
    case OP_SET_GLOBAL: return globalInstr(opcode, chunk, offset);
    case OP_GET_UPVALUE: return byteInstr(opcode, chunk, offset);
    case OP_SET_UPVALUE: return byteInstr(opcode, chunk, offset);
    case OP_GET_PROPERTY: return constantInstr(opcode, chunk, offset);
    case OP_SET_PROPERTY: return constantInstr(opcode, chunk, offset);
    case OP_GET_SUPER: return constantInstr(opcode, chunk, offset);
    case OP_EQUAL: return simpleInstr(opcode, offset);
    case OP_GREATER: return simpleInstr(opcode, offset);
    case OP_LESS: return simpleInstr(opcode, offset);
    case OP_ADD: return simpleInstr(opcode, offset);
    case OP_SUBTRACT: return simpleInstr(opcode, offset);
    case OP_MULTIPLY: return simpleInstr(opcode, offset);
    case OP_DIVIDE: return simpleInstr(opcode, offset);
    case OP_NOT: return simpleInstr(opcode, offset);
    case OP_NEGATE: return simpleInstr(opcode, offset);
    case OP_PRINT: return simpleInstr(opcode, offset);
    case OP_JUMP: return jumpInstr(opcode, 1, chunk, offset);
    case OP_JUMP_IF_FALSE: return jumpInstr(opcode, 1, chunk, offset);
    case OP_LOOP: return jumpInstr(opcode, -1, chunk, offset);
    case OP_CALL: return byteInstr(opcode, chunk, offset);
    case OP_INVOKE: return invokeInstr(opcode, chunk, offset);
    case OP_SUPER_INVOKE: return invokeInstr(opcode, chunk, offset);
    case OP_CLOSURE: {
      uint16_t constant = chunk->code[++offset];
      constant |= (uint16_t)(chunk->code[(offset += 2) - 1] << 8);
      printf("%-16s %5d ", getOpName(opcode), constant);
      printValue(chunk->constants.values[constant]);
      printf("\n");

      ObjFunction* function = AS_FUNCTION(chunk->constants.values[constant]);
      for (int j = 0; j < function->upvalueCount; j++) {
        int isLocal = chunk->code[offset++];
        int index = chunk->code[offset++];
        printf(
            "%04d      |                     %s %d\n", offset - 2,
            isLocal ? "local" : "upvalue", index);
      }

      return offset;
    }
    case OP_CLOSE_UPVALUE: return simpleInstr(opcode, offset);
    case OP_RETURN: return simpleInstr(opcode, offset);
    case OP_CLASS: return constantInstr(opcode, chunk, offset);
    case OP_INHERIT: return simpleInstr(opcode, offset);
    case OP_METHOD: return constantInstr(opcode, chunk, offset);
    case OP_CONSTANT_NEGATIVE_ONE: return simpleInstr(opcode, offset);
    case OP_CONSTANT_ZERO: return simpleInstr(opcode, offset);
    case OP_CONSTANT_ONE: return simpleInstr(opcode, offset);
    case OP_CONSTANT_TWO: return simpleInstr(opcode, offset);
    case OP_CONSTANT_THREE: return simpleInstr(opcode, offset);
    case OP_CONSTANT_FOUR: return simpleInstr(opcode, offset);
    case OP_CONSTANT_FIVE: return simpleInstr(opcode, offset);
    case OP_ADD_ONE: return simpleInstr(opcode, offset);
    case OP_SUBTRACT_ONE: return simpleInstr(opcode, offset);
    case OP_MULTIPLY_TWO: return simpleInstr(opcode, offset);
    case OP_EQUAL_ZERO: return simpleInstr(opcode, offset);
    case OP_NOT_EQUAL: return simpleInstr(opcode, offset);
    case OP_GREATER_EQUAL: return simpleInstr(opcode, offset);
    case OP_LESS_EQUAL: return simpleInstr(opcode, offset);
    case OP_GET_THIS: return simpleInstr(opcode, offset);
    case OP_DUP: return simpleInstr(opcode, offset);
  }
  printf("Unknown opcode %d\n", opcode);
  return offset + 1;
}

void printTable(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];

    if (IS_EMPTY(entry->key)) {
      printf("%d: ", i);
      printValue(entry->key);
      printf("\n");
    } else {
      printf("%d: ", i);
      printValue(entry->key);
      printf(" -> ");
      printValue(entry->value);
      printf("\n");
    }
  }
}
