find_package(wxWidgets REQUIRED COMPONENTS core base)
add_executable(gfd_modal_dialog_tests
    test_gfd_modal_dialog.cpp
    ../../src/slic3r/GUI/GFDModalDialog.cpp
)
target_include_directories(gfd_modal_dialog_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(gfd_modal_dialog_tests SYSTEM PRIVATE ${wxWidgets_INCLUDE_DIRS})
target_compile_options(gfd_modal_dialog_tests PRIVATE ${wxWidgets_CXX_FLAGS})
target_compile_definitions(gfd_modal_dialog_tests PRIVATE
    ${wxWidgets_DEFINITIONS}
    "$<$<CONFIG:Debug>:${wxWidgets_DEFINITIONS_DEBUG}>"
)
if (SLIC3R_STATIC)
    target_compile_definitions(gfd_modal_dialog_tests PRIVATE wxDEBUG_LEVEL=0)
endif()
target_link_libraries(gfd_modal_dialog_tests PRIVATE ${wxWidgets_LIBRARIES})
set_property(TARGET gfd_modal_dialog_tests PROPERTY FOLDER "tests")
add_test(NAME gui:gfd_modal_dialog COMMAND gfd_modal_dialog_tests)
set_tests_properties(gui:gfd_modal_dialog PROPERTIES LABELS "gui" TIMEOUT 45)
