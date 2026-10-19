# Pi-Build ohne WSL: clang und lld auf dem PC, gegen eine Kopie der Bibliotheken und Kopfdateien des Automaten
# (Sysroot). Derselbe Weg wie im Tempest-2000-Port (F:/Tempest 2000 Port, BUILD.md beschreibt, wie die Kopie entsteht);
# dessen Sysroot wird hier mitbenutzt. Anderer Ort: -DVPIE_PI_SYSROOT=<Ordner>.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(VPIE_PI_SYSROOT "F:/Tempest 2000 Port/build/pi-sysroot" CACHE PATH "Kopie von /usr und /lib des Automaten")
get_filename_component(VPIE_PI_SYSROOT "${VPIE_PI_SYSROOT}" ABSOLUTE)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_SYSROOT "${VPIE_PI_SYSROOT}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libstdc++ -static-libgcc -Wl,--allow-shlib-undefined")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${VPIE_PI_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# SDL2 kommt als fertiges Ziel aus dem Sysroot (siehe CMakeLists.txt)
set(CMAKE_DISABLE_FIND_PACKAGE_SDL2 TRUE)
set(VPIE_PI_CLANG TRUE)
