# The fuzzers: libFuzzer through MSVC, with AddressSanitizer, over a reader of
# what arrives from outside (the server's datagrams, a router's answers, the
# game's session, a mod's mod.ini and the load order); not in Debug (MSVC's AddressSanitizer does not combine with
# its run-time checks). Each replays its kept inputs (port/fuzz/<name>/)
# and fuzzes 15 s in the test suite; port/tools/fuzz.py runs them longer.
set(KHDAYS_FUZZ_SECONDS 15)
function(khdays_fuzz_target name)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        return()
    endif()
    add_executable(khdays-fuzz-${name} ${ARGN})
    # /DEBUG: a program database beside it, for the reports' lines
    if(MSVC)
        target_compile_options(khdays-fuzz-${name} PRIVATE /W4 /std:c17 /fsanitize=address /fsanitize=fuzzer)
        target_link_options(khdays-fuzz-${name} PRIVATE /DEBUG /INCREMENTAL:NO)
    else()
        target_compile_options(khdays-fuzz-${name} PRIVATE -fsanitize=address,fuzzer -fno-omit-frame-pointer)
        target_link_options(khdays-fuzz-${name} PRIVATE -fsanitize=address,fuzzer)
    endif()
    # libFuzzer writes what it finds to the first directory: the tree's, so
    # the kept inputs (the second) change only through port/tools/fuzz.py
    set(corpus "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/${name}")
    set(found "${CMAKE_BINARY_DIR}/fuzz-${name}-corpus")
    file(MAKE_DIRECTORY "${found}")
    add_test(NAME khdays-fuzz-${name}
             COMMAND khdays-fuzz-${name} -max_total_time=${KHDAYS_FUZZ_SECONDS} -timeout=10
                     "-artifact_prefix=${CMAKE_BINARY_DIR}/fuzz-${name}-" "${found}" "${corpus}")
endfunction()

khdays_fuzz_target(server fuzz/fuzz_server.c server/server_core.c host/net/net_stub.c)
khdays_fuzz_target(portmap fuzz/fuzz_portmap.c server/portmap.c host/net/net_stub.c)
if(WIN32)
    set(fuzz_instance host/app/instance_win.c)
else()
    set(fuzz_instance host/app/instance_posix.c)
endif()
khdays_fuzz_target(session fuzz/fuzz_session.c host/net/net_stub.c runtime/core/session_log.c ${fuzz_instance}
                   runtime/core/sha1.c)
if(TARGET khdays-fuzz-session)
    target_link_libraries(khdays-fuzz-session PRIVATE SDL3::SDL3)
endif()
khdays_fuzz_target(mods fuzz/fuzz_mods.c runtime/scripts/manifest.c runtime/scripts/order.c runtime/core/sha256.c)
