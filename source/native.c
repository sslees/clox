#include "native.h"

#include "memory.h"
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

static Value getFieldError(const char* instance, const char* name) {
  char* buff;
  if (asprintf(&buff, "%s does not have field \"%s\".", instance, name) == -1)
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
  if (str == NULL)
    NATIVE_ERROR("Could not convert argument 1 of str to a string.");
  NATIVE_RETURN(OBJ_VAL(takeString(str, strlen(str))));
}

bool hashNative(int argc, Value* argv) {
  ASSERT_ARITY(1);
  NATIVE_RETURN(NUMBER_VAL(hashValue(*argv)));
}

bool hasFieldNative(int argc, Value* argv) {
  ASSERT_ARITY(2);
  if (!IS_INSTANCE(argv[0]))
    NATIVE_ERROR("Argument 1 of hasField must be an instance.");
  if (!IS_STRING(argv[1]))
    NATIVE_ERROR("Argument 2 of hasField must be a string.");

  NATIVE_RETURN(
      BOOL_VAL(tableGet(&AS_INSTANCE(argv[0])->fields, argv[1], NULL)));
}

bool getFieldNative(int argc, Value* argv) {
  ASSERT_ARITY(2);
  if (!IS_INSTANCE(argv[0]))
    NATIVE_ERROR("Argument 1 of getField must be an instance.");
  if (!IS_STRING(argv[1]))
    NATIVE_ERROR("Argument 2 of getField must be a string.");

  Value value;

  if (!tableGet(&AS_INSTANCE(argv[0])->fields, argv[1], &value)) {
    char* strInstance = valToStr(argv[0]);

    if (strInstance == NULL)
      NATIVE_ERROR("Could not convert argument 1 of getField to a string.");

    char* strName = valToStr(argv[1]);

    if (strName == NULL)
      NATIVE_ERROR("Could not convert argument 2 of getField to a string.");

    argv[-1] = getFieldError(strInstance, strName);

    FREE_ARRAY(char, strInstance, strlen(strInstance) + 1);
    FREE_ARRAY(char, strName, strlen(strName) + 1);

    return false;
  }

  NATIVE_RETURN(value);
}

bool setFieldNative(int argc, Value* argv) {
  ASSERT_ARITY(3);
  if (!IS_INSTANCE(argv[0]))
    NATIVE_ERROR("Argument 1 of setField must be an instance.");
  if (!IS_STRING(argv[1]))
    NATIVE_ERROR("Argument 2 of setField must be a string.");

  NATIVE_RETURN(
      BOOL_VAL(tableSet(&AS_INSTANCE(argv[0])->fields, argv[1], argv[2])));
}

bool deleteFieldNative(int argc, Value* argv) {
  ASSERT_ARITY(2);
  if (!IS_INSTANCE(argv[0]))
    NATIVE_ERROR("Argument 1 of deleteField must be an instance.");
  if (!IS_STRING(argv[1]))
    NATIVE_ERROR("Argument 2 of deleteField must be a string.");

  NATIVE_RETURN(BOOL_VAL(tableDelete(&AS_INSTANCE(argv[0])->fields, argv[1])));
}
