#include "core/io/zip_writer.h"

#include <zip.h>

namespace archon {

namespace {

constexpr std::uint64_t fileLimit = (2ull << 30) - 1;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "zip", 0, std::move(message) });
}

}

struct ZipWriter::State {
    zip_t *archive = nullptr;
    std::string opened;
    std::deque<std::vector<std::byte>> kept;

    ~State()
    {
        if (archive != nullptr)
            zip_discard(archive);
    }
};

ZipWriter::ZipWriter(const std::filesystem::path &file)
    : state_(std::make_unique<State>())
{
    int error = 0;
    state_->archive = zip_open(file.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (state_->archive == nullptr) {
        zip_error_t details;
        zip_error_init_with_code(&details, error);
        state_->opened = zip_error_strerror(&details);
        zip_error_fini(&details);
    }
}

ZipWriter::~ZipWriter() = default;

Result<void> ZipWriter::add(std::string name, std::vector<std::byte> bytes)
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file: " + state_->opened);
    const std::vector<std::byte> &kept = state_->kept.emplace_back(std::move(bytes));
    zip_source_t *data = zip_source_buffer(state_->archive, kept.data(), kept.size(), 0);
    if (data == nullptr || zip_file_add(state_->archive, name.c_str(), data, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        if (data != nullptr)
            zip_source_free(data);
        return failure("cannot add " + name + " to the zip file");
    }
    return {};
}

Result<void> ZipWriter::addDirectory(std::string name)
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file: " + state_->opened);
    if (zip_dir_add(state_->archive, name.c_str(), ZIP_FL_ENC_UTF_8) < 0)
        return failure("cannot add " + name + " to the zip file");
    return {};
}

Result<void> ZipWriter::addFile(std::string name, const std::filesystem::path &file)
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file: " + state_->opened);
    std::error_code error;
    const bool regular = std::filesystem::is_regular_file(file, error);
    const std::uintmax_t size = regular ? std::filesystem::file_size(file, error) : 0;
    if (!regular || error)
        return failure("cannot read " + file.string());
    if (size > fileLimit)
        return failure(file.string() + " is larger than 2 GiB");
    zip_source_t *data = zip_source_file(state_->archive, file.c_str(), 0, ZIP_LENGTH_TO_END);
    if (data == nullptr || zip_file_add(state_->archive, name.c_str(), data, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        if (data != nullptr)
            zip_source_free(data);
        return failure("cannot add " + file.string() + " to the zip file");
    }
    return {};
}

Result<void> ZipWriter::finish()
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file: " + state_->opened);
    if (zip_close(state_->archive) != 0)
        return failure(std::string("cannot write the zip file: ") + zip_strerror(state_->archive));
    state_->archive = nullptr;
    return {};
}

}
