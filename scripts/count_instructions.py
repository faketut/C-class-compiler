#!/usr/bin/env python3
import argparse
import re
import sys


INSTR_RE = re.compile(r"^\s{2}([A-Za-z.][A-Za-z0-9.]*)\b")


def is_counted_instruction(line: str) -> bool:
    # Count codegen instructions, not labels/directives; matched lines start with two spaces.
    m = INSTR_RE.match(line)
    return bool(m) and not m.group(1).startswith(".")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Count static ARM64 instruction lines in generated asm."
    )
    ap.add_argument("path", help="Path to assembly file (or - for stdin)")
    args = ap.parse_args()

    if args.path == "-":
        lines = sys.stdin.read().splitlines(keepends=True)
    else:
        try:
            with open(args.path, "r", encoding="utf-8") as f:
                lines = f.readlines()
        except OSError as e:
            print(f"count_instructions: {args.path}: {e}", file=sys.stderr)
            return 1

    count = 0
    for ln in lines:
        if is_counted_instruction(ln):
            count += 1

    print(count)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

