add_library(archon-core STATIC
    crypto/aes.cpp
    crypto/dotnet_random.cpp
    crypto/hash.cpp
    crypto/inflate.cpp
    crypto/key_digests.cpp
    crypto/keys.cpp
    crypto/ng.cpp
    game/game_browser.cpp
    game/game_install.cpp
    io/byte_reader.cpp
    io/path_resolver.cpp
    io/writable_file.cpp
    rpf/rpf_archive.cpp
    rpf/rpf_extract.cpp
    rpf/rpf_reader.cpp
    rpf/rpf_toc.cpp
    rpf/rpf_writer.cpp
    rsc/rsc7.cpp
    safety/transaction.cpp
    search/entry_search.cpp
    version.cpp
)
target_include_directories(archon-core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(archon-core PRIVATE ARCHON_VERSION="${PROJECT_VERSION}")
target_link_libraries(archon-core PUBLIC Qt6::Core PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
