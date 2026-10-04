"""
Minimal Kod (Blakod) source reader for data extraction.

This is NOT a compiler. It reads the declarative parts of .kod/.khd files
(constants, resources, classvars, properties) and keeps each message
handler's body as raw text, so extractors can pattern-match on it.

Usage:
    idx = KodIndex(Path("Server-104/kod"))
    cls = idx.classes["Bunny"]
    idx.resolve_var("Bunny", "viLevel")     -> 30
    idx.resolved_vars("Bunny")              -> merged dict over the parent chain
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

ENCODING = "latin-1"  # the Kod tree has German/Latin-1 text in resources

_SECTION_RE = re.compile(r"^(constants|resources|classvars|properties|messages)\s*:\s*$", re.M)
_HEADER_RE = re.compile(r"^\s*(\w+)\s+is\s+(\w+)\s*$", re.M)
_ASSIGN_RE = re.compile(r"^\s*(\w+)\s*=\s*(.*?)\s*$")


def strip_comments(text: str) -> str:
    """Remove // line comments and /* */ block comments, but not inside strings."""
    out = []
    i, n = 0, len(text)
    in_str = False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            # keep newlines so line counts stay roughly aligned
            chunk = text[i: (n if j < 0 else j + 2)]
            out.append("\n" * chunk.count("\n"))
            i = n if j < 0 else j + 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _join_continuations(body: str) -> list[str]:
    """Join lines ending in '\\' and multi-string resource values into logical lines."""
    lines = body.split("\n")
    logical: list[str] = []
    buf = ""
    for raw in lines:
        line = raw.rstrip()
        if buf:
            buf += " " + line.strip()
        else:
            buf = line
        stripped = buf.rstrip()
        if stripped.endswith("\\"):
            buf = stripped[:-1]
            continue
        # a resource string continued on the next line: value is a string and
        # the next line starts with a string as well -> handled by lookahead below
        logical.append(buf)
        buf = ""
    if buf:
        logical.append(buf)

    # merge lines that are bare string continuations ("..." on its own line)
    merged: list[str] = []
    for line in logical:
        s = line.strip()
        if merged and s.startswith('"') and _ASSIGN_RE.match(line) is None:
            merged[-1] = merged[-1] + " " + s
        else:
            merged.append(line)
    return merged


def parse_string_value(value: str) -> str | None:
    """'"abc" "def"' -> 'abcdef'.  Returns None if value is not a string literal."""
    value = value.strip()
    # language-tagged form used in .lkod files: de "..."
    if not value.startswith('"'):
        return None
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', value)
    s = "".join(parts)
    return s.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")


@dataclass
class KodClass:
    name: str
    parent: str | None
    path: Path
    constants: dict[str, str] = field(default_factory=dict)
    resources: dict[str, str] = field(default_factory=dict)   # name -> decoded string or filename
    classvars: dict[str, str] = field(default_factory=dict)   # raw expression text
    properties: dict[str, str] = field(default_factory=dict)  # raw expression text
    messages: dict[str, str] = field(default_factory=dict)    # handler name -> body text (comments stripped)


