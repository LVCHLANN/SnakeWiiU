cmake_minimum_required(VERSION 3.20)
project(wiiu_sdl2 C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Toolchain (the wrapper sets this too; keeping it helps IDE/manual runs)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(EXISTS "/opt/devkitpro/wut/share/wut.toolchain.cmake")
        set(CMAKE_TOOLCHAIN_FILE "/opt/devkitpro/wut/share/wut.toolchain.cmake" CACHE FILEPATH "" FORCE)
    elseif(DEFINED ENV{DEVKITPRO} AND EXISTS "$ENV{DEVKITPRO}/wut/share/wut.toolchain.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/wut/share/wut.toolchain.cmake" CACHE FILEPATH "" FORCE)
    endif()
endif()

# Find the correct pkg-config wrapper
find_program(WIIU_PKGCONF
        NAMES powerpc-eabi-pkg-config wiiu-pkg-config ppc-pkg-config pkg-config
        HINTS /c/devkitPro/portlibs/wiiu/bin /opt/devkitpro/portlibs/wiiu/bin /opt/devkitpro/msys2/usr/bin
        REQUIRED
)
set(PKG_CONFIG_EXECUTABLE "${WIIU_PKGCONF}" CACHE FILEPATH "pkg-config to use for Wii U")

# Make pkg-config see Wii U .pc files
if(DEFINED ENV{DEVKITPRO})
    set(ENV{PKG_CONFIG_LIBDIR} "$ENV{DEVKITPRO}/portlibs/wiiu/lib/pkgconfig")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "$ENV{DEVKITPRO}")
else()
    set(ENV{PKG_CONFIG_LIBDIR} "/opt/devkitpro/portlibs/wiiu/lib/pkgconfig")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "/opt/devkitpro")
endif()

# SDL2 via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(wiiu_snake
        src/main.cpp
)

# Compiler flags/includes from SDL2
target_include_directories(wiiu_snake PRIVATE ${SDL2_INCLUDE_DIRS})
target_compile_options(wiiu_snake PRIVATE ${SDL2_CFLAGS_OTHER})

# Normalize DEVKITPRO for MSYS/Windows
if(DEFINED ENV{DEVKITPRO})
    file(TO_CMAKE_PATH "$ENV{DEVKITPRO}" DEVKITPRO_FS)
else()
    set(DEVKITPRO_FS "/opt/devkitpro")
endif()

# (Optional) Add lib search dirs; harmless if already provided by specs
target_link_directories(wiiu_snake PRIVATE
        "${DEVKITPRO_FS}/wut/lib"
        "${DEVKITPRO_FS}/wut/lib/powerpc-eabi"   # present on some installs
        "${DEVKITPRO_FS}/portlibs/wiiu/lib"
)

# ✅ New WUT layout: link ONLY 'wut' (NOT coreinit/vpad/whb)
target_link_libraries(wiiu_snake PRIVATE
        ${SDL2_LIBRARIES}
        wut
)

# Produce RPX (provided by the wrapper/toolchain)
wut_create_rpx(wiiu_snake)
