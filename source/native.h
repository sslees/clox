#ifndef CLOX_NATIVE_H
#define CLOX_NATIVE_H

#include "value.h"

bool clockNative(int argc, Value* argv);
bool strNative(int argc, Value* argv);

#endif
