#!/usr/bin/env python3
"""Compile a small C-like language into a flat 32-bit Lucid user image."""

import argparse
from dataclasses import dataclass, field
from pathlib import Path
import subprocess
import sys
import tempfile


USER_IMAGE_LIMIT = 64 * 1024
LINKER_SCRIPT = Path(__file__).with_name("lucidc.ld")


class CompileError(Exception):
    pass


@dataclass
class Token:
    kind: str
    value: object
    line: int
    column: int


@dataclass
class Node:
    kind: str
    value: object = None
    children: list = field(default_factory=list)


@dataclass(frozen=True)
class Binding:
    kind: str
    index: int
    name: str


@dataclass
class Function:
    name: str
    parameters: list
    body: Node
    local_count: int


class Lexer:
    def __init__(self, source):
        self.source = source
        self.position = 0
        self.line = 1
        self.column = 1

    def _peek(self, offset=0):
        position = self.position + offset
        return self.source[position] if position < len(self.source) else ""

    def _take(self):
        character = self._peek()
        if character:
            self.position += 1
            if character == "\n":
                self.line += 1
                self.column = 1
            else:
                self.column += 1
        return character

    def _error(self, message, line=None, column=None):
        line = self.line if line is None else line
        column = self.column if column is None else column
        raise CompileError(f"{line}:{column}: {message}")

    def tokens(self):
        result = []
        two_character_tokens = {
            "==", "!=", "<=", ">=", "&&", "||"
        }
        single_character_tokens = set("(){};,=+-*/%!<>")

        while self._peek():
            character = self._peek()
            if character.isspace():
                self._take()
                continue

            if character == "/" and self._peek(1) == "/":
                while self._peek() and self._take() != "\n":
                    pass
                continue

            if character == "/" and self._peek(1) == "*":
                start_line, start_column = self.line, self.column
                self._take()
                self._take()
                while self._peek() and not (
                    self._peek() == "*" and self._peek(1) == "/"
                ):
                    self._take()
                if not self._peek():
                    self._error(
                        "unterminated block comment", start_line, start_column
                    )
                self._take()
                self._take()
                continue

            start_line, start_column = self.line, self.column

            if character.isalpha() or character == "_":
                value = ""
                while self._peek().isalnum() or self._peek() == "_":
                    value += self._take()
                result.append(Token("identifier", value, start_line, start_column))
                continue

            if character.isdigit():
                value = ""
                if character == "0" and self._peek(1) in ("x", "X"):
                    value += self._take()
                    value += self._take()
                    digits = ""
                    while self._peek() and self._peek() in "0123456789abcdefABCDEF":
                        digits += self._take()
                    if not digits:
                        self._error(
                            "expected hexadecimal digits",
                            start_line,
                            start_column,
                        )
                    value += digits
                else:
                    while self._peek().isdigit():
                        value += self._take()
                result.append(Token("number", value, start_line, start_column))
                continue

            if character == '"':
                self._take()
                value = bytearray()
                while self._peek() and self._peek() != '"':
                    current = self._take()
                    if current in "\r\n":
                        self._error(
                            "newline in string literal",
                            start_line,
                            start_column,
                        )
                    if current == "\\":
                        escaped = self._take()
                        escape_map = {
                            "n": b"\n",
                            "r": b"\r",
                            "t": b"\t",
                            "0": b"\0",
                            "\\": b"\\",
                            '"': b'"',
                        }
                        if escaped not in escape_map:
                            self._error(
                                f"unsupported escape \\{escaped}",
                                self.line,
                                self.column,
                            )
                        value.extend(escape_map[escaped])
                    else:
                        value.extend(current.encode("utf-8"))
                if self._peek() != '"':
                    self._error(
                        "unterminated string literal",
                        start_line,
                        start_column,
                    )
                self._take()
                result.append(Token("string", bytes(value), start_line, start_column))
                continue

            pair = character + self._peek(1)
            if pair in two_character_tokens:
                self._take()
                self._take()
                result.append(Token("symbol", pair, start_line, start_column))
                continue

            if character in single_character_tokens:
                self._take()
                result.append(Token("symbol", character, start_line, start_column))
                continue

            self._error(
                f"unexpected character {character!r}", start_line, start_column
            )

        result.append(Token("eof", "", self.line, self.column))
        return result


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0
        self.scopes = []
        self.local_count = 0

    def _current(self):
        return self.tokens[self.position]

    def _at(self, value):
        return self._current().value == value

    def _take(self):
        token = self._current()
        self.position += 1
        return token

    def _error(self, message, token=None):
        token = self._current() if token is None else token
        raise CompileError(f"{token.line}:{token.column}: {message}")

    def _expect(self, value):
        if not self._at(value):
            self._error(f"expected {value!r}")
        return self._take()

    def _expect_identifier(self):
        if self._current().kind != "identifier":
            self._error("expected an identifier")
        return self._take()

    def _lookup(self, name):
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        self._error(f"unknown variable {name!r}")

    def parse(self):
        functions = []
        names = set()
        while self._current().kind != "eof":
            function = self._parse_function()
            if function.name in names:
                self._error(f"duplicate function {function.name!r}")
            names.add(function.name)
            functions.append(function)

        main = next((function for function in functions
                     if function.name == "main"), None)
        if main is None:
            self._error("program must define int main()")
        if main.parameters:
            self._error("main must not take parameters")
        return functions

    def _parse_function(self):
        self._expect("int")
        name = self._expect_identifier().value
        if name in ("_start", "__lucid_print", "print"):
            self._error(f"reserved function name {name!r}")
        self._expect("(")

        self.scopes = [{}]
        self.local_count = 0
        parameters = []
        if not self._at(")"):
            while True:
                self._expect("int")
                parameter = self._expect_identifier()
                if parameter.value in self.scopes[0]:
                    self._error(
                        f"duplicate parameter {parameter.value!r}", parameter
                    )
                binding = Binding("parameter", len(parameters), parameter.value)
                self.scopes[0][parameter.value] = binding
                parameters.append(binding)
                if not self._at(","):
                    break
                self._take()
        self._expect(")")
        body = self._parse_block(create_scope=False)
        function = Function(name, parameters, body, self.local_count)
        self.scopes = []
        return function

    def _parse_block(self, create_scope=True):
        self._expect("{")
        if create_scope:
            self.scopes.append({})
        statements = []
        while not self._at("}"):
            if self._current().kind == "eof":
                self._error("unterminated block")
            statements.append(self._parse_statement())
        self._take()
        if create_scope:
            self.scopes.pop()
        return Node("block", children=statements)

    def _parse_control_body(self):
        self.scopes.append({})
        statement = self._parse_statement()
        self.scopes.pop()
        return statement

    def _parse_statement(self):
        if self._at("{"):
            return self._parse_block()

        if self._at("int"):
            self._take()
            name = self._expect_identifier()
            scope = self.scopes[-1]
            if name.value in scope:
                self._error(f"duplicate local {name.value!r}", name)
            binding = Binding("local", self.local_count, name.value)
            self.local_count += 1
            scope[name.value] = binding
            self._expect("=")
            initializer = self._parse_expression()
            self._expect(";")
            return Node("declaration", binding, [initializer])

        if self._at("return"):
            self._take()
            result = self._parse_expression()
            self._expect(";")
            return Node("return", children=[result])

        if self._at("if"):
            self._take()
            self._expect("(")
            condition = self._parse_expression()
            self._expect(")")
            body = self._parse_control_body()
            alternate = None
            if self._at("else"):
                self._take()
                alternate = self._parse_control_body()
            return Node("if", children=[condition, body, alternate])

        if self._at("while"):
            self._take()
            self._expect("(")
            condition = self._parse_expression()
            self._expect(")")
            body = self._parse_control_body()
            return Node("while", children=[condition, body])

        expression = self._parse_expression()
        self._expect(";")
        return Node("expression", children=[expression])

    def _parse_expression(self):
        return self._parse_assignment()

    def _parse_assignment(self):
        expression = self._parse_or()
        if self._at("="):
            operator = self._take()
            if expression.kind != "variable":
                self._error("left side of assignment must be a variable", operator)
            value = self._parse_assignment()
            return Node("assignment", expression.value, [value])
        return expression

    def _parse_binary(self, next_parser, operators):
        expression = next_parser()
        while self._current().value in operators:
            operator = self._take().value
            right = next_parser()
            expression = Node("binary", operator, [expression, right])
        return expression

    def _parse_or(self):
        return self._parse_binary(self._parse_and, {"||"})

    def _parse_and(self):
        return self._parse_binary(self._parse_equality, {"&&"})

    def _parse_equality(self):
        return self._parse_binary(self._parse_comparison, {"==", "!="})

    def _parse_comparison(self):
        return self._parse_binary(
            self._parse_addition, {"<", "<=", ">", ">="}
        )

    def _parse_addition(self):
        return self._parse_binary(self._parse_multiplication, {"+", "-"})

    def _parse_multiplication(self):
        return self._parse_binary(self._parse_unary, {"*", "/", "%"})

    def _parse_unary(self):
        if self._current().value in ("-", "!"):
            operator = self._take().value
            return Node("unary", operator, [self._parse_unary()])
        return self._parse_primary()

    def _parse_primary(self):
        token = self._current()
        if token.kind == "number":
            self._take()
            value = int(token.value, 16 if token.value.lower().startswith("0x") else 10)
            if value > 0x7FFFFFFF:
                self._error("integer literal exceeds signed 32-bit range", token)
            return Node("integer", value)

        if token.kind == "string":
            self._take()
            return Node("string", token.value)

        if token.kind == "identifier":
            name = self._take().value
            if self._at("("):
                self._take()
                arguments = []
                if not self._at(")"):
                    while True:
                        arguments.append(self._parse_expression())
                        if not self._at(","):
                            break
                        self._take()
                self._expect(")")
                return Node("call", name, arguments)
            return Node("variable", self._lookup(name))

        if self._at("("):
            self._take()
            expression = self._parse_expression()
            self._expect(")")
            return expression

        self._error("expected an expression", token)


