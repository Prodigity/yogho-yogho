cmake_minimum_required(VERSION 3.20)
project(yogho C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

file(GLOB YOGHO_SRC ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(yogho ${YOGHO_SRC} third_party/nuked-opl3/opl3.c)
target_include_directories(yogho PRIVATE src third_party/nuked-opl3)

if(EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".js")
  target_compile_options(yogho PRIVATE -O2 -sUSE_SDL=2)
  target_link_options(yogho PRIVATE -O2 -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1
    -sEXPORTED_RUNTIME_METHODS=FS -sFORCE_FILESYSTEM=1)
else()
  # Desktop: SDL2 VC dev package (C:\Tools\SDL2\SDL2-2.32.10)
  set(SDL2_DIR "C:/Tools/SDL2/SDL2-2.32.10/cmake" CACHE PATH "")
  find_package(SDL2 REQUIRED CONFIG)
  target_link_libraries(yogho PRIVATE SDL2::SDL2 SDL2::SDL2main)
  if(MSVC)
    target_compile_options(yogho PRIVATE /W3 /D_CRT_SECURE_NO_WARNINGS)
    set_target_properties(yogho PROPERTIES WIN32_EXECUTABLE FALSE)
  endif()
  add_custom_command(TARGET yogho POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:yogho>)
endif()
