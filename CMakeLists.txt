cmake_minimum_required(VERSION 3.20)
project(shadow-gear-menu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(shadow-gear-menu SHARED
  src/dllmain.cpp
  src/gear_window.cpp
  src/gear_window.h
  app.manifest
)

target_compile_definitions(shadow-gear-menu PRIVATE UNICODE _UNICODE)
target_link_libraries(shadow-gear-menu PRIVATE user32 gdi32 comctl32)

if(MSVC)
  target_compile_options(shadow-gear-menu PRIVATE /W4 /permissive-)
endif()

set(SOW_PLUGINS "E:/SteamLibrary/steamapps/common/ShadowOfWar/x64/plugins" CACHE PATH
  "Shadow of War plugins folder. The built DLL is copied here.")

if(EXISTS "${SOW_PLUGINS}")
  add_custom_command(TARGET shadow-gear-menu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:shadow-gear-menu>"
      "${SOW_PLUGINS}/shadow-gear-menu.dll"
    COMMENT "Copy shadow-gear-menu.dll into the game plugins folder"
  )
endif()
