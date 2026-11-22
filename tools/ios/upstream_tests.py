#!/usr/bin/env python3
"""upstream_tests.py PORT_CMAKELISTS > upstream_tests.cmake

The port's CMakeLists.txt lists most of its tests for Windows alone: the other platforms' builds stop
before them (the game's own tests and tools/ios have theirs). This writes those same statements as a
CMake file a Linux build of the port includes (-DKHDAYS_UPSTREAM_TESTS=ON), so that upstream's tests run
against the merged tree without a second list to keep:

  * the statements inside the "if(WIN32)" blocks that hold tests between the libraries, and everything
    after the "return()" the other platforms stop at, up to the quality tools;
  * less what needs Windows: a target with a Windows source or library (and every statement about it),
    MSVC's own options, and tests/hd_fonts.cmake (the Linux build has it already);
  * each program also linked to libm, and all of them under one target, khdays-upstream-tests.

What was left out is listed in comments at the end of the output.
"""
import re
import sys

WINDOWS = re.compile(r"_win\.c\b|context_x64\.asm|\bws2_32\b|\bwinmm\b|\bdwmapi\b|\biphlpapi\b|\badvapi32\b|"
                     r"\bdbghelp\b|\.dll\b|\.rc\b|/DELAYLOAD|\bmfplat\b|\bwinhttp\b")
MSVC_FLAG = re.compile(r"(?:^|[\s:\"])/(?:W[0-4X]|std:|EH|utf-8|permissive|wd\d|Zp|O[12dx]|MT|MD|bigobj|Zc:)")
OPENERS = {"if": "endif", "foreach": "endforeach", "while": "endwhile", "function": "endfunction", "macro": "endmacro"}
END = "# ---- The quality tools"
ALREADY = ("tests/hd_fonts.cmake",)  # included by the Linux build itself


def statements(text):
    """The file's commands in order: (name, arguments' text, the whole text with what stood before it)."""
    out, i, n = [], 0, len(text)
    while i < n:
        m = re.compile(r"[A-Za-z_][A-Za-z0-9_]*[ \t]*\(").search(text, i)
        # comments and blank lines before the command: passed over (a '(' in a comment is not a command)
        j = i
        lead_end = None
        while j < n:
            line_end = text.find("\n", j)
            line_end = n if line_end < 0 else line_end
            line = text[j:line_end].strip()
            if line == "" or line.startswith("#"):
                j = line_end + 1
                continue
            lead_end = j
            break
        if lead_end is None:
            break
        m = re.compile(r"[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*\(").match(text, lead_end)
        if not m:
            raise SystemExit(f"upstream_tests.py: not a command at offset {lead_end}: {text[lead_end:lead_end + 60]!r}")
        k, depth, quote = m.end(), 1, False
        while k < n and depth:
            c = text[k]
            if quote:
                if c == "\\":
                    k += 1
                elif c == '"':
                    quote = False
            elif c == '"':
                quote = True
            elif c == "#":
                k = text.find("\n", k)
                k = n if k < 0 else k
                continue
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            k += 1
        out.append((m.group(1).lower(), text[m.end():k - 1], text[lead_end:k]))
        i = k
    return out


def units(stmts):
    """Statements grouped: a plain command, or a block (its opening, the units inside, its closing)."""
    out, i = [], 0

    def take(i):
        name, args, whole = stmts[i]
        if name in OPENERS:
            body, i2 = [], i + 1
            while stmts[i2][0] != OPENERS[name]:
                unit, i2 = take(i2)
                body.append(unit)
            return ("block", stmts[i], body, stmts[i2]), i2 + 1
        return ("command", stmts[i]), i + 1

    while i < len(stmts):
        unit, i = take(i)
        out.append(unit)
    return out


def first_word(args):
    m = re.match(r"\s*(\"[^\"]*\"|[^\s()]+)", args)
    return m.group(1).strip('"') if m else ""


