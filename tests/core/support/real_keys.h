#pragma once

#include "core/crypto/keys.h"

namespace archon::test {

const Keys *realKeys();
std::filesystem::path scratchDirectory();

}
