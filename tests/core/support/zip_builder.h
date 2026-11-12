#pragma once

#include <string>
#include <utility>
#include <vector>

namespace archon::test {

std::vector<std::byte> makeZip(const std::vector<std::pair<std::string, std::string>> &members);

}
