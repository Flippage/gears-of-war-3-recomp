add_executable(gow3_setup_tests test_main.cpp test_util.cpp be_test.cpp xex_info_test.cpp state_test.cpp stfs_test.cpp xdvdfs_test.cpp finder_test.cpp launch_decision_test.cpp install_test.cpp settings_store_test.cpp json_scan_test.cpp update_files_test.cpp language_test.cpp)
target_link_libraries(gow3_setup_tests PRIVATE gow3_setup)
target_compile_definitions(gow3_setup_tests PRIVATE
  GOW3_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME setup COMMAND gow3_setup_tests)
set_target_properties(gow3_setup_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
