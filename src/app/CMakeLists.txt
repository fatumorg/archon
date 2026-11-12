add_library(archon-app STATIC
    address_bar.cpp
    asi_manager_dialog.cpp
    busy.cpp
    display_format.cpp
    download.cpp
    game_setup.cpp
    history_dialog.cpp
    main_window.cpp
    package_creator_dialog.cpp
    package_installer_dialog.cpp
    prefix_setup_dialog.cpp
    search_dialog.cpp
    style.cpp
    texture_viewer.cpp
)
target_link_libraries(archon-app PUBLIC archon-core Qt6::Widgets Qt6::Svg Qt6::Network)

add_executable(archon main.cpp icons.qrc)
target_link_libraries(archon PRIVATE archon-app)
install(TARGETS archon RUNTIME DESTINATION bin)
install(FILES archon.desktop DESTINATION share/applications)
install(FILES icons/archon.svg DESTINATION share/icons/hicolor/scalable/apps)
