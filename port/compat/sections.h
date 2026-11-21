#ifndef KHDAYS_SECTIONS_H
#define KHDAYS_SECTIONS_H
/* One packed array per record type, enumerable without running constructors. */
#if defined(_MSC_VER)
#define KHDAYS_DATA_RECORD __declspec(allocate(".khdi$m"))
#define KHDAYS_FUNCTION_RECORD __declspec(allocate(".khfr$m"))
#elif defined(__APPLE__)
#define KHDAYS_DATA_RECORD __attribute__((used, section("__DATA,khdi"), aligned(8)))
#define KHDAYS_FUNCTION_RECORD __attribute__((used, section("__DATA,khfr"), aligned(8)))
#define KHDAYS_SECTION_START(name) __asm("section$start$__DATA$" #name)
#define KHDAYS_SECTION_END(name) __asm("section$end$__DATA$" #name)
#else
#define KHDAYS_DATA_RECORD __attribute__((used, section("khdi"), aligned(8)))
#define KHDAYS_FUNCTION_RECORD __attribute__((used, section("khfr"), aligned(8)))
#define KHDAYS_SECTION_START(name) __asm("__start_" #name)
#define KHDAYS_SECTION_END(name) __asm("__stop_" #name)
#endif
#endif
