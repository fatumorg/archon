add_library(archon-app STATIC
    address_bar.cpp
    busy.cpp
    display_format.cpp
    game_setup.cpp
    main_window.cpp
    search_dialog.cpp
    style.cpp
)
target_link_libraries(archon-app PUBLIC archon-core Qt6::Widgets Qt6::Svg Qt6::Network)

add_executable(archon main.cpp icons.qrc)
target_link_libraries(archon PRIVATE archon-app)
install(TARGETS archon RUNTIME DESTINATION bin)
install(FILES archon.desktop DESTINATION share/applications)
install(FILES icons/archon.svg DESTINATION share/icons/hicolor/scalable/apps)
