#include "vm.h"

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "native.h"
#include "object.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VM vm;

static void resetStack() {
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ", getLine(&function->chunk, instruction));
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}

char* getGlobalName(uint16_t index) {
  for (int i = 0; i < vm.globalNames.capacity; i++) {
    Entry* entry = &vm.globalNames.entries[i];
    if ((uint16_t)AS_NUMBER(entry->value) == index)
      return AS_STRING(entry->key)->chars;
  }
  return NULL;
}

static void defineNative(const char* name, NativeFn function) {
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  uint16_t index = (uint16_t)vm.globalValues.count;
  writeValueArray(&vm.globalValues, vm.stack[1]);
  tableSet(&vm.globalNames, vm.stack[0], NUMBER_VAL((double)index));
  pop();
  pop();
}

static void slotsNeeded(int slots) {
  if (vm.stackCapacity < (vm.stackTop - vm.stack) + slots) {
    int oldCapacity = vm.stackCapacity;
    while (vm.stackCapacity < (vm.stackTop - vm.stack) + slots)
      vm.stackCapacity = GROW_CAPACITY(vm.stackCapacity);
    Value* oldStack = vm.stack;
    vm.stack = GROW_ARRAY(Value, vm.stack, oldCapacity, vm.stackCapacity);
    if (vm.stack != oldStack) {
      vm.stackTop = vm.stack + (vm.stackTop - oldStack);
      for (int i = 0; i < vm.frameCount; i++) {
        CallFrame* frame = &vm.frames[i];
        frame->slots = vm.stack + (frame->slots - oldStack);
      }
      for (ObjUpvalue* uv = vm.openUpvalues; uv != NULL; uv = uv->next)
        uv->location = vm.stack + (uv->location - oldStack);
    }
  }
}

void initVM() {
  vm.stack = NULL;
  vm.stackCapacity = 0;
  resetStack();
  slotsNeeded(2);
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  vm.nextGC = 1024 * 1024;

  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;

  initTable(&vm.globalNames);
  initValueArray(&vm.globalValues);
  initTable(&vm.strings);

  vm.initString = NULL;
  vm.initString = copyString("init", 4);

  defineNative("clock", clockNative);
  defineNative("str", strNative);
}

void freeVM() {
  FREE_ARRAY(Value*, vm.stack, vm.stackCapacity);
  freeTable(&vm.globalNames);
  freeValueArray(&vm.globalValues);
  freeTable(&vm.strings);
  vm.initString = NULL;
  freeObjects();
}

void push(Value value) {
#ifdef DEBUG_CHECK_STACK
  if (vm.stackTop - vm.stack == vm.stackCapacity) {
    runtimeError("Stack overflow.");
    exit(70);
  }
#endif
  *vm.stackTop++ = value;
}

static inline void put(Value value) {
  vm.stackTop[-1] = value;
}

Value pop() {
  return *--vm.stackTop;
}

static inline Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}

static inline Value peek0() {
  return vm.stackTop[-1];
}

static inline Value peek1() {
  return vm.stackTop[-2];
}

static bool call(ObjClosure* closure, int argCount) {
  if (argCount != closure->function->arity) {
    runtimeError(
        "Expected %d arguments but got %d.", closure->function->arity,
        argCount);
    return false;
  }

  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }
  slotsNeeded(closure->function->chunk.slots);

  CallFrame* frame = &vm.frames[vm.frameCount++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        vm.stackTop[-argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
      }
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
        Value initializer;
        if (tableGet(&klass->methods, OBJ_VAL(vm.initString), &initializer)) {
          return call(AS_CLOSURE(initializer), argCount);
        } else if (argCount != 0) {
          runtimeError("Expected 0 arguments but got %d.", argCount);
          return false;
        }
        return true;
      }
      case OBJ_CLOSURE: return call(AS_CLOSURE(callee), argCount);
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        bool success = native(argCount, vm.stackTop - argCount);
        vm.stackTop -= argCount;
        if (!success) runtimeError(AS_CSTRING(peek0()));
        return success;
      }
      default: break;
    }
  }
  runtimeError("Can only call functions and classes.");
  return false;
}

