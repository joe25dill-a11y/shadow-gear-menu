cmake_minimum_required(VERSION 3.20)
project(shadow-gear-menu LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MENU_SOURCES
  src/log.cpp
  src/log.h
  src/settings_store.cpp
  src/settings_store.h
  src/features.cpp
  src/features.h
  src/value_scan.cpp
  src/value_scan.h
  src/menu.cpp
  src/menu.h
)

add_library(shadow-gear-menu SHARED
  src/dllmain.cpp
  src/gear_window.cpp
  src/gear_window.h
  ${MENU_SOURCES}
  app.manifest
)

add_executable(shadow-menu-preview WIN32
  src/preview_main.cpp
  ${MENU_SOURCES}
  app.manifest
)

foreach(target shadow-gear-menu shadow-menu-preview)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
  target_link_libraries(${target} PRIVATE user32 gdi32 gdiplus dwmapi)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  endif()
endforeach()

target_compile_definitions(shadow-menu-preview PRIVATE FEATURES_PREVIEW)

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
