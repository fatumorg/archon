add_executable(archon
    main.cpp
    main_window.cpp
)
target_link_libraries(archon PRIVATE archon-core Qt6::Widgets)
install(TARGETS archon RUNTIME DESTINATION bin)
