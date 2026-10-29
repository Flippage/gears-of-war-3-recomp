add_library(gow3_setup STATIC finder.cpp state.cpp stfs.cpp xdvdfs.cpp xex_info.cpp)
target_include_directories(gow3_setup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(gow3_setup PUBLIC cxx_std_20)
