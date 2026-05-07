# wq_view — minimal skin viewer demo.  Opens a <container>/<layout>
# from a parsed skin XML in a frameless Qt window.

add_executable(wq_view main.cpp)
target_link_libraries(wq_view PRIVATE
    wasabiqt
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)
