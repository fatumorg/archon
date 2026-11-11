add_executable(fuzz_rpf fuzz_rpf.cpp)
target_link_libraries(fuzz_rpf PRIVATE archon-test-support)
target_link_options(fuzz_rpf PRIVATE -fsanitize=fuzzer)

add_executable(write_fuzz_seed write_fuzz_seed.cpp)
target_link_libraries(write_fuzz_seed PRIVATE archon-test-support)
