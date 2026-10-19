cmake_minimum_required(VERSION 3.21)

project(vector_pinball VERSION 0.1.0 LANGUAGES C CXX)

# Vector Pinball fuer VectorPie (Raspberry Pi + USB-DVG/BeamForge, oder ein Fenster auf dem PC).
#
#   Windows, direkt mit MSYS2 MINGW64 (gcc, SDL2, wie der Vectrex-Simulator):
#       cmake --preset win && cmake --build --preset win        -> build/win/pinball.exe
#   Pi-Fassung auf dem PC ohne WSL (clang gegen eine Kopie der Bibliotheken des Automaten), dann tools/make_vpk.sh:
#       cmake --preset pi-clang && cmake --build --preset pi-clang   -> build/pi/pinball (arm64)
#   WSL2 Debian 13 nach sdk-repo/sdk/tools/setup-wsl.sh (so baut das SDK selbst):
#       cmake --preset pc && cmake --build --preset pc          -> build/pc/pinball.exe
#       cmake --preset pi && cmake --build --preset pi          -> build/pi/pinball (arm64)
#       cmake --build --preset pi --target vpk                  -> build/pi/vpinball-<version>.vpk
#
# Spielkern, Ton, Schrift, Vorspann, Einstellungen und Bestenliste kommen unveraendert aus ../vectrex; eigen sind hier nur
# die Plattform und die Dateien, in denen Farbe und Bildformat stecken (siehe docs/ARCHITECTURE.md).

