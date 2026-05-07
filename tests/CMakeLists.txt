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

# M2 smoke test — verifies the opensourced Maki VM (vcpu.cpp) links into
# libwasabiqt and accepts a .maki blob via VCPU::addScript().
add_executable(maki_loader_test maki_loader_test.cpp)
target_link_libraries(maki_loader_test PRIVATE
    wasabiqt
    Qt6::Test)
add_test(NAME maki_loader COMMAND maki_loader_test)
