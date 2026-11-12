#include "core/meta/metadata_document.h"

#include "core/crypto/inflate.h"
#include "core/io/ascii.h"
#include "core/meta/meta_xml.h"
#include "core/meta/pso_file.h"
#include "core/meta/rbf_file.h"
#include "core/meta/rsc_meta_file.h"
#include "core/rsc/resource_file.h"
#include "core/rsc/rsc7.h"

#include <algorithm>
#include <array>

namespace archon {

namespace {

constexpr std::size_t rsc7HeaderSize = 16;

std::unexpected<Error> failure(std::string message)
{
    return std::unexpected(Error { "meta", 0, std::move(message) });
}

bool isRscMeta(std::span<const std::byte> file)
{
    if (file.size() < rsc7HeaderSize)
        return false;
    const auto header = parseRsc7Header(file.first(rsc7HeaderSize));
    return header && header->version == rscMetaVersion;
}

Result<RscPages> resourcePages(std::span<const std::byte> file)
{
    const auto header = parseRsc7Header(file.first(rsc7HeaderSize));
    if (!header)
        return std::unexpected(header.error());
    auto pages = inflateResource(file);
    if (!pages)
        return std::unexpected(pages.error());
    return RscPages { header->systemFlags, header->graphicsFlags, std::move(*pages) };
}

Result<MetadataFormat> formatOf(std::span<const std::byte> file)
{
    if (isPsoFile(file))
        return MetadataFormat::Pso;
    if (isRbfFile(file))
        return MetadataFormat::Rbf;
    if (isRscMeta(file))
        return MetadataFormat::RscMeta;
    return failure("the file is not PSO, RBF or resource metadata");
}

bool sameContent(MetadataFormat format, std::span<const std::byte> original, std::span<const std::byte> saved)
{
    if (format != MetadataFormat::RscMeta)
        return std::ranges::equal(original, saved);
    const auto before = resourcePages(original);
    const auto after = resourcePages(saved);
    return before && after && before->systemFlags == after->systemFlags && before->graphicsFlags == after->graphicsFlags && before->pages == after->pages;
}

}

bool isBinaryMetadataName(std::string_view name)
{
    const std::string lower = asciiLower(name);
    return std::ranges::any_of(std::array<std::string_view, 6> { ".ymt", ".ymf", ".ymap", ".ytyp", ".pso", ".rbf" }, [&lower](std::string_view extension) { return lower.ends_with(extension); });
}

bool isBinaryMetadata(std::span<const std::byte> file)
{
    return formatOf(file).has_value();
}

Result<MetadataDocument> openMetadata(std::span<const std::byte> file, const NameTable &names)
{
    const auto format = formatOf(file);
    if (!format)
        return std::unexpected(format.error());
    Result<std::string> xml = failure("the file could not be read");
    switch (*format) {
    case MetadataFormat::Pso: {
        const auto data = readPsoFile(file);
        xml = data ? metaToXml(*data, names) : Result<std::string>(std::unexpected(data.error()));
        break;
    }
    case MetadataFormat::Rbf:
        xml = rbfToXml(file);
        break;
    case MetadataFormat::RscMeta: {
        const auto pages = resourcePages(file);
        const auto data = pages ? readRscMeta(*pages) : Result<MetaData>(std::unexpected(pages.error()));
        xml = data ? metaToXml(*data, names) : Result<std::string>(std::unexpected(data.error()));
        break;
    }
    }
    if (!xml)
        return std::unexpected(xml.error());
    MetadataDocument document { *format, std::move(*xml), {} };
    const auto saved = saveMetadata(file, document.xml, names);
    if (!saved)
        document.readOnlyReason = "This file cannot be saved byte-exact yet (" + saved.error().message + ").";
    else if (!sameContent(*format, file, *saved))
        document.readOnlyReason = "This file cannot be saved byte-exact yet (its XML does not reproduce every byte of the original).";
    return document;
}

Result<std::vector<std::byte>> saveMetadata(std::span<const std::byte> original, std::string_view xml, const NameTable &names)
{
    const auto format = formatOf(original);
    if (!format)
        return std::unexpected(format.error());
    switch (*format) {
    case MetadataFormat::Pso: {
        const auto data = readPsoFile(original);
        if (!data)
            return std::unexpected(data.error());
        const auto edited = metaFromXml(*data, xml, names);
        if (!edited)
            return std::unexpected(edited.error());
        return writePsoFile(original, *edited);
    }
    case MetadataFormat::Rbf:
        return rbfFromXml(xml);
    case MetadataFormat::RscMeta: {
        const auto pages = resourcePages(original);
        if (!pages)
            return std::unexpected(pages.error());
        const auto data = readRscMeta(*pages);
        if (!data)
            return std::unexpected(data.error());
        const auto edited = metaFromXml(*data, xml, names);
        if (!edited)
            return std::unexpected(edited.error());
        const auto written = writeRscMeta(*pages, *edited);
        if (!written)
            return std::unexpected(written.error());
        const auto head = buildRsc7Header(Rsc7Header { rscMetaVersion, written->systemFlags, written->graphicsFlags });
        std::vector<std::byte> file(head.begin(), head.end());
        const auto body = compressRaw(written->pages);
        if (body.empty())
            return failure("the resource could not be compressed");
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }
    }
    return failure("the format is not supported");
}

}
