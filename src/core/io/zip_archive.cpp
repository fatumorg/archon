#include "core/io/zip_archive.h"

#include <QFile>

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
    return listed(std::move(state));
}

Result<ZipArchive> ZipArchive::open(const std::filesystem::path &file)
{
    auto state = std::make_unique<State>();
    int code = 0;
    state->archive = zip_open(file.c_str(), ZIP_RDONLY | ZIP_CHECKCONS, &code);
    if (state->archive == nullptr)
        return failure(code == ZIP_ER_NOENT || code == ZIP_ER_OPEN || code == ZIP_ER_READ ? "cannot read " + file.string() : "not a zip file");
    return listed(std::move(state));
}

ZipArchive ZipArchive::listed(std::unique_ptr<State> state)
{
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

Result<std::pair<std::uint64_t, std::uint64_t>> ZipArchive::member(const std::string &key) const
{
    const zip_int64_t index = zip_name_locate(state_->archive, key.c_str(), ZIP_FL_ENC_GUESS);
    if (index < 0)
        return failure(key + " is not in the zip file");
    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(state_->archive, static_cast<zip_uint64_t>(index), 0, &stat) != 0 || (stat.valid & ZIP_STAT_SIZE) == 0)
        return failure("cannot read " + key);
    return std::pair(static_cast<std::uint64_t>(index), static_cast<std::uint64_t>(stat.size));
}

Result<std::vector<std::byte>> ZipArchive::read(std::string_view name, std::uint64_t limit) const
{
    const std::string key(name);
    const auto found = member(key);
    if (!found)
        return std::unexpected(found.error());
    const auto [index, size] = *found;
    if (size > limit)
        return failure(key + " is too large");
    zip_file_t *file = zip_fopen_index(state_->archive, index, 0);
    if (file == nullptr)
        return failure("cannot read " + key);
    std::vector<std::byte> content(static_cast<std::size_t>(size));
    const zip_int64_t read = content.empty() ? 0 : zip_fread(file, content.data(), content.size());
    std::byte extra;
    const zip_int64_t beyond = zip_fread(file, &extra, 1);
    zip_fclose(file);
    if (read != static_cast<zip_int64_t>(content.size()) || beyond != 0)
        return failure(key + " is damaged");
    return content;
}

Result<void> ZipArchive::extract(std::string_view name, const std::filesystem::path &destination) const
{
    const std::string key(name);
    const auto found = member(key);
    if (!found)
        return std::unexpected(found.error());
    const auto [index, size] = *found;
    QFile out(destination);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return failure("cannot write " + destination.string());
    const std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(zip_fopen_index(state_->archive, index, 0), &zip_fclose);
    if (!file)
        return failure("cannot read " + key);
    std::vector<char> buffer(4u << 20);
    std::uint64_t total = 0;
    zip_int64_t read = 0;
    while ((read = zip_fread(file.get(), buffer.data(), buffer.size())) > 0) {
        if (out.write(buffer.data(), read) != read)
            return failure("cannot write " + destination.string());
        total += static_cast<std::uint64_t>(read);
    }
    if (read < 0 || total != size)
        return failure(key + " is damaged");
    if (!out.flush())
        return failure("cannot write " + destination.string());
    return {};
}

}
