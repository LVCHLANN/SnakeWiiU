cmake_minimum_required(VERSION 3.20)
project(wiiu_snake C)

add_executable(wiiu_snake src/main.c)

# Let the toolchain handle libraries/paths; this emits the .rpx
wut_create_rpx(wiiu_snake)
add_custom_command(TARGET wiiu_snake POST_BUILD
  COMMAND /opt/devkitpro/tools/bin/wuhbtool
          ${CMAKE_CURRENT_BINARY_DIR}/wiiu_snake.rpx
          ${CMAKE_CURRENT_BINARY_DIR}/wiiu_snake.wuhb
          --name "Snake (Dev Test)"
          --short-name "Snake"
          --author "God Tier Gamers"
          --icon ${CMAKE_SOURCE_DIR}/assets/iconTex.tga
          --tv-image ${CMAKE_SOURCE_DIR}/assets/bootTvTex.tga
          --drc-image ${CMAKE_SOURCE_DIR}/assets/bootDrcTex.tga
  VERBATIM)

