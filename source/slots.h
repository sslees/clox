#ifndef CLOX_SLOTS_H
#define CLOX_SLOTS_H

#include "chunk.h"

typedef struct {
  int delta;
  int peak;
} SlotUsage;

SlotUsage getUsage(OpCode op);

#endif
