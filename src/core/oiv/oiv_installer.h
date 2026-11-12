#pragma once

#include "core/crypto/keys.h"
#include "core/oiv/oiv_package.h"

#include <filesystem>
#include <memory>

namespace archon {

enum class InstallTarget { Mods, Game };

Result<void> installOivPackage(const ZipArchive &zip, const OivPackage &package, std::size_t content, InstallTarget target, const std::filesystem::path &root, std::shared_ptr<const Keys> keys, const std::filesystem::path &store);

}