static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
  Value method;
  if (!tableGet(&klass->methods, OBJ_VAL(name), &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }
  return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
  Value receiver = peek(argCount);

  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(receiver);

  Value value;
  if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
  Value method;
  if (!tableGet(&klass->methods, OBJ_VAL(name), &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  put(OBJ_VAL(newBoundMethod(peek0(), AS_CLOSURE(method))));
  return true;
}

static ObjUpvalue* captureUpvalue(Value* local) {
  ObjUpvalue* prevUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  while (upvalue != NULL && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  if (upvalue != NULL && upvalue->location == local) return upvalue;

  ObjUpvalue* createdUpvalue = newUpvalue(local);
  createdUpvalue->next = upvalue;

  if (prevUpvalue == NULL) {
    vm.openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last) {
    ObjUpvalue* upvalue = vm.openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm.openUpvalues = upvalue->next;
  }
}

static void defineMethod(ObjString* name) {
  Value method = peek0();
  ObjClass* klass = AS_CLASS(peek1());
  tableSet(&klass->methods, OBJ_VAL(name), method);
  pop();
}

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate() {
  ObjString* b = AS_STRING(peek0());
  ObjString* a = AS_STRING(peek1());

  int length = a->length + b->length;
  ObjString* result = allocateString(length);
  memcpy(result->chars, a->chars, a->length);
  memcpy(result->chars + a->length, b->chars, b->length);
  result->chars[length] = '\0';
  result->hash = hashString(result->chars, length);

  push(OBJ_VAL(result));
  tableSet(&vm.strings, OBJ_VAL(result), NIL_VAL);
  pop();

  pop();
  put(OBJ_VAL(result));
}

static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  register uint8_t* ip = frame->ip;

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)(ip[-2] | ip[-1] << 8))
#define READ_CONSTANT() \
  (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
  do { \
    if (!IS_NUMBER(peek0()) || !IS_NUMBER(peek1())) { \
      frame->ip = ip; \
      runtimeError("Operands must be numbers."); \
      return INTERPRET_RUNTIME_ERROR; \
    } \
    double b = AS_NUMBER(pop()); \
    put(valueType(AS_NUMBER(peek0()) op b)); \
  } while (false)

  while (true) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstr(
        &frame->closure->function->chunk,
        (int)(ip - frame->closure->function->chunk.code));
#endif

    OpCode instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: push(READ_CONSTANT()); break;
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_POP: pop(); break;
      case OP_GET_LOCAL: push(frame->slots[READ_BYTE()]); break;
      case OP_SET_LOCAL: frame->slots[READ_BYTE()] = peek0(); break;
      case OP_GET_GLOBAL: {
        uint16_t index = READ_SHORT();
        Value value = vm.globalValues.values[index];
        if (IS_UNDEFINED(value)) {
          frame->ip = ip;
          runtimeError("Undefined variable '%s'.", getGlobalName(index));
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_DEFINE_GLOBAL:
        vm.globalValues.values[READ_SHORT()] = pop();
        break;
      case OP_SET_GLOBAL: {
        uint16_t index = READ_SHORT();
        if (IS_UNDEFINED(vm.globalValues.values[index])) {
          frame->ip = ip;
          runtimeError("Undefined variable '%s'.", getGlobalName(index));
          return INTERPRET_RUNTIME_ERROR;
        }
        vm.globalValues.values[index] = peek(0);
        break;
      }
      case OP_GET_UPVALUE:
        push(*frame->closure->upvalues[READ_BYTE()]->location);
        break;
      case OP_SET_UPVALUE: {
        *frame->closure->upvalues[READ_BYTE()]->location = peek0();
        break;
      }
      case OP_GET_PROPERTY: {
        if (!IS_INSTANCE(peek0())) {
          frame->ip = ip;
          runtimeError("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjInstance* instance = AS_INSTANCE(peek0());
        ObjString* name = READ_STRING();
        Value value;

        if (tableGet(&instance->fields, OBJ_VAL(name), &value)) {
          put(value);
          break;
        }
        frame->ip = ip;
        if (!bindMethod(instance->klass, name)) return INTERPRET_RUNTIME_ERROR;
        break;
      }
      case OP_SET_PROPERTY:
        if (!IS_INSTANCE(peek1())) {
          frame->ip = ip;
          runtimeError("Only instances have fields.");
          return INTERPRET_RUNTIME_ERROR;
        }

        tableSet(&AS_INSTANCE(peek1())->fields, READ_CONSTANT(), peek0());
        put(pop());
        break;
      case OP_GET_SUPER:
        frame->ip = ip;
        if (!bindMethod(AS_CLASS(pop()), READ_STRING()))
          return INTERPRET_RUNTIME_ERROR;
        break;
      case OP_EQUAL: {
        Value b = pop();
        put(BOOL_VAL(valuesEqual(peek0(), b)));
        break;
      }
      case OP_GREATER: BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS: BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        Value b = peek0();
        Value a = peek1();

        if (IS_NUMBER(a) && IS_NUMBER(b)) {
          pop();
          put(NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b)));
        } else if (IS_STRING(a) && IS_STRING(b)) {
          concatenate();
        } else if (IS_STRING(a)) {
          push(b);
          strNative(1, vm.stackTop - 1);
          pop();
          concatenate();
        } else if (IS_STRING(b)) {
          put(a);
          strNative(1, vm.stackTop - 1);
          put(b);
          concatenate();
        } else {
          frame->ip = ip;
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE: BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT: put(BOOL_VAL(isFalsey(peek0()))); break;
      case OP_NEGATE:
        if (!IS_NUMBER(peek0())) {
          frame->ip = ip;
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        put(NUMBER_VAL(-AS_NUMBER(peek0())));
        break;
      case OP_PRINT:
        printValue(pop());
        printf("\n");
        break;
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek0())) ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        ip -= offset;
        break;
      }
      case OP_CALL: {
        int argCount = READ_BYTE();
        frame->ip = ip;
        if (!callValue(peek(argCount), argCount))
          return INTERPRET_RUNTIME_ERROR;
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }
      case OP_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        frame->ip = ip;
        if (!invoke(method, argCount)) return INTERPRET_RUNTIME_ERROR;
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }
      case OP_SUPER_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        ObjClass* superclass = AS_CLASS(pop());
        frame->ip = ip;
        if (!invokeFromClass(superclass, method, argCount))
          return INTERPRET_RUNTIME_ERROR;
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }
      case OP_CLOSURE: {
        ObjClosure* closure = newClosure(AS_FUNCTION(READ_CONSTANT()));
        push(OBJ_VAL(closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          if (isLocal)
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          else
            closure->upvalues[i] = frame->closure->upvalues[index];
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(vm.stackTop - 1);
        pop();
        break;
      case OP_RETURN: {
        Value result = pop();
        closeUpvalues(frame->slots);
        vm.frameCount--;
        if (vm.frameCount == 0) {
          pop();
          return INTERPRET_OK;
        }

        vm.stackTop = frame->slots;
        push(result);
        frame = &vm.frames[vm.frameCount - 1];
        ip = frame->ip;
        break;
      }
      case OP_CLASS: push(OBJ_VAL(newClass(READ_STRING()))); break;
      case OP_INHERIT: {
        Value superclass = peek1();
        if (!IS_CLASS(superclass)) {
          frame->ip = ip;
          runtimeError("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjClass* subclass = AS_CLASS(peek0());
        tableAddAll(&AS_CLASS(superclass)->methods, &subclass->methods);
        pop();
        break;
      }
      case OP_METHOD: defineMethod(READ_STRING()); break;
      case OP_CONSTANT_NEGATIVE_ONE: push(NUMBER_VAL(-1)); break;
      case OP_CONSTANT_ZERO: push(NUMBER_VAL(0)); break;
      case OP_CONSTANT_ONE: push(NUMBER_VAL(1)); break;
      case OP_CONSTANT_TWO: push(NUMBER_VAL(2)); break;
      case OP_CONSTANT_THREE: push(NUMBER_VAL(3)); break;
      case OP_CONSTANT_FOUR: push(NUMBER_VAL(4)); break;
      case OP_CONSTANT_FIVE: push(NUMBER_VAL(5)); break;
      case OP_ADD_ONE: {
        Value value = peek0();
        if (IS_NUMBER(value)) {
          put(NUMBER_VAL(AS_NUMBER(value) + 1));
        } else if (IS_STRING(value)) {
          push(OBJ_VAL(copyString("1", 1)));
          concatenate();
        } else {
          frame->ip = ip;
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUBTRACT_ONE:
        if (!IS_NUMBER(peek0())) {
          frame->ip = ip;
          runtimeError("Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        put(NUMBER_VAL(AS_NUMBER(peek0()) - 1));
        break;
      case OP_MULTIPLY_TWO:
        if (!IS_NUMBER(peek0())) {
          frame->ip = ip;
          runtimeError("Operands must be numbers.");
          return INTERPRET_RUNTIME_ERROR;
        }
        put(NUMBER_VAL(AS_NUMBER(peek0()) * 2));
        break;
      case OP_EQUAL_ZERO: {
        Value a = peek0();
        put(BOOL_VAL(IS_NUMBER(a) && AS_NUMBER(a) == 0));
        break;
      }
      case OP_NOT_EQUAL: {
        Value b = pop();
        put(BOOL_VAL(!valuesEqual(peek0(), b)));
        break;
      }
      case OP_GREATER_EQUAL: BINARY_OP(BOOL_VAL, >=); break;
      case OP_LESS_EQUAL: BINARY_OP(BOOL_VAL, <=); break;
      case OP_GET_THIS: push(*frame->slots); break;
      case OP_DUP: push(peek0()); break;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

InterpretResult interpret(char* source, bool file) {
  ObjFunction* function = compile(source);

  if (file) free(source);
  if (function == NULL) return INTERPRET_COMPILE_ERROR;

  push(OBJ_VAL(function));
  ObjClosure* closure = newClosure(function);
  pop();
  push(OBJ_VAL(closure));
  call(closure, 0);

  return run();
}
