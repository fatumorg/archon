add_library(archon-core STATIC
    asi/asi_manager.cpp
    crypto/aes.cpp
    crypto/dotnet_random.cpp
    crypto/hash.cpp
    crypto/inflate.cpp
    crypto/key_digests.cpp
    crypto/keys.cpp
    crypto/ng.cpp
    game/game_browser.cpp
    game/game_edit.cpp
    game/game_install.cpp
    prefix/prefix.cpp
    io/byte_reader.cpp
    io/path_resolver.cpp
    io/writable_file.cpp
    io/zip_archive.cpp
    io/zip_writer.cpp
    meta/metadata_document.cpp
    meta/meta_xml.cpp
    meta/name_table.cpp
    meta/pso_file.cpp
    meta/rbf_file.cpp
    meta/rsc_meta_file.cpp
    meta/text_file.cpp
    oiv/content_edit.cpp
    oiv/oiv_installer.cpp
    oiv/oiv_package.cpp
    oiv/oiv_writer.cpp
    rpf/rpf_archive.cpp
    rpf/rpf_extract.cpp
    rpf/rpf_reader.cpp
    rpf/rpf_toc.cpp
    rpf/rpf_writer.cpp
    rsc/resource_file.cpp
    rsc/rsc7.cpp
    safety/transaction.cpp
    search/entry_search.cpp
    ytd/texture_dictionary.cpp
    ytd/texture_edit.cpp
    ytd/texture_format.cpp
    ytd/texture_pixels.cpp
    version.cpp
)
target_include_directories(archon-core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(archon-core PRIVATE ARCHON_VERSION="${PROJECT_VERSION}")
target_link_libraries(archon-core PUBLIC Qt6::Core PRIVATE ZLIB::ZLIB OpenSSL::Crypto PkgConfig::LIBZIP PkgConfig::PUGIXML)
