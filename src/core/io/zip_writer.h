#pragma once

#include "core/io/error.h"

#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace archon {

class ZipWriter {
public:
    explicit ZipWriter(const std::filesystem::path &file);
    ZipWriter(const ZipWriter &) = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;
    ~ZipWriter();

    Result<void> add(std::string name, std::vector<std::byte> bytes);
    Result<void> addDirectory(std::string name);
    Result<void> addFile(std::string name, const std::filesystem::path &file);
    Result<void> finish();

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
