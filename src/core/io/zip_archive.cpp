#include "core/io/zip_archive.h"

#include <zip.h>

namespace archon {

namespace {

constexpr char format[] = "zip";

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { format, 0, std::move(message) });
}

}

struct ZipArchive::State {
    std::vector<std::byte> bytes;
    zip_t *archive = nullptr;
    std::vector<std::string> names;

    ~State()
    {
        if (archive != nullptr)
            zip_discard(archive);
    }
};

ZipArchive::ZipArchive(std::unique_ptr<State> state)
    : state_(std::move(state))
{
}

ZipArchive::ZipArchive(ZipArchive &&) noexcept = default;
ZipArchive &ZipArchive::operator=(ZipArchive &&) noexcept = default;
ZipArchive::~ZipArchive() = default;

Result<ZipArchive> ZipArchive::open(std::vector<std::byte> bytes)
{
    if (bytes.empty())
        return failure("file is empty");
    auto state = std::make_unique<State>();
    state->bytes = std::move(bytes);
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t *source = zip_source_buffer_create(state->bytes.data(), state->bytes.size(), 0, &error);
    if (source == nullptr)
        return failure("cannot read the zip file");
    state->archive = zip_open_from_source(source, ZIP_RDONLY | ZIP_CHECKCONS, &error);
    if (state->archive == nullptr) {
        zip_source_free(source);
        zip_error_fini(&error);
        return failure("not a zip file");
    }
    const zip_int64_t count = zip_get_num_entries(state->archive, 0);
    for (zip_int64_t index = 0; index < count; ++index) {
        const char *name = zip_get_name(state->archive, static_cast<zip_uint64_t>(index), ZIP_FL_ENC_GUESS);
        state->names.emplace_back(name != nullptr ? name : "");
    }
    return ZipArchive(std::move(state));
}

const std::vector<std::string> &ZipArchive::names() const
{
    return state_->names;
}

Result<std::vector<std::byte>> ZipArchive::read(std::string_view name, std::uint64_t limit) const
{
    const std::string key(name);
    const zip_int64_t index = zip_name_locate(state_->archive, key.c_str(), ZIP_FL_ENC_GUESS);
    if (index < 0)
        return failure(key + " is not in the zip file");
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(state_->archive, static_cast<zip_uint64_t>(index), 0, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0)
        return failure("cannot read " + key);
    if (stat.size > limit)
        return failure(key + " is too large");
    zip_file_t *file = zip_fopen_index(state_->archive, static_cast<zip_uint64_t>(index), 0);
    if (file == nullptr)
        return failure("cannot read " + key);
    std::vector<std::byte> content(static_cast<std::size_t>(stat.size));
    const zip_int64_t read = content.empty() ? 0 : zip_fread(file, content.data(), content.size());
    std::byte extra;
    const zip_int64_t beyond = zip_fread(file, &extra, 1);
    zip_fclose(file);
    if (read != static_cast<zip_int64_t>(content.size()) || beyond != 0)
        return failure(key + " is damaged");
    return content;
}

}
