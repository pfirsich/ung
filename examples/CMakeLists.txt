add_executable(hello_game hello_game.cpp)
target_link_libraries(hello_game PRIVATE ung)
target_compile_definitions(hello_game PUBLIC SDL_MAIN_HANDLED) # don't override main()

if(EMSCRIPTEN)
  # Instead of INITIAL_HEAP, use ALLOW_MEMORY_GROWTH if appropriate, though it may cause stuttering
  target_link_options(hello_game PRIVATE
    -sINITIAL_HEAP=8mb
    # https://emscripten.org/docs/porting/files/packaging_files.html
    --embed-file=../examples/hello_game.vert@/examples/hello_game.vert
    --embed-file=../examples/hello_game.frag@/examples/hello_game.frag
    --embed-file=../examples/assets/checkerboard.png@/examples/assets/checkerboard.png
    # --shell-file path/to/shell.html # https://emscripten.org/docs/compiling/Deploying-Pages.html
  )
  set_target_properties(hello_game PROPERTIES SUFFIX ".html")
endif()
