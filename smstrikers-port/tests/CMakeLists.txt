# The port's own tests: small, no Aurora, no game data, so they run in every configuration.
add_executable(chunk_bounds_test
    chunk_bounds_test.c
    ../src/platform/cam_endian.c
    ../src/platform/skin_endian.c
    ../src/platform/wld_endian.c
    ../src/platform/bmd_endian.c)
target_include_directories(chunk_bounds_test PRIVATE ../include)
add_test(NAME chunk_bounds COMMAND chunk_bounds_test)

add_executable(prompts_resolve_test
    prompts_resolve_test.cpp
    ../src/platform/prompts_resolve.cpp)
target_include_directories(prompts_resolve_test PRIVATE ../include)
add_test(NAME prompts_resolve COMMAND prompts_resolve_test)

# The two tool tests, pure Python, so they are one `ctest` away.
add_test(NAME tool_genstubs
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_genstubs.py")
add_test(NAME tool_extract_disc
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_extract_disc.py")
add_test(NAME tool_vita_cpu_trace
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_analyze_vita_cpu_trace.py")
add_test(NAME tool_vita_frames
         COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_analyze_vita_frames.py")

if(UNIX)
    add_executable(cpu_trace_hotpaths_test
        cpu_trace_hotpaths_test.cpp
        ../src/NL/plat/platvmath.cpp
        ../src/Dolphin/mtx/mtx44.c
        ../src/Dolphin/mtx/vec.c)
    set_target_properties(cpu_trace_hotpaths_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(cpu_trace_hotpaths_test PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)
    target_link_libraries(cpu_trace_hotpaths_test PRIVATE port_flags m)
    add_test(NAME cpu_trace_hotpaths COMMAND cpu_trace_hotpaths_test)
    add_executable(diagnostics_config_test diagnostics_config_test.c ../src/platform/config.c)
    target_include_directories(diagnostics_config_test PRIVATE ../include)
    add_test(NAME diagnostics_config COMMAND diagnostics_config_test)
    add_executable(vita_default_config_test vita_default_config_test.c ../src/platform/config.c)
    target_include_directories(vita_default_config_test PRIVATE ../include)
    target_compile_definitions(vita_default_config_test PRIVATE PORT_VITA=1)
    add_test(NAME vita_default_config COMMAND vita_default_config_test)
    add_executable(benchmark_fps_test benchmark_fps_test.c ../src/platform/benchmark.c)
    target_include_directories(benchmark_fps_test PRIVATE ../include)
    target_link_libraries(benchmark_fps_test PRIVATE m)
    add_test(NAME benchmark_fps COMMAND benchmark_fps_test)
    add_executable(frame_capture_test frame_capture_test.c ../src/platform/benchmark.c)
    target_include_directories(frame_capture_test PRIVATE ../include)
    add_test(NAME frame_capture COMMAND frame_capture_test)
    find_package(Threads REQUIRED)
    add_executable(skin_matrix_packets_test
        skin_matrix_packets_test.cpp
        ../src/Dolphin/mtx/mtx.c
        ../src/Dolphin/mtx/vec.c)
    set_target_properties(skin_matrix_packets_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_compile_options(skin_matrix_packets_test PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>)
    target_link_libraries(skin_matrix_packets_test PRIVATE port_flags Threads::Threads m)
    add_test(NAME skin_matrix_packets COMMAND skin_matrix_packets_test)
    add_executable(profile_output_test profile_output_test.cpp)
    target_include_directories(profile_output_test PRIVATE ../include)
    add_test(NAME profile_output COMMAND profile_output_test)
    add_executable(switch_dvd_test switch_dvd_test.c)
    target_include_directories(switch_dvd_test PRIVATE ../include)
    add_test(NAME switch_dvd COMMAND switch_dvd_test)
    add_executable(vita_dvd_test vita_dvd_test.c)
    target_include_directories(vita_dvd_test PRIVATE ../include)
    add_test(NAME vita_dvd COMMAND vita_dvd_test)
    add_executable(vita_pad_test vita_pad_test.c)
    target_include_directories(vita_pad_test PRIVATE ../include)
    target_link_libraries(vita_pad_test PRIVATE m)
    add_test(NAME vita_pad COMMAND vita_pad_test)
    add_test(NAME tool_switch_rebuild
             COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_switch_rebuild.py")
    add_test(NAME tool_vita_delay_contract
             COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/vita_delay_contract_test.py")
endif()