def _expression_type(expression, functions):
    kind = expression.kind
    if kind == "integer" or kind == "variable":
        return "int"
    if kind == "string":
        return "string"
    if kind == "assignment":
        if _expression_type(expression.children[0], functions) != "int":
            raise CompileError("assignment requires an integer value")
        return "int"
    if kind == "unary":
        if _expression_type(expression.children[0], functions) != "int":
            raise CompileError(f"operator {expression.value!r} requires an integer")
        return "int"
    if kind == "binary":
        if any(_expression_type(child, functions) != "int"
               for child in expression.children):
            raise CompileError(f"operator {expression.value!r} requires integers")
        return "int"
    if kind == "call":
        if expression.value == "print":
            if len(expression.children) != 1 or \
                    expression.children[0].kind != "string":
                raise CompileError("print expects one string literal")
            return "int"
        function = functions.get(expression.value)
        if function is None:
            raise CompileError(f"unknown function {expression.value!r}")
        if len(expression.children) != len(function.parameters):
            raise CompileError(
                f"{expression.value} expects {len(function.parameters)} "
                f"arguments, got {len(expression.children)}"
            )
        for argument in expression.children:
            if _expression_type(argument, functions) != "int":
                raise CompileError("function arguments must be integers")
        return "int"
    raise CompileError(f"unsupported expression {kind!r}")


