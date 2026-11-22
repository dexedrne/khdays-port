set(runtime "${CMAKE_CURRENT_LIST_DIR}/../runtime")
add_executable(khdays-script-test "${CMAKE_CURRENT_LIST_DIR}/script_test.c" ${runtime}/scripts/scripts.c ${runtime}/scripts/sandbox.c
               ${runtime}/scripts/manifest.c ${runtime}/scripts/order.c ${runtime}/scripts/folders.cpp ${runtime}/mods/list.c
               ${runtime}/scripts/mods_log.c ${runtime}/scripts/queue.c ${runtime}/scripts/api_events.c
               ${runtime}/scripts/api_mod.c ${runtime}/scripts/api_state.c ${runtime}/scripts/api_menus.c
               ${runtime}/scripts/api_actions.c ${runtime}/scripts/api_v2.c ${runtime}/scripts/api_enemies.c
               ${runtime}/scripts/api_run.c ${runtime}/scripts/api_screens.c ${runtime}/scripts/api_deck.c
               ${runtime}/scripts/api_time.c ${runtime}/scripts/api_memory.c ${runtime}/scripts/screens.c
               ${runtime}/scripts/view.c ${runtime}/core/sha256.c)
target_link_libraries(khdays-script-test PRIVATE khdays-lua SDL3::SDL3)
unset(runtime)