set(VP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk-repo/sdk" CACHE PATH "VectorPie SDK (Ordner sdk des Repos Goldfinch2/VectorPie)")
set(VP_SOUND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vectrex/sdcard/pinball/snd" CACHE PATH "Tonaufnahmen (make sounds in ../vectrex)")
if(NOT EXISTS "${VP_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "VectorPie SDK fehlt: git clone https://github.com/Goldfinch2/VectorPie.git sdk-repo  (siehe docs/SETUP.md)")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(VPIE_PI_CLANG)
    # cmake/toolchain-pi-clang.cmake: das SDL2 des Automaten, ueber seine Datei im Sysroot
    add_library(SDL2::SDL2 SHARED IMPORTED GLOBAL)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/libSDL2-2.0.so.0"
        IMPORTED_SONAME "libSDL2-2.0.so.0"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SYSROOT}/usr/include/SDL2"
        INTERFACE_COMPILE_DEFINITIONS "_REENTRANT")
    set(THREADS_PREFER_PTHREAD_FLAG ON)
endif()

set(VP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(VP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${VP_SDK_DIR}" sdk)

set(VX "${CMAKE_CURRENT_SOURCE_DIR}/../vectrex")

# Der Spielkern wird unveraendert mit der Vectrex-Fassung geteilt. Zwei seiner Grenzen sind dort aber fuer den Strahl der
# Vectrex gesetzt: auf Tisch 7 gehen nur die hellsten Sterne an die Anzeige, und die Liste der Formen fasst 160. Hier soll
# die ganze Sternkarte des Originals zu sehen sein. Darum uebersetzt dieser Build eine KOPIE des Kerns (build/.../core), in
# der genau diese Zahlen angehoben sind - die Quellen bleiben, wie sie sind. Passt eine Ersetzung nicht mehr (der Kern hat
# sich geaendert), bricht das Konfigurieren ab, statt die Grenze stillschweigend zu verlieren.
set(CORE "${CMAKE_CURRENT_BINARY_DIR}/core")
function(vpie_core_copy rel)
    file(READ "${VX}/core/${rel}" text)
    set(i 1)
    while(i LESS ARGC)
        math(EXPR j "${i} + 1")
        set(from "${ARGV${i}}")
        set(to "${ARGV${j}}")
        string(FIND "${text}" "${from}" at)
        if(at EQUAL -1)
            message(FATAL_ERROR "vectrex/core/${rel}: '${from}' nicht gefunden - die Grenze dort hat sich geaendert, CMakeLists.txt anpassen")
        endif()
        string(REPLACE "${from}" "${to}" text "${text}")
        math(EXPR i "${i} + 2")
    endwhile()
    set(old "")
    if(EXISTS "${CORE}/${rel}")
        file(READ "${CORE}/${rel}" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${CORE}/${rel}" "${text}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VX}/core/${rel}")
endfunction()
file(GLOB core_files RELATIVE "${VX}/core" "${VX}/core/*.c" "${VX}/core/*.h" "${VX}/core/gen/*.c")
foreach(rel ${core_files})
    if(rel STREQUAL "vp.h")
        vpie_core_copy(${rel} "#define VP_MAX_SHAPES   160" "#define VP_MAX_SHAPES   2000")
    elseif(rel STREQUAL "vp_table7.c")
        vpie_core_copy(${rel} "#define T7_MAXPROJ  320" "#define T7_MAXPROJ  1536"
                              "#define T7_MAX_DOTS 48 " "#define T7_MAX_DOTS 1536 "
                              "#define T7_OTHER_STARS 12" "#define T7_OTHER_STARS 1536")
    else()
        vpie_core_copy(${rel})
    endif()
endforeach()

add_executable(pinball
    game/main.c game/attract.c game/play.c game/entry.c game/service.c game/art.c
    game/gfx.c game/render.c game/segdisp.c game/fx.c
    platform/vectorpie.c platform/vpbind.c
    ${VX}/game/font.c ${VX}/game/settings.c ${VX}/game/hiscore.c
    ${VX}/game/sound.c ${VX}/game/aysynth.c ${VX}/game/pcm.c ${VX}/game/mp3.c
    ${CORE}/vp_field.c ${CORE}/vp_phys.c
    ${CORE}/vp_table1.c ${CORE}/vp_table5.c ${CORE}/vp_table7.c ${CORE}/vp_table9.c ${CORE}/vp_table10.c
    ${CORE}/gen/table_data.c ${CORE}/gen/star_data.c ${CORE}/gen/lid_data.c)
# Reihenfolge: die eigenen Ordner zuerst (game/gfx.h ersetzt das der Vectrex-Fassung), platform/shim liefert den Ersatz fuer
# vectrex/vectrexInterface.h, den sound.c einbindet
target_include_directories(pinball PRIVATE game platform platform/shim
    ${VX}/game ${CORE} ${VX}/platform ${VX}/third_party/minimp3)
set_target_properties(pinball PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
# Lautstaerken EFFECTS und MUSIC: sound.c ruft den Mischer ueber die Huellen in platform/vectorpie.c (siehe dort)
set_source_files_properties(${VX}/game/sound.c PROPERTIES
    COMPILE_DEFINITIONS "pcm_play=vpie_pcm_play;pcm_music_start=vpie_pcm_music_start;aysynth_render=vpie_aysynth_render")
vp_program(pinball)                       # aus dem SDK: bindet libvectorpie, legt SDL2.dll neben die .exe
target_compile_options(pinball PRIVATE -Wno-unused-parameter -Wno-comment)

# Was das Spiel liest, liegt neben dem Programm in data/ (vp_asset_path)
# game.ini gehoert auch dazu: die Vorgabe der Knopfbelegung steht darin
add_custom_command(TARGET pinball POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/game.ini" "$<TARGET_FILE_DIR:pinball>/game.ini")
if(EXISTS "${VP_SOUND_DIR}")
    add_custom_command(TARGET pinball POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${VP_SOUND_DIR}" "$<TARGET_FILE_DIR:pinball>/data/snd")
else()
    message(WARNING "Keine Tonaufnahmen in ${VP_SOUND_DIR}: das Spiel spielt dann die nachgebildeten Chip-Effekte (make sounds in ../vectrex)")
endif()

# Paket fuer den Automaten: Ordner vpinball/ mit game.ini, Programm und data/, daraus die .vpk (vp-pack des SDK)
if(UNIX)
    set(PKG "${CMAKE_CURRENT_BINARY_DIR}/pkg/vpinball")
    add_custom_target(vpk
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${PKG}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PKG}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/game.ini" "$<TARGET_FILE:pinball>" "${PKG}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:pinball>/data" "${PKG}/data"
        COMMAND sh "${VP_SDK_DIR}/tools/vp-pack/vp-pack.sh" "${PKG}" "${CMAKE_CURRENT_BINARY_DIR}"
        DEPENDS pinball VERBATIM)
endif()
