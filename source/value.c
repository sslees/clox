#include "value.h"

#include "memory.h"
#include "object.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void initValueArray(ValueArray* array) {
  array->values = NULL;
  array->capacity = 0;
  array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {
  if (array->capacity < array->count + 1) {
    int oldCapacity = array->capacity;
    array->capacity = GROW_CAPACITY(oldCapacity);
    array->values =
        GROW_ARRAY(Value, array->values, oldCapacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray* array) {
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);
}

char* valToStr(Value value) {
  char* buff;
  int bytes = -1;

#ifdef NAN_BOXING
  if (IS_BOOL(value)) {
    bytes = asprintf(&buff, AS_BOOL(value) ? "true" : "false");
  } else if (IS_NIL(value)) {
    bytes = asprintf(&buff, "nil");
  } else if (IS_NUMBER(value)) {
    bytes = asprintf(&buff, "%g", AS_NUMBER(value));
  } else if (IS_OBJ(value)) {
    bytes = objToStr(&buff, value);
  }
#else
  switch (value.type) {
    case VAL_BOOL:
      bytes = asprintf(&buff, AS_BOOL(value) ? "true" : "false");
      break;
    case VAL_NIL: bytes = asprintf(&buff, "nil"); break;
    case VAL_NUMBER: bytes = asprintf(&buff, "%g", AS_NUMBER(value)); break;
    case VAL_OBJ: bytes = objToStr(&buff, value); break;
  }
#endif
  if (bytes == -1) return NULL;
  return buff;
}

void printValue(Value value) {
  char* str = valToStr(value);

  if (str != NULL) {
    printf("%s", str);
    free(str);
  }
}

bool valuesEqual(Value a, Value b) {
#ifdef NAN_BOXING
  if (IS_NUMBER(a) && IS_NUMBER(b)) return AS_NUMBER(a) == AS_NUMBER(b);
  return a == b;
#else
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL: return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL: return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: return AS_OBJ(a) == AS_OBJ(b);
    default: return false;
  }
#endif
}
