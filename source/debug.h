#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H

#include "chunk.h"
#include "scanner.h"
#include "table.h"

char* getTokenName(TokenType token);
char* getOpName(OpCode op);
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstr(Chunk* chunk, int offset);
void printTable(Table* table);

#endif
