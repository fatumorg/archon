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
    zip_source_t *buffer = nullptr;
    zip_t *archive = nullptr;
    std::deque<std::vector<std::byte>> kept;

    ~State()
    {
        if (archive != nullptr)
            zip_discard(archive);
        if (buffer != nullptr)
            zip_source_free(buffer);
    }
};

ZipWriter::ZipWriter()
    : state_(std::make_unique<State>())
{
    zip_error_t error;
    zip_error_init(&error);
    state_->buffer = zip_source_buffer_create(nullptr, 0, 0, &error);
    if (state_->buffer != nullptr) {
        state_->archive = zip_open_from_source(state_->buffer, ZIP_TRUNCATE, &error);
        if (state_->archive != nullptr)
            zip_source_keep(state_->buffer);
    }
    zip_error_fini(&error);
}

ZipWriter::~ZipWriter() = default;

Result<void> ZipWriter::add(std::string name, std::vector<std::byte> bytes)
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file");
    const std::vector<std::byte> &kept = state_->kept.emplace_back(std::move(bytes));
    zip_source_t *data = zip_source_buffer(state_->archive, kept.data(), kept.size(), 0);
    if (data == nullptr || zip_file_add(state_->archive, name.c_str(), data, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        if (data != nullptr)
            zip_source_free(data);
        return failure("cannot add " + name + " to the zip file");
    }
    return {};
}

Result<void> ZipWriter::addFile(std::string name, const std::filesystem::path &file)
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file");
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
        return failure("cannot read " + file.string());
    if (std::filesystem::file_size(file, error) > fileLimit)
        return failure(file.string() + " is larger than 2 GiB");
    zip_source_t *data = zip_source_file(state_->archive, file.c_str(), 0, ZIP_LENGTH_TO_END);
    if (data == nullptr || zip_file_add(state_->archive, name.c_str(), data, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0) {
        if (data != nullptr)
            zip_source_free(data);
        return failure("cannot add " + file.string() + " to the zip file");
    }
    return {};
}

Result<std::vector<std::byte>> ZipWriter::finish()
{
    if (state_->archive == nullptr)
        return failure("cannot create the zip file");
    if (zip_close(state_->archive) != 0)
        return failure(std::string("cannot write the zip file: ") + zip_strerror(state_->archive));
    state_->archive = nullptr;
    if (zip_source_open(state_->buffer) != 0)
        return failure("cannot read the zip file back");
    zip_source_seek(state_->buffer, 0, SEEK_END);
    const zip_int64_t size = zip_source_tell(state_->buffer);
    zip_source_seek(state_->buffer, 0, SEEK_SET);
    std::vector<std::byte> bytes(static_cast<std::size_t>(std::max<zip_int64_t>(size, 0)));
    const zip_int64_t read = bytes.empty() ? 0 : zip_source_read(state_->buffer, bytes.data(), bytes.size());
    zip_source_close(state_->buffer);
    if (read != static_cast<zip_int64_t>(bytes.size()))
        return failure("cannot read the zip file back");
    return bytes;
}

}
