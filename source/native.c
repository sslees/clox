#include "native.h"

#include "object.h"
#include "value.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define ASSERT_ARITY(expected) \
  do { \
    if (argc != (expected)) { \
      argv[-1] = arityError((expected), argc); \
      return false; \
    } \
  } while (false)
#define NATIVE_ERROR(message) \
  do { \
    argv[-1] = OBJ_VAL(copyString((message), strlen(message))); \
    return false; \
  } while (false)
#define NATIVE_RETURN(value) \
  do { \
    argv[-1] = (value); \
    return true; \
  } while (false)

static Value arityError(int expected, int actual) {
  char* buff;
  if (asprintf(&buff, "Expected %d arguments but got %d.", expected, actual) ==
      -1)
    exit(1);
  Value errorVal = OBJ_VAL(copyString(buff, strlen(buff)));
  free(buff);
  return errorVal;
}

bool clockNative(int argc, Value* argv) {
  ASSERT_ARITY(0);
  NATIVE_RETURN(NUMBER_VAL((double)clock() / CLOCKS_PER_SEC));
}

bool strNative(int argc, Value* argv) {
  ASSERT_ARITY(1);
  Value value = *argv;
  if (IS_STRING(value)) NATIVE_RETURN(value);
  char* str = valToStr(value);
  if (str == NULL) NATIVE_ERROR("Could not convert value to string.");
  NATIVE_RETURN(OBJ_VAL(takeString(str, strlen(str))));
}

bool hasFieldNative(int argc, Value* argv) {
  ASSERT_ARITY(2);
  if (!IS_INSTANCE(argv[0]))
    NATIVE_ERROR("Argument 1 of hasField must be an instance.");
  if (!IS_STRING(argv[1]))
    NATIVE_ERROR("Argument 2 of hasField must be a string.");

  ObjInstance* instance = AS_INSTANCE(argv[0]);
  Value unused;
  NATIVE_RETURN(BOOL_VAL(tableGet(&instance->fields, argv[1], &unused)));
}
