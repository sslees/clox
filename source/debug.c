#include "debug.h"

#include "object.h"
#include "value.h"

#include <stdio.h>

char* getOpName(OpCode op) {
  switch (op) {
    case OP_CONSTANT: return "OP_CONSTANT"; break;
    case OP_NIL: return "OP_NIL"; break;
    case OP_TRUE: return "OP_TRUE"; break;
    case OP_FALSE: return "OP_FALSE"; break;
    case OP_POP: return "OP_POP"; break;
    case OP_GET_LOCAL: return "OP_GET_LOCAL"; break;
    case OP_SET_LOCAL: return "OP_SET_LOCAL"; break;
    case OP_GET_GLOBAL: return "OP_GET_GLOBAL"; break;
    case OP_DEFINE_GLOBAL: return "OP_DEFINE_GLOBAL"; break;
    case OP_SET_GLOBAL: return "OP_SET_GLOBAL"; break;
    case OP_GET_UPVALUE: return "OP_GET_UPVALUE"; break;
    case OP_SET_UPVALUE: return "OP_SET_UPVALUE"; break;
    case OP_GET_PROPERTY: return "OP_GET_PROPERTY"; break;
    case OP_SET_PROPERTY: return "OP_SET_PROPERTY"; break;
    case OP_GET_SUPER: return "OP_GET_SUPER"; break;
    case OP_EQUAL: return "OP_EQUAL"; break;
    case OP_GREATER: return "OP_GREATER"; break;
    case OP_LESS: return "OP_LESS"; break;
    case OP_ADD: return "OP_ADD"; break;
    case OP_SUBTRACT: return "OP_SUBTRACT"; break;
    case OP_MULTIPLY: return "OP_MULTIPLY"; break;
    case OP_DIVIDE: return "OP_DIVIDE"; break;
    case OP_NOT: return "OP_NOT"; break;
    case OP_NEGATE: return "OP_NEGATE"; break;
    case OP_PRINT: return "OP_PRINT"; break;
    case OP_JUMP: return "OP_JUMP"; break;
    case OP_JUMP_IF_FALSE: return "OP_JUMP_IF_FALSE"; break;
    case OP_LOOP: return "OP_LOOP"; break;
    case OP_CALL: return "OP_CALL"; break;
    case OP_INVOKE: return "OP_INVOKE"; break;
    case OP_SUPER_INVOKE: return "OP_SUPER_INVOKE"; break;
    case OP_CLOSURE: return "OP_CLOSURE"; break;
    case OP_CLOSE_UPVALUE: return "OP_CLOSE_UPVALUE"; break;
    case OP_RETURN: return "OP_RETURN"; break;
    case OP_CLASS: return "OP_CLASS"; break;
    case OP_INHERIT: return "OP_INHERIT"; break;
    case OP_METHOD: return "OP_METHOD"; break;
    case OP_CONSTANT_NEGATIVE_ONE: return "OP_CONSTANT_NEGATIVE_ONE"; break;
    case OP_CONSTANT_ZERO: return "OP_CONSTANT_ZERO"; break;
    case OP_CONSTANT_ONE: return "OP_CONSTANT_ONE"; break;
    case OP_CONSTANT_TWO: return "OP_CONSTANT_TWO"; break;
    case OP_CONSTANT_THREE: return "OP_CONSTANT_THREE"; break;
    case OP_CONSTANT_FOUR: return "OP_CONSTANT_FOUR"; break;
    case OP_CONSTANT_FIVE: return "OP_CONSTANT_FIVE"; break;
    case OP_ADD_ONE: return "OP_ADD_ONE"; break;
    case OP_SUBTRACT_ONE: return "OP_SUBTRACT_ONE"; break;
    case OP_MULTIPLY_TWO: return "OP_MULTIPLY_TWO"; break;
    case OP_EQUAL_ZERO: return "OP_EQUAL_ZERO"; break;
    case OP_NOT_EQUAL: return "OP_NOT_EQUAL"; break;
    case OP_GREATER_EQUAL: return "OP_GREATER_EQUAL"; break;
    case OP_LESS_EQUAL: return "OP_LESS_EQUAL"; break;
    case OP_GET_THIS: return "OP_GET_THIS"; break;
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
    case OP_GET_GLOBAL: return constantInstr(opcode, chunk, offset);
    case OP_DEFINE_GLOBAL: return constantInstr(opcode, chunk, offset);
    case OP_SET_GLOBAL: return constantInstr(opcode, chunk, offset);
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
  }
  printf("Unknown opcode %d\n", opcode);
  return offset + 1;
}
