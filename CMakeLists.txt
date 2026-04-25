cmake_minimum_required(VERSION 3.14.7...4.2.3)

project(ung)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

include(FetchContent)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/wall.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/no-rtti.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/no-exceptions.cmake)

if(UNG_ENABLE_ASAN)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/asan.cmake)
endif()

option(UNG_STB_IMAGE "Use stb_image" ON)
option(UNG_FAST_OBJ "Use fast_obj" ON)
option(UNG_CGLTF "Use cgltf" ON)

add_subdirectory(mugfx)
add_subdirectory(utxt)

add_subdirectory(deps/miniaudio)

add_library(um src/um.cpp)
target_include_directories(um PUBLIC include/)
ung_set_wall(um)
ung_set_no_exceptions(um)
ung_set_no_rtti(um)

set(UNG_SOURCES
  src/ung.cpp
  src/animation.cpp
  src/allocator.cpp
  src/files.cpp
  src/geometry.cpp
  src/input.cpp
  src/load-profiler.cpp
  src/material.cpp
  src/model.cpp
  src/random.cpp
  src/render.cpp
  src/shader.cpp
  src/slotmap.cpp
  src/sound.cpp
  src/sprite_renderer.cpp
  src/text.cpp
  src/texture.cpp
  src/types.cpp
)

if(UNG_CGLTF)
  list(APPEND UNG_SOURCES src/model-cgltf.cpp)
endif()

add_library(ung ${UNG_SOURCES})
target_include_directories(ung PUBLIC include/)
target_compile_definitions(ung PUBLIC SDL_MAIN_HANDLED) # don't override main()
target_link_libraries(ung PRIVATE um)
target_link_libraries(ung PUBLIC utxt)
target_link_libraries(ung PUBLIC mugfx)
target_link_libraries(ung PRIVATE miniaudio)

if(UNG_STB_IMAGE)
  add_subdirectory(deps/stb)
  target_link_libraries(ung PRIVATE stb_image)
  target_compile_definitions(ung PRIVATE UNG_STB_IMAGE)
endif()

if(UNG_FAST_OBJ)
  add_subdirectory(deps/fast_obj)
  target_link_libraries(ung PRIVATE fast_obj)
  target_compile_definitions(ung PRIVATE UNG_FAST_OBJ)
endif()

if(UNG_CGLTF)
  add_subdirectory(deps/cgltf)
  target_link_libraries(ung PUBLIC cgltf)
  target_compile_definitions(ung PUBLIC UNG_CGLTF)
endif()

option(UNG_LOAD_PROFILING "Profile asset loads" OFF)
if (UNG_LOAD_PROFILING)
  target_compile_definitions(ung PRIVATE UNG_LOAD_PROFILING)
endif()

ung_set_wall(ung)
ung_set_no_exceptions(ung)
ung_set_no_rtti(ung)

if(NOT EMSCRIPTEN)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)

  CPMAddPackage(
    NAME SDL2
    VERSION 2.26.5
    URL "https://github.com/libsdl-org/SDL/releases/download/release-2.26.5/SDL2-2.26.5.zip"
    URL_HASH MD5=0664f3980570c4641128866e6c9f2e29
  )

  target_link_libraries(ung PUBLIC SDL2::SDL2)
else()
  # USE_SDL is both a compile and link flag!
  target_compile_options(ung PUBLIC
    -sUSE_SDL=2
  )
  target_link_options(ung PUBLIC
    -sUSE_SDL=2
  )
endif()

option(UNG_BUILD_EXAMPLES "Build Examples" ${PROJECT_IS_TOP_LEVEL})
if(UNG_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
