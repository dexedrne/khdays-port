add_library(khdays-sound STATIC
    archive.c
    bus.c
    categories.c
    channels.c
    mix_ds.c
    mix_pc.c
    render.c
    replace.c
    snd7_alarm.c
    snd7_bank.c
    snd7_channel.c
    snd7_command.c
    snd7_driver.c
    snd7_hw.c
    snd7_os.c
    snd7_seq.c
    snd7_tables.c
    stream_reader.c
    third_party_decoders.c)
target_include_directories(khdays-sound PRIVATE "${stb_SOURCE_DIR}" "${dr_libs_SOURCE_DIR}")
