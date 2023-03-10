#include "native.h"

#include "object.h"
#include "value.h"

#include <time.h>

Value clockNative(int argc, Value* argv) {
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

Value strNative(int argc, Value* argv) {
  Value value = *argv;

  if (IS_STRING(value)) return value;

  char* str = valToStr(value);

  if (str == NULL) return NIL_VAL;
  return OBJ_VAL(takeString(str, strlen(str)));
}
