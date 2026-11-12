#pragma once

#include "core/meta/name_table.h"

#include <cstddef>
#include <vector>

namespace archon::test {

std::vector<std::byte> samplePso();
std::vector<std::byte> sampleRbf();
NameTable sampleNames();

}
