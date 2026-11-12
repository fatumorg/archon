#include "support/zip_builder.h"

#include <zip.h>

namespace archon::test {

std::vector<std::byte> makeZip(const std::vector<std::pair<std::string, std::string>> &members)
{
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t *buffer = zip_source_buffer_create(nullptr, 0, 0, &error);
    zip_t *archive = zip_open_from_source(buffer, ZIP_TRUNCATE, &error);
    zip_source_keep(buffer);
    for (const auto &[name, content] : members) {
        zip_source_t *data = zip_source_buffer(archive, content.data(), content.size(), 0);
        zip_file_add(archive, name.c_str(), data, ZIP_FL_ENC_UTF_8);
    }
    zip_close(archive);
    zip_source_open(buffer);
    zip_source_seek(buffer, 0, SEEK_END);
    const auto size = static_cast<std::size_t>(zip_source_tell(buffer));
    zip_source_seek(buffer, 0, SEEK_SET);
    std::vector<std::byte> bytes(size);
    zip_source_read(buffer, bytes.data(), size);
    zip_source_close(buffer);
    zip_source_free(buffer);
    return bytes;
}

}
