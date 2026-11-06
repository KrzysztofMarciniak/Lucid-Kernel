# LucidC

LucidC is a small host-side cross-compiler for a deliberately limited
C-like language. It emits flat 32-bit x86 images for Lucid's user-mode ABI.
The compiler itself runs on the build host and uses GNU `as` and `ld`; it is
not yet a compiler that runs inside Lucid.

## Build a program

```sh
python3 tools/lucidc.py examples/lucidc/hello.lc -o hello.bin
```

Use `--asm` to emit assembly instead:

```sh
python3 tools/lucidc.py --asm examples/lucidc/hello.lc -o hello.s
```

The resulting flat image is linked at `0x01000000`, Lucid's current user-image
base. Its entry stub calls `main()` and exits through syscall 1. `print` writes
string literals through syscall 0. Images are limited to 64 KiB, matching the
current embedded-image build limit.

Lucid currently executes only images registered in its user-image table.
Producing a `.bin` file does not yet make it directly executable from Clearsh;
the image must be added to the registry and build process. A general executable
loader is a separate step.

## Language subset

- Functions, parameters, and local variables all use signed 32-bit integers.
- Functions have the form `int name(int argument, ...) { ... }`.
- `int main()` is required and must take no arguments.
- Local variables require an initializer.
- Statements: blocks, `return`, `if`/`else`, `while`, assignments, and calls.
- Expressions: integer arithmetic, comparisons, `&&`, `||`, unary `-` and `!`,
  variables, and function calls.
- `print("...")` accepts one string literal; escapes include `\n`, `\r`, `\t`,
  `\0`, `\\`, and `\"`.

Pointers, arrays, structs, globals, preprocessing, imports, and a standard
library are not implemented. Integer division by zero is a runtime fault.

Run the focused compiler tests with:

```sh
python3 -m unittest discover -s tests -p 'test_lucidc.py'
```
