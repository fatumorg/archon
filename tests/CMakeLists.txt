add_library(archon-test-support STATIC
    core/support/real_keys.cpp
    core/support/rpf_builder.cpp
    core/support/test_bytes.cpp
)
target_compile_definitions(archon-test-support PRIVATE ARCHON_TEST_CACHE_DIR="${CMAKE_BINARY_DIR}")
target_include_directories(archon-test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(archon-test-support PUBLIC archon-core Qt6::Test PRIVATE ZLIB::ZLIB OpenSSL::Crypto)

function(archon_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE archon-test-support)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

archon_add_test(tst_main_window
    app/tst_main_window.cpp
    ${CMAKE_SOURCE_DIR}/src/app/main_window.cpp
)
target_link_libraries(tst_main_window PRIVATE Qt6::Widgets)

archon_add_test(tst_byte_reader core/tst_byte_reader.cpp)
archon_add_test(tst_crypto_primitives core/tst_crypto_primitives.cpp)
archon_add_test(tst_keys core/tst_keys.cpp)
archon_add_test(tst_ng core/tst_ng.cpp)
archon_add_test(tst_rpf_archive core/tst_rpf_archive.cpp)
archon_add_test(tst_rsc7 core/tst_rsc7.cpp)
archon_add_test(tst_rpf_reader core/tst_rpf_reader.cpp)
archon_add_test(tst_path_resolver core/tst_path_resolver.cpp)
archon_add_test(tst_game_install core/tst_game_install.cpp)
