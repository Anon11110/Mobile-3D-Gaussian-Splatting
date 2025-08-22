from __future__ import annotations

import os
import sys


def _supports_color() -> bool:
    try:
        return sys.stdout.isatty() and os.environ.get("NO_COLOR") is None
    except Exception:
        return False


RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
FG_RED = "\033[31m"
FG_GREEN = "\033[32m"
FG_YELLOW = "\033[33m"
FG_BLUE = "\033[34m"
FG_MAGENTA = "\033[35m"
FG_CYAN = "\033[36m"
FG_GRAY = "\033[90m"

_COLOR_ENABLED = _supports_color()


def colorize(text: str, color: str) -> str:
    if not _COLOR_ENABLED:
        return text
    return f"{color}{text}{RESET}"


def sep(char: str = "-", width: int = 60) -> None:
    print(char * width)


def section(title: str) -> None:
    sep("=")
    print(colorize(title, BOLD + FG_MAGENTA))
    sep("=")


def info(message: str) -> None:
    print(colorize(f"[INFO] {message}", FG_CYAN))


def success(message: str) -> None:
    print(colorize(f"[OK]   {message}", FG_GREEN))


def warn(message: str) -> None:
    print(colorize(f"[WARN] {message}", FG_YELLOW))


def error(message: str) -> None:
    print(colorize(f"[ERR]  {message}", FG_RED))


def kv(key: str, value: str) -> None:
    k = colorize(key, BOLD)
    print(f"  {k}: {value}")
