cmake_minimum_required(VERSION 3.3.0)

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

add_subdirectory(mugfx)
add_subdirectory(utxt)

add_subdirectory(deps/stb)
add_subdirectory(deps/fast_obj)

add_library(um src/um.cpp)
target_include_directories(um PUBLIC include/)
ung_set_wall(um)
set_no_exceptions(um)
set_no_rtti(um)

set(UNG_SOURCES
  src/ung.cpp
  src/allocator.cpp
)

add_library(ung ${UNG_SOURCES})
target_include_directories(ung PUBLIC include/)
target_compile_definitions(ung PUBLIC SDL_MAIN_HANDLED) # don't override main()
target_link_libraries(ung PRIVATE um)
target_link_libraries(ung PRIVATE stb_image)
target_link_libraries(ung PRIVATE fast_obj)
target_link_libraries(ung PUBLIC utxt)
target_link_libraries(ung PUBLIC mugfx)
ung_set_wall(ung)
set_no_exceptions(ung)
set_no_rtti(ung)

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

# This will only be true if this project is not used as a subdirectory (e.g. FetchContent)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  option(UNG_BUILD_EXAMPLES "Build Examples" ON)

  if(UNG_BUILD_EXAMPLES)
    add_subdirectory(examples)
  endif()
endif()
