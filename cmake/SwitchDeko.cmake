# Additional experimental executable; the existing GLES target stays independent.
option(GT3R_BUILD_SWITCH_DEKO_APP "Build the real app with native Deko SDL menus" OFF)
if(NOT GT3R_BUILD_SWITCH_DEKO_APP)
  return()
endif()
set(GT3R_DEKO_APP_DIR "${CMAKE_SOURCE_DIR}/build-switch-deko-app" CACHE PATH "Deko app artifacts and prepared SDL")
set(_deko_sdl "${GT3R_DEKO_APP_DIR}/libSDL3-deko.a")
if(NOT EXISTS "${_deko_sdl}")
  message(FATAL_ERROR "Prepare Deko SDL first with tools/switch/build_deko_app.sh")
endif()
add_executable(gt3r_deko_app EXCLUDE_FROM_ALL ${GT3R_APP_SOURCES} ${GT3R_UI_FONT_SRC}
  ${CMAKE_SOURCE_DIR}/tools/switch/deko-probe/graphics.cpp
  ${CMAKE_SOURCE_DIR}/tools/switch/deko-model/model.cpp
  ${CMAKE_SOURCE_DIR}/tools/switch/deko-session/session.cpp)
target_compile_definitions(gt3r_deko_app PRIVATE GT3R_SWITCH_DEKO_MENUS=1 GT3R_DEKO_SDL GT3R_DEKO_MODEL)
target_include_directories(gt3r_deko_app SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_link_libraries(gt3r_deko_app PRIVATE -Wl,--start-group
  "${_deko_sdl}" gt3r_engine deko3dd EGL GLESv2 glapi drm_nouveau nx m -Wl,--end-group)
set_target_properties(gt3r_deko_app PROPERTIES OUTPUT_NAME gt3r-deko-app RUNTIME_OUTPUT_DIRECTORY "${GT3R_DEKO_APP_DIR}")
target_link_options(gt3r_deko_app PRIVATE "-Wl,-Map,${GT3R_DEKO_APP_DIR}/gt3r-deko-app.map")
add_custom_command(TARGET gt3r_deko_app POST_BUILD
  COMMAND nacptool --create "GT3 Revisited Deko" "tomplayer07" "0.6.1-dk-envmath" "${GT3R_DEKO_APP_DIR}/app.nacp"
  COMMAND elf2nro $<TARGET_FILE:gt3r_deko_app> "${GT3R_DEKO_APP_DIR}/gt3r-deko-app.nro"
    --icon=${CMAKE_SOURCE_DIR}/packaging/switch_icon.jpg
    --nacp=${GT3R_DEKO_APP_DIR}/app.nacp --romfsdir=${GT3R_DEKO_APP_DIR}/romfs
  COMMENT "Building experimental native menu app")
