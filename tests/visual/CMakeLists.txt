# Visual rendering harness — offscreen PNGs of expanded layouts for
# pixel-regression diffs.  Builds the `render_layout` binary that is
# the workhorse for `compare_visual.sh`.

add_executable(render_layout render_layout.cpp)
target_link_libraries(render_layout PRIVATE
    wasabiqt
    Qt6::Gui)

# Wire `compare_visual.sh` as a CTest case so `ctest` runs the
# pixel-diff against committed goldens.
add_test(NAME visual_diff
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/compare_visual.sh
            $<TARGET_FILE:render_layout>
            ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(visual_diff PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Side-by-side titlebar comparison — renders the current Modern
# titlebar strip stacked under /tmp/sbs_13.png (or whatever
# WASABIQT_REF_TITLEBAR points at) for eye-checking.  Output:
# /tmp/sbs_titlebar.png.  Run via: ctest -R sbs_titlebar
add_test(NAME sbs_titlebar
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/sbs_titlebar.sh
            Blues
            $<TARGET_FILE:render_layout>)
set_tests_properties(sbs_titlebar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