def _check_statement(statement, functions):
    kind = statement.kind
    if kind == "block":
        for child in statement.children:
            _check_statement(child, functions)
    elif kind == "declaration":
        if _expression_type(statement.children[0], functions) != "int":
            raise CompileError("local variables require integer initializers")
    elif kind == "return":
        if _expression_type(statement.children[0], functions) != "int":
            raise CompileError("functions must return an integer")
    elif kind == "expression":
        _expression_type(statement.children[0], functions)
    elif kind == "if" or kind == "while":
        if _expression_type(statement.children[0], functions) != "int":
            raise CompileError("condition must be an integer")
        _check_statement(statement.children[1], functions)
        if kind == "if" and statement.children[2] is not None:
            _check_statement(statement.children[2], functions)
    else:
        raise CompileError(f"unsupported statement {kind!r}")


class CodeGenerator:
    def __init__(self, functions):
        self.functions = functions
        self.lines = []
        self.strings = {}
        self.label_number = 0
        self.function_end = None

    def _emit(self, line):
        self.lines.append(line)

    def _new_label(self, prefix):
        label = f".L_lucid_{prefix}_{self.label_number}"
        self.label_number += 1
        return label

    def _string_label(self, value):
        if value not in self.strings:
            self.strings[value] = f".L_lucid_string_{len(self.strings)}"
        return self.strings[value]

    def _variable_address(self, binding):
        if binding.kind == "local":
            return f"[ebp - {4 * (binding.index + 1)}]"
        return f"[ebp + {8 + 4 * binding.index}]"

    def generate(self):
        self._emit(".intel_syntax noprefix")
        self._emit(".code32")
        self._emit(".text")
        self._emit(".globl _start")
        self._emit(".type _start, @function")
        self._emit("_start:")
        self._emit("    call main")
        self._emit("    mov ebx, eax")
        self._emit("    mov eax, 1")
        self._emit("    int 0x80")
        self._emit("1:")
        self._emit("    jmp 1b")
        self._emit(".size _start, .-_start")
        self._emit("")
        self._emit(".type __lucid_print, @function")
        self._emit("__lucid_print:")
        self._emit("    push ebp")
        self._emit("    mov ebp, esp")
        self._emit("    push ebx")
        self._emit("    push ecx")
        self._emit("    mov ebx, DWORD PTR [ebp + 8]")
        self._emit("    xor ecx, ecx")
        self._emit(".L_lucid_print_length:")
        self._emit("    cmp BYTE PTR [ebx + ecx], 0")
        self._emit("    je .L_lucid_print_write")
        self._emit("    inc ecx")
        self._emit("    jmp .L_lucid_print_length")
        self._emit(".L_lucid_print_write:")
        self._emit("    mov eax, 0")
        self._emit("    int 0x80")
        self._emit("    pop ecx")
        self._emit("    pop ebx")
        self._emit("    mov esp, ebp")
        self._emit("    pop ebp")
        self._emit("    ret")

        for function in self.functions:
            self._generate_function(function)

        if self.strings:
            self._emit("")
            self._emit(".section .rodata")
            for value, label in self.strings.items():
                encoded = list(value) + [0]
                self._emit(f"{label}:")
                for offset in range(0, len(encoded), 16):
                    chunk = ", ".join(
                        str(byte) for byte in encoded[offset:offset + 16]
                    )
                    self._emit(f"    .byte {chunk}")

        return "\n".join(self.lines) + "\n"

    def _generate_function(self, function):
        self.function_end = self._new_label("return")
        self._emit("")
        self._emit(f".globl {function.name}")
        self._emit(f".type {function.name}, @function")
        self._emit(f"{function.name}:")
        self._emit("    push ebp")
        self._emit("    mov ebp, esp")
        if function.local_count:
            self._emit(f"    sub esp, {function.local_count * 4}")
        self._generate_statement(function.body)
        self._emit("    xor eax, eax")
        self._emit(f"{self.function_end}:")
        self._emit("    leave")
        self._emit("    ret")
        self._emit(f".size {function.name}, .-{function.name}")

    def _generate_statement(self, statement):
        kind = statement.kind
        if kind == "block":
            for child in statement.children:
                self._generate_statement(child)
        elif kind == "declaration":
            self._generate_expression(statement.children[0])
            self._emit(
                f"    mov DWORD PTR {self._variable_address(statement.value)}, eax"
            )
        elif kind == "return":
            self._generate_expression(statement.children[0])
            self._emit(f"    jmp {self.function_end}")
        elif kind == "expression":
            self._generate_expression(statement.children[0])
        elif kind == "if":
            alternate_label = self._new_label("else")
            end_label = self._new_label("endif")
            self._generate_expression(statement.children[0])
            self._emit("    test eax, eax")
            target = (
                alternate_label
                if statement.children[2] is not None
                else end_label
            )
            self._emit(f"    je {target}")
            self._generate_statement(statement.children[1])
            if statement.children[2] is not None:
                self._emit(f"    jmp {end_label}")
                self._emit(f"{alternate_label}:")
                self._generate_statement(statement.children[2])
            self._emit(f"{end_label}:")
        elif kind == "while":
            start_label = self._new_label("while")
            end_label = self._new_label("endwhile")
            self._emit(f"{start_label}:")
            self._generate_expression(statement.children[0])
            self._emit("    test eax, eax")
            self._emit(f"    je {end_label}")
            self._generate_statement(statement.children[1])
            self._emit(f"    jmp {start_label}")
            self._emit(f"{end_label}:")

    def _generate_expression(self, expression):
        kind = expression.kind
        if kind == "integer":
            self._emit(f"    mov eax, {expression.value}")
        elif kind == "string":
            label = self._string_label(expression.value)
            self._emit(f"    mov eax, OFFSET FLAT:{label}")
        elif kind == "variable":
            self._emit(
                f"    mov eax, DWORD PTR "
                f"{self._variable_address(expression.value)}"
            )
        elif kind == "assignment":
            self._generate_expression(expression.children[0])
            self._emit(
                f"    mov DWORD PTR "
                f"{self._variable_address(expression.value)}, eax"
            )
        elif kind == "unary":
            self._generate_expression(expression.children[0])
            if expression.value == "-":
                self._emit("    neg eax")
            else:
                self._emit("    test eax, eax")
                self._emit("    sete al")
                self._emit("    movzx eax, al")
        elif kind == "binary":
            self._generate_binary(expression)
        elif kind == "call":
            self._generate_call(expression)

    def _generate_binary(self, expression):
        operator = expression.value
        left, right = expression.children
        if operator in ("&&", "||"):
            shortcut = self._new_label("logic_short")
            end = self._new_label("logic_end")
            self._generate_expression(left)
            self._emit("    test eax, eax")
            self._emit(
                f"    {'je' if operator == '&&' else 'jne'} {shortcut}"
            )
            self._generate_expression(right)
            self._emit("    test eax, eax")
            self._emit("    setne al")
            self._emit("    movzx eax, al")
            self._emit(f"    jmp {end}")
            self._emit(f"{shortcut}:")
            if operator == "&&":
                self._emit("    xor eax, eax")
            else:
                self._emit("    mov eax, 1")
            self._emit(f"{end}:")
            return

        self._generate_expression(left)
        self._emit("    push eax")
        self._generate_expression(right)
        self._emit("    mov ecx, eax")
        self._emit("    pop eax")
        if operator == "+":
            self._emit("    add eax, ecx")
        elif operator == "-":
            self._emit("    sub eax, ecx")
        elif operator == "*":
            self._emit("    imul eax, ecx")
        elif operator == "/" or operator == "%":
            self._emit("    cdq")
            self._emit("    idiv ecx")
            if operator == "%":
                self._emit("    mov eax, edx")
        else:
            condition = {
                "==": "sete",
                "!=": "setne",
                "<": "setl",
                "<=": "setle",
                ">": "setg",
                ">=": "setge",
            }[operator]
            self._emit("    cmp eax, ecx")
            self._emit(f"    {condition} al")
            self._emit("    movzx eax, al")

    def _generate_call(self, expression):
        name = expression.value
        arguments = expression.children
        if name == "print":
            self._generate_expression(arguments[0])
            self._emit("    push eax")
            self._emit("    call __lucid_print")
            self._emit("    add esp, 4")
            self._emit("    xor eax, eax")
            return

        for argument in reversed(arguments):
            self._generate_expression(argument)
            self._emit("    push eax")
        self._emit(f"    call {name}")
        if arguments:
            self._emit(f"    add esp, {len(arguments) * 4}")