def _parse_assignments(block: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for line in _join_continuations(block):
        if line.strip().startswith("include"):
            continue
        m = _ASSIGN_RE.match(line)
        if m:
            out[m.group(1)] = m.group(2)
    return out


def _parse_messages(block: str) -> dict[str, str]:
    """Split the messages: section into {HandlerName: body}."""
    msgs: dict[str, str] = {}
    i, n = 0, len(block)
    head_re = re.compile(r"(\w+)\s*\(([^)]*)\)\s*(\"(?:[^\"\\]|\\.)*\"\s*)?\{", re.S)
    while i < n:
        m = head_re.search(block, i)
        if not m:
            break
        name = m.group(1)
        start = m.end()  # just after '{'
        depth, j, in_str = 1, start, False
        while j < n and depth:
            c = block[j]
            if in_str:
                if c == "\\":
                    j += 2
                    continue
                if c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
            j += 1
        msgs[name] = block[start: j - 1]
        i = j
    return msgs


def parse_kod_file(path: Path) -> KodClass | None:
    text = strip_comments(path.read_text(encoding=ENCODING, errors="replace"))
    hm = _HEADER_RE.search(text)
    if not hm:
        return None
    cls = KodClass(name=hm.group(1), parent=hm.group(2), path=path)
    if cls.parent and cls.parent.lower() == "nil":
        cls.parent = None

    # split by section headers
    sections: dict[str, str] = {}
    marks = list(_SECTION_RE.finditer(text))
    for k, m in enumerate(marks):
        end = marks[k + 1].start() if k + 1 < len(marks) else text.rfind("\nend")
        if end < m.end():
            end = len(text)
        sections[m.group(1)] = text[m.end(): end]

    cls.constants = _parse_assignments(sections.get("constants", ""))
    for k, v in _parse_assignments(sections.get("resources", "")).items():
        s = parse_string_value(v)
        cls.resources[k] = s if s is not None else v.strip()
    cls.classvars = _parse_assignments(sections.get("classvars", ""))
    cls.properties = _parse_assignments(sections.get("properties", ""))
    cls.messages = _parse_messages(sections.get("messages", ""))
    return cls


def parse_khd(path: Path) -> dict[str, str]:
    text = strip_comments(path.read_text(encoding=ENCODING, errors="replace"))
    return _parse_assignments(text)


# ---------------------------------------------------------------- expressions

_TOKEN_RE = re.compile(r"\s*(0x[0-9A-Fa-f]+|\d+|[A-Za-z_]\w*|&\w+|\$|[()|&+\-*/%~<>]{1,2})")


class ConstEval:
    """Evaluates simple integer constant expressions: names, ints, hex, | & + - * / ( ) ~ <<."""

    def __init__(self, raw: dict[str, str]):
        self.raw = raw
        self._lc = {k.lower(): k for k in raw}
        self.cache: dict[str, int | None] = {}

    def value(self, name: str, _depth: int = 0) -> int | None:
        name = self._lc.get(name.lower(), name)
        if name in self.cache:
            return self.cache[name]
        if name not in self.raw or _depth > 50:
            return None
        self.cache[name] = None  # cycle guard
        v = self.eval(self.raw[name], _depth + 1)
        self.cache[name] = v
        return v

    def eval(self, expr: str, _depth: int = 0, local: dict[str, str] | None = None) -> int | None:
        expr = expr.strip()
        if not expr:
            return None
        toks = _TOKEN_RE.findall(expr)
        if "".join(toks).replace(" ", "") != expr.replace(" ", ""):
            return None  # contains something we don't understand (strings, lists, calls)
        py = []
        for t in toks:
            if re.fullmatch(r"0x[0-9A-Fa-f]+", t):
                py.append(str(int(t, 16)))
            elif t.isdigit():
                py.append(t)
            elif re.fullmatch(r"[A-Za-z_]\w*", t):
                v = None
                lv = KodIndex._ci_get(local, t) if local else None
                if lv is not None:
                    v = self.eval(lv, _depth + 1)
                if v is None:
                    v = self.value(t, _depth + 1)
                if v is None:
                    return None
                py.append(str(v))
            elif t == "/":
                py.append("//")
            elif t in ("(", ")", "|", "&", "+", "-", "*", "%", "~", "<<", ">>"):
                py.append(t)
            else:
                return None
        try:
            return int(eval(" ".join(py), {"__builtins__": {}}, {}))  # noqa: S307 - ints/operators only
        except Exception:
            return None


# ---------------------------------------------------------------------- index

class CIDict(dict):
    """dict with case-insensitive get / [] / in (keys keep their display spelling)."""

    def _key(self, k):
        if dict.__contains__(self, k):
            return k
        lk = k.lower()
        for kk in self.keys():
            if kk.lower() == lk:
                return kk
        return None

    def __contains__(self, k):
        return self._key(k) is not None

    def __getitem__(self, k):
        kk = self._key(k)
        if kk is None:
            raise KeyError(k)
        return dict.__getitem__(self, kk)

    def get(self, k, default=None):
        kk = self._key(k)
        return default if kk is None else dict.__getitem__(self, kk)


class KodIndex:
    """All lookups are case-insensitive, like the Blakod compiler.  Output keeps the
    spelling of the first (base-most) declaration."""

    def __init__(self, kod_root: Path):
        self.root = kod_root
        raw_consts: dict[str, str] = {}
        for khd in sorted((kod_root / "include").glob("*.khd")):
            raw_consts.update(parse_khd(khd))
        self.consts = ConstEval(raw_consts)
        self.classes: dict[str, KodClass] = {}   # canonical name -> class
        self._lc: dict[str, str] = {}            # lowercase name -> canonical name
        for p in kod_root.rglob("*.kod"):
            c = parse_kod_file(p)
            if c:
                self.classes[c.name] = c
                self._lc[c.name.lower()] = c.name

    def canon(self, name: str | None) -> str | None:
        return self._lc.get(name.lower()) if name else None

    def chain(self, name: str) -> list[KodClass]:
        """[cls, parent, grandparent, ...]"""
        out = []
        seen = set()
        name = self.canon(name)
        while name and name not in seen:
            seen.add(name)
            c = self.classes[name]
            out.append(c)
            name = self.canon(c.parent)
        return out

    def is_a(self, name: str, ancestor: str) -> bool:
        a = ancestor.lower()
        return any(c.name.lower() == a for c in self.chain(name))

    def subclasses(self, ancestor: str) -> list[str]:
        return sorted(n for n in self.classes if n.lower() != ancestor.lower() and self.is_a(n, ancestor))

    @staticmethod
    def _ci_get(d: dict[str, str], key: str):
        if key in d:
            return d[key]
        k = key.lower()
        for kk, vv in d.items():
            if kk.lower() == k:
                return vv
        return None

    def raw_var(self, name: str, var: str) -> tuple[str, KodClass] | None:
        for c in self.chain(name):
            for d in (c.properties, c.classvars):
                v = self._ci_get(d, var)
                if v is not None:
                    return v, c
        return None

    def resource(self, name: str, res: str) -> str | None:
        for c in self.chain(name):
            v = self._ci_get(c.resources, res)
            if v is not None:
                return v
        return None

    def eval_in(self, name: str, expr: str):
        """Evaluate a var expression in the context of class `name`:
        int if computable, resource string if it names a resource, else raw text."""
        expr = expr.strip()
        local: dict[str, str] = {}
        for c in reversed(self.chain(name)):
            local.update(c.constants)
        v = self.consts.eval(expr, local=local)
        if v is not None:
            return v
        if re.fullmatch(r"\w+", expr):
            r = self.resource(name, expr)
            if r is not None:
                return r
        if expr == "$":
            return None
        return expr

    def resolve_var(self, name: str, var: str):
        hit = self.raw_var(name, var)
        if hit is None:
            return None
        return self.eval_in(name, hit[0])

    def resolved_vars(self, name: str) -> dict:
        merged: dict[str, tuple[str, str]] = {}  # lower -> (display name, raw)
        for c in reversed(self.chain(name)):
            for d in (c.classvars, c.properties):
                for k, v in d.items():
                    disp = merged[k.lower()][0] if k.lower() in merged else k
                    merged[k.lower()] = (disp, v)
        return CIDict({disp: self.eval_in(name, raw) for disp, raw in merged.values()})

    def message(self, name: str, msg: str) -> tuple[str, KodClass] | None:
        for c in self.chain(name):
            v = self._ci_get(c.messages, msg)
            if v is not None:
                return v, c
        return None

    def const_names(self, prefix: str) -> dict[str, int]:
        out = {}
        for k in self.consts.raw:
            if k.startswith(prefix):
                v = self.consts.value(k)
                if v is not None:
                    out[k] = v
        return out

    def flag_names(self, value: int, prefix: str) -> list[str]:
        """Decode a bitmask into constant names with the given prefix (single-bit constants only)."""
        out = []
        for k, v in sorted(self.const_names(prefix).items(), key=lambda kv: kv[1]):
            if v and (v & (v - 1)) == 0 and value & v:
                out.append(k)
        return out
