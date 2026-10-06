#pragma once

#include <cstddef>

#include "shared_payload.h"

bool readingBufferPush(const Payload &p);

bool readingBufferPeek(Payload &out);

void readingBufferPop();

size_t readingBufferCount();
