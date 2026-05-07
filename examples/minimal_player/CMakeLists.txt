# Minimal embedder showing how to host a WasabiQt skin in a Qt app.
add_executable(wasabiqt_minimal_player main.cpp)
target_link_libraries(wasabiqt_minimal_player PRIVATE
    wasabiqt
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)
