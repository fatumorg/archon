function(archon_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE archon-core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

archon_add_test(tst_main_window
    app/tst_main_window.cpp
    ${CMAKE_SOURCE_DIR}/src/app/main_window.cpp
)
target_link_libraries(tst_main_window PRIVATE Qt6::Widgets)

archon_add_test(tst_byte_reader core/tst_byte_reader.cpp)