class Writer:
    def __init__(self):
        self.lines, self.dropped, self.left, self.programs = [], set(), [], []

    def leave(self, why, whole):
        self.left.append(f"{why}: {' '.join(whole.split())[:150]}")

    def command(self, stmt, indent):
        name, args, whole = stmt
        target = first_word(args)
        if name == "add_test":
            m = re.search(r"\bCOMMAND\s+(\"[^\"]*\"|\S+)", args)
            target = m.group(1).strip('"') if m else ""
        if name in ("add_executable", "add_library") and WINDOWS.search(whole):
            self.dropped.add(target)
            return self.leave("needs Windows", whole)
        if target in self.dropped or any(f"TARGET_FILE:{d}>" in whole for d in self.dropped):
            return self.leave("its program needs Windows", whole)
        if name == "add_dependencies" and any(re.search(rf"(?<![\w-]){re.escape(d)}(?![\w-])", args) for d in self.dropped):
            return self.leave("depends on a program that needs Windows", whole)
        if name == "target_link_libraries" and WINDOWS.search(args):
            # Windows' libraries among the others: linked without them
            kept = " ".join(word for word in args.split() if not WINDOWS.fullmatch(word))
            self.lines.append(f"{indent}target_link_libraries({kept})")
            return None
        if name in ("target_compile_options", "set_source_files_properties") and MSVC_FLAG.search(args):
            return None  # MSVC's options alone: nothing to say
        if WINDOWS.search(whole):
            return self.leave("needs Windows", whole)
        if name == "include" and any(a in args for a in ALREADY):
            return None
        if name in ("return", "option") and "KHDAYS_ASAN" in args:
            return None
        self.lines.append(indent + " ".join(whole.split("\n")[0:1]) if "\n" not in whole else
                          "\n".join(indent + line for line in whole.split("\n")))
        if name == "add_executable":
            self.lines.append(f"{indent}target_link_libraries({target} PRIVATE m)")
            self.lines.append(f"{indent}add_dependencies(khdays-upstream-tests {target})")
        elif name == "add_library" and re.search(r"\bMODULE\b", args):
            self.lines.append(f"{indent}add_dependencies(khdays-upstream-tests {target})")

    def unit(self, unit, indent=""):
        if unit[0] == "command":
            return self.command(unit[1], indent)
        _, opening, body, closing = unit
        if WINDOWS.search(opening[2]):
            return self.leave("needs Windows", opening[2])
        self.lines.append(indent + opening[2].strip())
        for inner in body:
            if inner[0] == "command" and inner[1][0] in ("else", "elseif"):
                self.lines.append(indent + inner[1][2].strip())
            else:
                self.unit(inner, indent + "    ")
        self.lines.append(indent + closing[2].strip())


def main():
    text = open(sys.argv[1], encoding="utf-8").read()
    stop = text.find(END)
    if stop < 0:
        raise SystemExit(f"upstream_tests.py: {END!r} is not in {sys.argv[1]}")
    stopped = text.find("return() # host tests have a standalone build in tools/ios")
    if stopped < 0:
        raise SystemExit("upstream_tests.py: the other platforms' return() is not where it was")
    after = text.find("endif()", stopped) + len("endif()")
    w = Writer()
    w.lines.append("# Generated by tools/ios/upstream_tests.py from port/CMakeLists.txt: upstream's tests listed for\n"
                   "# Windows that need none of it. Not edited by hand.\n"
                   "add_custom_target(khdays-upstream-tests)")
    # the tests between the libraries: the blocks "if(WIN32)" that open on a test or a tool
    for unit in units(statements(text[:text.rfind("\nif(NOT WIN32)", 0, stopped)])):
        if unit[0] == "block" and unit[1][0] == "if" and unit[1][1].strip() == "WIN32":
            if any(inner[0] == "command" and inner[1][0] == "add_test" for inner in unit[2]):
                for inner in unit[2]:
                    w.unit(inner)
    for unit in units(statements(text[after:stop])):
        w.unit(unit)
    print("\n".join(w.lines))
    print("\n# Left out:")
    for line in w.left:
        print("#   " + line)


if __name__ == "__main__":
    main()
