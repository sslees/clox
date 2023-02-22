#include "slots.h"

SlotUsage getUsage(OpCode op) {
  switch (op) {
    case OP_CONSTANT: return (SlotUsage){1, 1};
    case OP_NIL: return (SlotUsage){1, 1};
    case OP_TRUE: return (SlotUsage){1, 1};
    case OP_FALSE: return (SlotUsage){1, 1};
    case OP_POP: return (SlotUsage){-1, 0};
    case OP_GET_LOCAL: return (SlotUsage){1, 1};
    case OP_SET_LOCAL: return (SlotUsage){0, 0};
    case OP_GET_GLOBAL: return (SlotUsage){1, 1};
    case OP_DEFINE_GLOBAL: return (SlotUsage){-1, 0};
    case OP_SET_GLOBAL: return (SlotUsage){0, 0};
    case OP_GET_UPVALUE: return (SlotUsage){1, 1};
    case OP_SET_UPVALUE: return (SlotUsage){0, 0};
    case OP_GET_PROPERTY: return (SlotUsage){0, 0};
    case OP_SET_PROPERTY: return (SlotUsage){-1, 0};
    case OP_GET_SUPER: return (SlotUsage){-1, 0};
    case OP_EQUAL: return (SlotUsage){-1, 0};
    case OP_GREATER: return (SlotUsage){-1, 0};
    case OP_LESS: return (SlotUsage){-1, 0};
    case OP_ADD: return (SlotUsage){-1, 1};
    case OP_SUBTRACT: return (SlotUsage){-1, 0};
    case OP_MULTIPLY: return (SlotUsage){-1, 0};
    case OP_DIVIDE: return (SlotUsage){-1, 0};
    case OP_NOT: return (SlotUsage){0, 0};
    case OP_NEGATE: return (SlotUsage){0, 0};
    case OP_PRINT: return (SlotUsage){-1, 0};
    case OP_JUMP: return (SlotUsage){0, 0};
    case OP_JUMP_IF_FALSE: return (SlotUsage){0, 0};
    case OP_LOOP: return (SlotUsage){0, 0};
    case OP_CALL: return (SlotUsage){0, 0};
    case OP_INVOKE: return (SlotUsage){0, 0};
    case OP_SUPER_INVOKE: return (SlotUsage){-1, 0};
    case OP_CLOSURE: return (SlotUsage){1, 1};
    case OP_CLOSE_UPVALUE: return (SlotUsage){-1, 0};
    case OP_RETURN: return (SlotUsage){-1, 0};
    case OP_CLASS: return (SlotUsage){1, 1};
    case OP_INHERIT: return (SlotUsage){-1, 0};
    case OP_METHOD: return (SlotUsage){-1, 0};
    case OP_CONSTANT_NEGATIVE_ONE: return (SlotUsage){1, 1};
    case OP_CONSTANT_ZERO: return (SlotUsage){1, 1};
    case OP_CONSTANT_ONE: return (SlotUsage){1, 1};
    case OP_CONSTANT_TWO: return (SlotUsage){1, 1};
    case OP_CONSTANT_THREE: return (SlotUsage){1, 1};
    case OP_CONSTANT_FOUR: return (SlotUsage){1, 1};
    case OP_CONSTANT_FIVE: return (SlotUsage){1, 1};
    case OP_ADD_ONE: return (SlotUsage){0, 0};
    case OP_SUBTRACT_ONE: return (SlotUsage){0, 0};
    case OP_MULTIPLY_TWO: return (SlotUsage){0, 0};
    case OP_EQUAL_ZERO: return (SlotUsage){0, 0};
    case OP_NOT_EQUAL: return (SlotUsage){-1, 0};
    case OP_GREATER_EQUAL: return (SlotUsage){-1, 0};
    case OP_LESS_EQUAL: return (SlotUsage){-1, 0};
    case OP_GET_THIS: return (SlotUsage){1, 1};
  }
  return (SlotUsage){0, 0};
}
