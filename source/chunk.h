#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include "common.h"
#include "value.h"

typedef enum {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_SET_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_SUPER,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
  OP_NEGATE,
  OP_PRINT,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,
  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  OP_CLASS,
  OP_INHERIT,
  OP_METHOD,
  OP_CONSTANT_NEGATIVE_ONE,
  OP_CONSTANT_ZERO,
  OP_CONSTANT_ONE,
  OP_CONSTANT_TWO,
  OP_CONSTANT_THREE,
  OP_CONSTANT_FOUR,
  OP_CONSTANT_FIVE,
  OP_ADD_ONE,
  OP_SUBTRACT_ONE,
  OP_MULTIPLY_TWO,
  OP_EQUAL_ZERO,
  OP_NOT_EQUAL,
  OP_GREATER_EQUAL,
  OP_LESS_EQUAL,
  OP_GET_THIS
} OpCode;

typedef struct {
  int offset;
  int line;
} LineStart;

typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int lineCount;
  int lineCapacity;
  LineStart* lines;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
void amendChunk(Chunk* chunk, int bytes);
int addConstant(Chunk* chunk, Value value);
int getLine(Chunk* chunk, int instruction);

#endif
