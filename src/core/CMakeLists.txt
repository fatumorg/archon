add_library(archon-core STATIC
    crypto/aes.cpp
    crypto/hash.cpp
    crypto/inflate.cpp
    io/byte_reader.cpp
    version.cpp
)
target_include_directories(archon-core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(archon-core PRIVATE ARCHON_VERSION="${PROJECT_VERSION}")
target_link_libraries(archon-core PUBLIC Qt6::Core PRIVATE ZLIB::ZLIB OpenSSL::Crypto)
