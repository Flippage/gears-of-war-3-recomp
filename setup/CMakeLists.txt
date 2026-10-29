# The options catalog, model and TOML editor are shared with the in-game overlay (they stay in src/).
set(GOW3_OPTIONS_SOURCES
  ${CMAKE_SOURCE_DIR}/src/gow3_options_catalog.cpp
  ${CMAKE_SOURCE_DIR}/src/gow3_options_model.cpp
  ${CMAKE_SOURCE_DIR}/src/gow3_options_values_text.cpp
  ${CMAKE_SOURCE_DIR}/src/gow3_toml_edit.cpp
  ${CMAKE_SOURCE_DIR}/src/gow3_language_registry.cpp)
add_library(gow3_setup STATIC finder.cpp install.cpp json_scan.cpp launch_decision.cpp update_files.cpp settings_store.cpp state.cpp stfs.cpp xdvdfs.cpp xex_info.cpp ${GOW3_OPTIONS_SOURCES})
target_include_directories(gow3_setup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/src)
target_compile_features(gow3_setup PUBLIC cxx_std_20)
target_link_libraries(gow3_setup PUBLIC rex::tomlplusplus)
