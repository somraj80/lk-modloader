#!/usr/bin/env python3
"""Generate docs/lk-modloader-architecture.pdf from docs/ARCHITECTURE.md."""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "docs" / "ARCHITECTURE.md"
OUT = ROOT / "docs" / "lk-modloader-architecture.pdf"


def generate_with_pandoc() -> None:
    pandoc = shutil.which("pandoc")
    if not pandoc:
        raise RuntimeError("pandoc not found")

    engine = shutil.which("xelatex") or shutil.which("pdflatex")
    if not engine:
        raise RuntimeError("xelatex/pdflatex not found")

    cmd = [
        pandoc,
        str(SRC),
        "-o",
        str(OUT),
        f"--pdf-engine={Path(engine).name}",
        "--highlight-style=tango",
        "-V",
        "papersize=a4",
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)


def generate_fallback() -> None:
    """Minimal stdlib PDF if pandoc is unavailable."""
    import re
    import textwrap

    PAGE_W, PAGE_H = 612, 792
    MARGIN = 54

    class PdfWriter:
        def __init__(self) -> None:
            self.objects: list[bytes] = []
            self.pages: list[int] = []

        def add_object(self, data: bytes) -> int:
            self.objects.append(data)
            return len(self.objects)

        def _escape(self, text: str) -> str:
            return text.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)")

        def add_page(self, lines: list[tuple[float, float, int, str]]) -> None:
            content = ["BT"]
            for x, y, size, text in lines:
                content.append(f"/F1 {size} Tf {x:.2f} {y:.2f} Td ({self._escape(text)}) Tj")
                content.append(f"{-x:.2f} {-y:.2f} Td")
            content.append("ET")
            stream = "\n".join(content).encode("latin-1", errors="replace")
            sid = self.add_object(
                f"<< /Length {len(stream)} >>\nstream\n".encode() + stream + b"\nendstream"
            )
            self.pages.append(
                self.add_object(
                    f"<< /Type /Page /Parent {{p}} 0 R /MediaBox [0 0 {PAGE_W} {PAGE_H}] "
                    f"/Resources << /Font << /F1 {{f}} 0 R >> >> /Contents {sid} 0 R >>".encode()
                )
            )

        def write(self, path: Path) -> None:
            font = self.add_object(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
            pages_ph = self.add_object(b"PLACEHOLDER")
            catalog = self.add_object(f"<< /Type /Catalog /Pages {pages_ph} 0 R >>".encode())
            kids = " ".join(f"{p} 0 R" for p in self.pages)
            self.objects[pages_ph - 1] = (
                f"<< /Type /Pages /Kids [{kids}] /Count {len(self.pages)} >>".encode()
            )
            for i, p in enumerate(self.pages):
                self.objects[p - 1] = (
                    self.objects[p - 1].decode().replace("{p}", str(pages_ph)).replace("{f}", str(font)).encode()
                )
            parts = [b"%PDF-1.4\n"]
            offs = [0]
            for i, obj in enumerate(self.objects, 1):
                offs.append(sum(len(x) for x in parts))
                parts.append(f"{i} 0 obj\n".encode() + obj + b"\nendobj\n")
            xref = sum(len(x) for x in parts)
            parts.append(f"xref\n0 {len(self.objects)+1}\n0000000000 65535 f \n".encode())
            for o in offs[1:]:
                parts.append(f"{o:010d} 00000 n \n".encode())
            parts.append(
                f"trailer\n<< /Size {len(self.objects)+1} /Root {catalog} 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
            )
            path.write_bytes(b"".join(parts))

    text = SRC.read_text(encoding="utf-8")
    if text.startswith("---"):
        text = text.split("---", 2)[-1]
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = text.replace("**", "").replace("`", "")

    pdf = PdfWriter()
    y = PAGE_H - MARGIN
    lines: list[tuple[float, float, int, str]] = []

    def flush() -> None:
        nonlocal lines, y
        if lines:
            pdf.add_page(lines)
        lines, y = [], PAGE_H - MARGIN

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line == "---":
            y -= 6
            continue
        if line.startswith("#"):
            flush()
            size = 16 if line.startswith("# ") else 13
            line = line.lstrip("#").strip()
            for part in textwrap.wrap(line, 80):
                y -= size + 2
                lines.append((MARGIN, y, size, part))
            y -= 4
            continue
        for part in textwrap.wrap(line, 90):
            if y < MARGIN + 12:
                flush()
            y -= 11
            lines.append((MARGIN, y, 10, part))
    flush()
    pdf.write(OUT)


def main() -> int:
    if not SRC.exists():
        print(f"error: missing {SRC}", file=sys.stderr)
        return 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    try:
        generate_with_pandoc()
        backend = "pandoc"
    except RuntimeError as e:
        print(f"note: {e}; using fallback PDF writer", file=sys.stderr)
        generate_fallback()
        backend = "fallback"
    size = OUT.stat().st_size
    print(f"Wrote {OUT} ({size} bytes, {backend})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
