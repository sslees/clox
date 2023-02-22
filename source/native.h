#ifndef CLOX_NATIVE_H
#define CLOX_NATIVE_H

#include "value.h"

Value clockNative(int argc, Value* argv);
Value strNative(int argc, Value* argv);

#endif
