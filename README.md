# wlp4-compiler

A **WLP4** compiler implemented as a classic multi-pass pipeline in **C++17**: lexical analysis, parsing, semantic analysis, and ARM64 code generation. WLP4 is a small teaching language (C++-like subset with `long` / `long*`, procedures, and `wain` as entry point). This repository ships the compiler sources, specification excerpts, and a regression harness.

## Architecture

| Stage       | Binary      | Role |
|------------|-------------|------|
| Scan       | `wlp4scan`  | Source → token stream |
| Parse      | `wlp4parse` | Tokens → `.wlp4i` preorder tree |
| Type check | `wlp4type`  | `.wlp4i` → `.wlp4ti` (annotated tree) |
| Codegen    | `wlp4gen`   | `.wlp4ti` → ARM64 assembly |

Typical shell pipeline:

```bash
wlp4scan < program.wlp4 | wlp4parse | wlp4type | wlp4gen > program.asm
```

Assemble and link the emitted `.asm` with your environment’s ARM64 toolchain (e.g. `linkasm`), then run under an ARM64 emulator as required by your setup.

## Build

Requires **g++** with **C++17**.

```bash
./build-toolchain.sh
```

Produces `bin/wlp4scan`, `bin/wlp4parse`, `bin/wlp4type`, and `bin/wlp4gen`.

To prefer these binaries in your current shell:

```bash
source ./use-local-tools.sh
```

## Documentation

| Path | Contents |
|------|----------|
| `docs/wlp4.txt` | Language definition (lexical, grammar, semantics, behaviour) |
| `docs/wlp4i.txt`, `docs/wlp4ti.txt` | Intermediate text formats |
| `docs/bin.txt` | Tool invocations, external `wlp4c` / emulator notes |
| `docs/cfg.txt`, `docs/armcom.txt`, `docs/arm64_ref_sheet.md` | Parsing / object / ISA reference as applicable |

## Tests

```bash
bash scripts/run-tests.sh
```

The script exercises the pipeline against programs under `test/`. The corpus is organized as **`*.wlp4`** / **`*.wlp4ti`** in category subdirectories (`procedures/`, `pointers/`, …) plus **`test/expected/`** (reference captures for optional tooling; the harness only runs WLP4 cases). It expects **built** tools in `bin/` plus **copies** of the host toolchain there—at minimum `linkasm`, `arm64emu`, `wlp4c`, and `linker-striparmcom`—so names resolve from `bin/` only. Optional: `alloc.com` / `print.com` for heap (`ALLOC_COM`, `PRINT_COM`). See `scripts/run-tests.sh` for environment variables.

## Repository layout

```
src/           # wlp4scan.cc, wlp4parse.cc, wlp4type.cc, wlp4gen.cc
bin/           # build output (gitignored if configured)
docs/          # language and tool documentation
scripts/       # run-tests.sh, count_instructions.py
test/          # *.wlp4 / *.wlp4ti by category; expected/ for goldens
```

## Contributing

Issues and pull requests are welcome. Please keep changes focused; match existing style in `src/` and run `scripts/run-tests.sh` when you touch the compiler.

## Acknowledgements

WLP4 and related tooling names follow **University of Waterloo CS 241**–style conventions; this project is maintained independently for learning and experimentation.
