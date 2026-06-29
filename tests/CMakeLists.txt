# Pixel-regression tests against canonical reference renders.
# First milestone: titlebar_test loads WACUP, captures the rendered
# titlebar, and compares streak silver positions + text glyph extent
# against the canonical reference image.

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(titlebar_test titlebar_test.cpp)
target_link_libraries(titlebar_test PRIVATE
    qtwasabi
    Qt6::Test
    Qt6::Gui)
target_compile_definitions(titlebar_test PRIVATE
    QTWASABI_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME titlebar COMMAND titlebar_test)

# M2 smoke test — verifies the opensourced Maki VM (vcpu.cpp) links into
# libqtwasabi and accepts a .maki blob via VCPU::addScript().
add_executable(maki_loader_test maki_loader_test.cpp)
target_link_libraries(maki_loader_test PRIVATE
    qtwasabi
    Qt6::Test)
add_test(NAME maki_loader COMMAND maki_loader_test)

# M3 — Wasabi skin XML parser walks a skin.xml's <include> tree and
# extracts metadata + script paths.
add_executable(skin_xml_test skin_xml_test.cpp)
target_link_libraries(skin_xml_test PRIVATE
    qtwasabi
    Qt6::Test)
add_test(NAME skin_xml COMMAND skin_xml_test)

# M4 — first widget paints: BitmapRegistry resolves <bitmap> defs +
# LayerPainter renders a <layer> onto a QPainter canvas.
add_executable(layer_paint_test layer_paint_test.cpp)
target_link_libraries(layer_paint_test PRIVATE
    qtwasabi
    Qt6::Test
    Qt6::Gui)
add_test(NAME layer_paint COMMAND layer_paint_test)

# M6 — layout expansion: groupdef + sendparams resolution.
add_executable(layout_test layout_test.cpp)
target_link_libraries(layout_test PRIVATE
    qtwasabi
    Qt6::Test)
add_test(NAME layout COMMAND layout_test)

# M7 — paint a resolved tree onto a QImage canvas.
add_executable(tree_paint_test tree_paint_test.cpp)
target_link_libraries(tree_paint_test PRIVATE
    qtwasabi
    Qt6::Test
    Qt6::Widgets)
add_test(NAME tree_paint COMMAND tree_paint_test)
set_tests_properties(tree_paint PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M8 — SkinView QWidget smoke test (QApplication + grab()).
add_executable(skinview_test skinview_test.cpp)
target_link_libraries(skinview_test PRIVATE
    qtwasabi
    Qt6::Test
    Qt6::Widgets)
add_test(NAME skinview COMMAND skinview_test)
set_tests_properties(skinview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M13a — SkinRuntime loads .maki scripts + binds widget objects.
add_executable(skin_runtime_test skin_runtime_test.cpp)
target_link_libraries(skin_runtime_test PRIVATE
    qtwasabi
    Qt6::Test)
add_test(NAME skin_runtime COMMAND skin_runtime_test)

# M10 — bitmap-font text painter renders timer/song-info digits.
add_executable(text_paint_test text_paint_test.cpp)
target_link_libraries(text_paint_test PRIVATE
    qtwasabi
    Qt6::Test
    Qt6::Widgets)
add_test(NAME text_paint COMMAND text_paint_test)
set_tests_properties(text_paint PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