def compile_source(source):
    functions = Parser(Lexer(source).tokens()).parse()
    function_map = {function.name: function for function in functions}
    for function in functions:
        _check_statement(function.body, function_map)
    return CodeGenerator(functions).generate()


def _run_tool(command, description):
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        raise CompileError(f"could not run {description}: {error}") from error
    if result.returncode:
        message = result.stderr.strip() or result.stdout.strip()
        raise CompileError(f"{description} failed:\n{message}")


def main(argv=None):
    argument_parser = argparse.ArgumentParser(
        description="Compile a small C-like program into a Lucid user image."
    )
    argument_parser.add_argument("source", help="source file")
    argument_parser.add_argument(
        "-o", "--output", help="output flat image (default: source.bin)"
    )
    argument_parser.add_argument(
        "--asm", action="store_true", help="write assembly instead of an image"
    )
    argument_parser.add_argument("--as", dest="assembler", default="as")
    argument_parser.add_argument("--ld", dest="linker", default="ld")
    arguments = argument_parser.parse_args(argv)

    source_path = Path(arguments.source)
    try:
        source = source_path.read_text(encoding="utf-8")
        assembly = compile_source(source)
        if arguments.asm:
            output_path = Path(arguments.output) if arguments.output else None
            if output_path:
                output_path.write_text(assembly, encoding="utf-8")
            else:
                sys.stdout.write(assembly)
            return 0

        output_path = (
            Path(arguments.output)
            if arguments.output
            else source_path.with_suffix(".bin")
        )
        with tempfile.TemporaryDirectory(prefix="lucidc-") as directory:
            temporary = Path(directory)
            assembly_path = temporary / "program.s"
            object_path = temporary / "program.o"
            assembly_path.write_text(assembly, encoding="utf-8")
            _run_tool(
                [arguments.assembler, "--32", "-o", str(object_path),
                 str(assembly_path)],
                "assembler",
            )
            _run_tool(
                [arguments.linker, "-m", "elf_i386", "-T", str(LINKER_SCRIPT),
                 "--oformat", "binary", "-o", str(output_path),
                 str(object_path)],
                "linker",
            )

        image_size = output_path.stat().st_size
        if image_size > USER_IMAGE_LIMIT:
            output_path.unlink()
            raise CompileError(
                f"image is {image_size} bytes; Lucid's embedded-image limit "
                f"is {USER_IMAGE_LIMIT} bytes"
            )
        print(f"wrote {output_path} ({image_size} bytes)")
        return 0
    except (OSError, UnicodeError, CompileError) as error:
        print(f"lucidc: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
