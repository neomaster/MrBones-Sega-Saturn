cmake_minimum_required(VERSION 3.24)
project(mrbones_port LANGUAGES C)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if (MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND
    CMAKE_C_CL_SHOWINCLUDES_PREFIX MATCHES "^Observa")
    # Keep the prefix UTF-8 regardless of the configure console code page.
    string(ASCII 195 167 195 163 _msvc_pt_accents)
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "Observa${_msvc_pt_accents}o: incluindo arquivo: ")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_C_CL_SHOWINCLUDES_PREFIX}")
    unset(_msvc_pt_accents)
    if (NOT CMAKE_C_COMPILER_LAUNCHER)
        set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_CURRENT_SOURCE_DIR}/tools/msvc-utf8.cmd")
    endif()
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()
if (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_library(mbcore STATIC
    src/core/sha256.c
    src/core/cdimage.c
    src/core/resource.c
    src/core/runway_assets.c
    src/core/runway_sprite.c
    src/core/runway_projection.c
    src/core/runway_compose.c
    src/core/runway_input.c
    src/core/saturn_pad.c
    src/core/pan_collision.c
    src/core/pan_frame.c
    src/core/pan_palette.c
    src/core/stxtr.c
    src/core/snd1.c
    src/core/snd1_pcm.c
    src/core/sound_sequence.c
    src/core/sound_event.c
    src/core/cdda.c
    src/core/sprite_decode.c
    src/core/bones_state.c
    src/core/bones_message.c
    src/core/bones_bbs.c
    src/core/mover.c
    src/core/identity.c
    src/core/obj_scheduler.c
    src/core/joypad_brain.c
    src/core/pan_collision_map.c
    src/core/bones_tall.c
    src/core/mplay_cue.c
    src/core/pan_scroll.c
    src/core/runway_sfx.c
    src/core/runway_skel.c
    src/core/runway_hazard.c
    src/core/runway_phase.c
    src/core/nvm_shadow.c
    src/core/menu_phase.c
    src/core/stage_catalog.c
    src/core/stage_assets.c
    src/core/stage_runner.c
    src/core/stage_progress.c
    src/core/sidescrl_phase.c
    src/core/scrl_map.c
    src/core/scrl_decomp.c
    src/core/scrl_tilecache.c
    src/core/special_phase.c
    src/core/rhythm_table.c
    src/core/stage_enemy.c
    src/core/duck_avi.c
    src/core/duck_tm1.c
    src/core/mb_font.c
    src/core/menu_options.c
    src/core/menu_options_view.c
    src/core/aabb_hitchk.c
    src/core/sidescrl_physics.c
    src/core/sidescrl_slope.c
    src/core/sidescrl_wall.c
    src/core/sidescrl_motion.c
    src/platform/config.c
    src/platform/host_input.c)
target_include_directories(mbcore PUBLIC src)

set(MB_FFMPEG_ROOT "" CACHE PATH "Static FFmpeg installation")
set(MB_FFMPEG_SOURCE "${CMAKE_SOURCE_DIR}/build-static/ffmpeg-7.1.1" CACHE PATH "Configured FFmpeg 7.1.1 source tree")
if (WIN32 AND NOT MB_FFMPEG_ROOT)
    set(MB_FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/build-static/ffmpeg-min" CACHE PATH "Static FFmpeg installation" FORCE)
endif()
if (MB_FFMPEG_ROOT)
    if (NOT EXISTS "${MB_FFMPEG_ROOT}/include/libavcodec/avcodec.h" OR
        NOT EXISTS "${MB_FFMPEG_ROOT}/lib/libavcodec.a" OR
        NOT EXISTS "${MB_FFMPEG_SOURCE}/config.h" OR
        NOT EXISTS "${MB_FFMPEG_SOURCE}/libavcodec/truemotion1data.h")
        message(FATAL_ERROR "FFmpeg installation or configured source missing: MB_FFMPEG_ROOT=${MB_FFMPEG_ROOT}, MB_FFMPEG_SOURCE=${MB_FFMPEG_SOURCE}")
    endif()
    target_sources(mbcore PRIVATE src/third_party/truemotion1_saturn.c)
    set_source_files_properties(src/third_party/truemotion1_saturn.c PROPERTIES
        COMPILE_DEFINITIONS "HAVE_AV_CONFIG_H;BUILDING_avcodec;_WIN32_WINNT=0x0600")
    target_compile_definitions(mbcore PRIVATE MB_USE_FFMPEG=1)
    target_include_directories(mbcore PRIVATE "${MB_FFMPEG_SOURCE}"
        "${MB_FFMPEG_SOURCE}/libavcodec" "${MB_FFMPEG_SOURCE}/compat/atomics/win32"
        "${MB_FFMPEG_SOURCE}/compat/stdbit" "${MB_FFMPEG_ROOT}/include")
    target_link_libraries(mbcore PUBLIC
        "${MB_FFMPEG_ROOT}/lib/libavcodec.a"
        "${MB_FFMPEG_ROOT}/lib/libswresample.a"
        "${MB_FFMPEG_ROOT}/lib/libswscale.a"
        "${MB_FFMPEG_ROOT}/lib/libavutil.a"
        bcrypt secur32 ws2_32)
endif()

add_executable(mbverify src/tools/mbverify.c)
target_link_libraries(mbverify mbcore)

if (WIN32)
    add_executable(mbvisual src/tools/mbvisual.c)
    target_link_libraries(mbvisual mbcore user32 gdi32)
    add_executable(mbrunway src/tools/mbrunway.c src/platform/host_audio.c)
    target_link_libraries(mbrunway mbcore user32 gdi32 winmm)
    add_executable(mrbones src/tools/mrbones.c src/platform/host_audio.c)
    target_link_libraries(mrbones mbcore user32 gdi32 winmm)
    add_test(NAME test_mrbones_paths COMMAND ${CMAKE_COMMAND}
        "-DEXE=$<TARGET_FILE:mrbones>"
        "-DTEST_DIR=${CMAKE_BINARY_DIR}"
        "-DMANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt"
        "-DDISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue"
        -P "${CMAKE_SOURCE_DIR}/tests/test_mrbones_paths.cmake")
    add_executable(test_movie_audio tests/test_movie_audio.c src/platform/host_audio.c)
    target_link_libraries(test_movie_audio mbcore winmm)
    add_test(NAME test_movie_audio COMMAND test_movie_audio)
    set_tests_properties(test_movie_audio PROPERTIES
        ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue")
    # §19: headless native campaign (MENU→Disc1→INSERTCD→Disc2→FINALE→credits)
    add_test(NAME test_full_campaign
        COMMAND mrbones
            "${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue"
            "${CMAKE_SOURCE_DIR}/manifest/versions.txt"
            --disc2 "${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue"
            --full-campaign
            --save-dir "${CMAKE_BINARY_DIR}/full_campaign_save"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    set_tests_properties(test_full_campaign PROPERTIES TIMEOUT 300)
    add_test(NAME test_fmv_transition COMMAND mrbones
        "${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue"
        "${CMAKE_SOURCE_DIR}/manifest/versions.txt"
        --disc2 "${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue"
        --check --save-dir "${CMAKE_BINARY_DIR}/test_fmv_transition")
    set_tests_properties(test_fmv_transition PROPERTIES TIMEOUT 300)
endif()

enable_testing()
if (MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND
    CMAKE_C_CL_SHOWINCLUDES_PREFIX MATCHES "^Observa")
    add_test(NAME test_msvc_dependencies COMMAND ${CMAKE_COMMAND}
        "-DNINJA=${CMAKE_MAKE_PROGRAM}"
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_SOURCE_DIR}/tests/test_msvc_dependencies.cmake")
endif()
add_executable(test_duck_no_ffmpeg tests/test_duck_no_ffmpeg.c src/core/duck_avi.c src/core/duck_tm1.c)
target_include_directories(test_duck_no_ffmpeg PRIVATE src)
add_test(NAME test_duck_no_ffmpeg COMMAND test_duck_no_ffmpeg)
if (WIN32 AND MB_FFMPEG_ROOT)
    add_executable(test_fmv_all tests/test_fmv_all.c)
    target_link_libraries(test_fmv_all mbcore)
    add_test(NAME test_fmv_all COMMAND test_fmv_all)
    set_tests_properties(test_fmv_all PROPERTIES TIMEOUT 300
        ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue")
endif()
foreach(t test_sha256 test_cdimage test_config test_stxtr test_bones_state test_sprite_decode test_runway_sprite test_snd1 test_snd1_pcm test_mover test_cdda test_runway_projection test_bones_message test_bones_bbs test_runway_input test_sound_sequence test_pan_collision test_sound_event test_saturn_pad test_pan_frame test_pan_palette test_runway_compose test_host_input test_obj_scheduler test_pan_collision_map test_bones_tall test_mplay_cue test_runway_phase test_runway_sfx test_runway_skel test_nvm_shadow test_menu_phase test_stage_catalog test_stage_runner test_stage_progress test_sidescrl_phase test_scrl_map test_special_phase test_duck_avi test_rhythm_enemies test_menu_options test_aabb_hitchk test_sidescrl_physics test_sidescrl_slope test_menu_options_view test_sidescrl_wall test_sidescrl_motion test_scrl_decomp test_scrl_tilecache)
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} mbcore)
    add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endforeach()
set_tests_properties(test_runway_phase PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_runway_sfx PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_menu_phase PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_stage_catalog PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_stage_runner PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_stage_progress PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_sidescrl_phase PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_scrl_map PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_special_phase PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_duck_avi PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
set_tests_properties(test_rhythm_enemies PROPERTIES
    ENVIRONMENT "MB_DISC1=${CMAKE_SOURCE_DIR}/disco1/Mr. Bones (USA) (Disc 1).cue;MB_DISC2=${CMAKE_SOURCE_DIR}/disco2/Mr. Bones (USA) (Disc 2).cue;MB_MANIFEST=${CMAKE_SOURCE_DIR}/manifest/versions.txt")
