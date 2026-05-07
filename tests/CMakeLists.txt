# Pixel-regression tests against canonical reference renders.
# First milestone: titlebar_test loads WACUP, captures the rendered
# titlebar, and compares streak silver positions + text glyph extent
# against the canonical reference image.

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(titlebar_test titlebar_test.cpp)
target_link_libraries(titlebar_test PRIVATE
    wasabiqt
    Qt6::Test
    Qt6::Widgets)

add_test(NAME titlebar COMMAND titlebar_test)

# Maki loader + VM smoke test — parses every .maki blob in the user's
# Winamp Modern + Bento skin folders (skipped if not installed) and
# verifies VM::addScript accepts the loaded Script.
add_executable(maki_loader_test maki_loader_test.cpp)
target_include_directories(maki_loader_test PRIVATE
    ${CMAKE_SOURCE_DIR})
target_link_libraries(maki_loader_test PRIVATE
    wasabiqt
    Qt6::Test)
add_test(NAME maki_loader COMMAND maki_loader_test)
