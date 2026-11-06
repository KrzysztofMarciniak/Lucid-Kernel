import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMPILER = ROOT / "tools" / "lucidc.py"


class LucidCompilerTests(unittest.TestCase):
    def test_generates_lucid_syscall_and_control_flow(self):
        source = """
            int twice(int value) {
                return value * 2;
            }
            int half(int value) {
                return value / 2;
            }
            int main() {
                int count = 3;
                while (count > 0) {
                    print("hello\\n");
                    count = count - 1;
                }
                if (twice(count) == 0) {
                    return half(twice(count));
                } else {
                    return 1;
                }
            }
        """
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "sample.lc"
            assembly_path = Path(directory) / "sample.s"
            source_path.write_text(source)
            result = subprocess.run(
                [sys.executable, str(COMPILER), "--asm", "-o",
                 str(assembly_path), str(source_path)],
                capture_output=True,
                text=True,
            )
            assembly = assembly_path.read_text() if result.returncode == 0 else ""
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("int 0x80", assembly)
        self.assertIn("imul eax, ecx", assembly)
        self.assertIn("idiv ecx", assembly)
        self.assertIn(".L_lucid_while_", assembly)
        self.assertIn("call twice", assembly)

    @unittest.skipUnless(shutil.which("as") and shutil.which("ld"),
                         "GNU assembler and linker are required")
    def test_links_flat_lucid_image(self):
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "hello.lc"
            image_path = Path(directory) / "hello.bin"
            source_path.write_text(
                'int main() { print("hello\\n"); return 0; }\n'
            )
            result = subprocess.run(
                [sys.executable, str(COMPILER), str(source_path),
                 "-o", str(image_path)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            image = image_path.read_bytes()
        self.assertGreater(len(image), 0)
        self.assertLessEqual(len(image), 64 * 1024)
        self.assertNotEqual(image[:4], b"\x7fELF")
        self.assertIn(b"hello\n\0", image)
        self.assertEqual(image[0], 0xE8)

    def test_rejects_unknown_variables(self):
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "bad.lc"
            source_path.write_text("int main() { return missing; }\n")
            result = subprocess.run(
                [sys.executable, str(COMPILER), "--asm", str(source_path)],
                capture_output=True,
                text=True,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unknown variable", result.stderr)


if __name__ == "__main__":
    unittest.main()
