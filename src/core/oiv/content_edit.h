#pragma once

#include "core/oiv/oiv_package.h"

#include <span>
#include <vector>

namespace archon {

std::vector<std::byte> applyTextEdits(std::span<const std::byte> original, std::span<const OivText> edits);
Result<std::vector<std::byte>> applyXmlEdits(std::span<const std::byte> original, std::span<const OivXml> edits);

}
