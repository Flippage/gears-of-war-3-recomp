# launcher.exe: first-run setup, content upgrades, settings and updates (Win32 + DX11 Dear ImGui).
# The ImGui core comes from rexruntime.dll (it exports the ImGui API); only the two backends are
# compiled here, from thirdparty/imgui-backends (Dear ImGui 1.92.5, MIT).
enable_language(RC)
# The toolchain pairs clang++ with rc.exe, and CMake's clang rule hands rc.exe clang's
# preprocessor and depfile flags, which it rejects: compile the .rc with rc.exe alone.
set(CMAKE_DEPFILE_FLAGS_RC "")
set(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> /nologo <DEFINES> <INCLUDES> /fo <OBJECT> <SOURCE>")

set(GOW3_IMGUI_BACKENDS ${CMAKE_SOURCE_DIR}/thirdparty/imgui-backends)
add_executable(launcher WIN32
  main.cpp
  theme.cpp
  pages_game.cpp
  pages_settings.cpp
  ${CMAKE_SOURCE_DIR}/src/gow3_keybind_text.cpp
  ${CMAKE_SOURCE_DIR}/src/gow3_options_actions.cpp
  launcher.rc
  launcher.manifest
  ${GOW3_IMGUI_BACKENDS}/imgui_impl_win32.cpp
  ${GOW3_IMGUI_BACKENDS}/imgui_impl_dx11.cpp)
target_include_directories(launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${GOW3_IMGUI_BACKENDS})
target_link_libraries(launcher PRIVATE gow3_setup rex::runtime d3d11 d3dcompiler dxgi dwmapi ole32 shell32)
# Next to gow3.exe and rexruntime.dll, as in a release.
set_target_properties(launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
