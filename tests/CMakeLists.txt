add_library(archon-test-support STATIC
    core/support/browse_fixture.cpp
    core/support/real_keys.cpp
    core/support/rpf_builder.cpp
    core/support/test_bytes.cpp
    core/support/zip_builder.cpp
)
target_compile_definitions(archon-test-support PRIVATE ARCHON_TEST_CACHE_DIR="${CMAKE_BINARY_DIR}")
target_include_directories(archon-test-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(archon-test-support PUBLIC archon-core Qt6::Test PRIVATE ZLIB::ZLIB OpenSSL::Crypto PkgConfig::LIBZIP)

function(archon_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE archon-test-support)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

function(archon_add_app_test name)
    archon_add_test(${name} ${ARGN} ${CMAKE_SOURCE_DIR}/src/app/icons.qrc)
    target_link_libraries(${name} PRIVATE archon-app)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
endfunction()

archon_add_app_test(tst_main_window app/tst_main_window.cpp)
archon_add_app_test(tst_display_format app/tst_display_format.cpp)
archon_add_app_test(tst_game_setup app/tst_game_setup.cpp)
archon_add_app_test(tst_asi_dialogs app/tst_asi_dialogs.cpp)
archon_add_app_test(tst_package_installer app/tst_package_installer.cpp)

archon_add_test(tst_byte_reader core/tst_byte_reader.cpp)
archon_add_test(tst_crypto_primitives core/tst_crypto_primitives.cpp)
archon_add_test(tst_keys core/tst_keys.cpp)
archon_add_test(tst_ng core/tst_ng.cpp)
archon_add_test(tst_rpf_archive core/tst_rpf_archive.cpp)
archon_add_test(tst_rsc7 core/tst_rsc7.cpp)
archon_add_test(tst_rpf_reader core/tst_rpf_reader.cpp)
archon_add_test(tst_path_resolver core/tst_path_resolver.cpp)
archon_add_test(tst_game_install core/tst_game_install.cpp)
archon_add_test(tst_rpf_extract core/tst_rpf_extract.cpp)
archon_add_test(tst_game_browser core/tst_game_browser.cpp)
archon_add_test(tst_entry_search core/tst_entry_search.cpp)
archon_add_test(tst_transaction core/tst_transaction.cpp)
archon_add_test(tst_rpf_writer core/tst_rpf_writer.cpp)
archon_add_test(tst_game_edit core/tst_game_edit.cpp)
archon_add_test(tst_zip_archive core/tst_zip_archive.cpp)
archon_add_test(tst_asi_manager core/tst_asi_manager.cpp)
archon_add_test(tst_prefix core/tst_prefix.cpp)
archon_add_test(tst_oiv_package core/tst_oiv_package.cpp)
archon_add_test(tst_content_edit core/tst_content_edit.cpp)
archon_add_test(tst_oiv_installer core/tst_oiv_installer.cpp)
archon_add_test(tst_real_sweep core/tst_real_sweep.cpp)

set_tests_properties(tst_game_setup tst_keys tst_ng tst_rpf_archive tst_rpf_reader tst_game_install tst_rpf_extract tst_rpf_writer tst_oiv_installer PROPERTIES LABELS real)
set_tests_properties(tst_oiv_installer PROPERTIES TIMEOUT 3600 ENVIRONMENT QTEST_FUNCTION_TIMEOUT=3600000)
set_tests_properties(tst_rpf_writer PROPERTIES TIMEOUT 3600 ENVIRONMENT QTEST_FUNCTION_TIMEOUT=3600000)
set_tests_properties(tst_real_sweep PROPERTIES LABELS sweep TIMEOUT 14400 ENVIRONMENT QTEST_FUNCTION_TIMEOUT=14400000)

if(ARCHON_FUZZ)
    add_subdirectory(fuzz)
endif()
