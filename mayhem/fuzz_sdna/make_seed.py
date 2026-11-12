#!/usr/bin/env python3
"""Writes small, valid SDNA blobs (the payload of a .blend DNA1 block) used as fuzz seeds and as
known-answer input for mayhem/selftest. Usage: make_seed.py <out-dir>"""
import struct as st
import sys


def pad4(b):
    return b + b"\0" * (-len(b) % 4)


def sdna(ptr, extra_structs):
    types = ["char", "uchar", "short", "ushort", "int", "long", "ulong", "float", "double",
             "int64_t", "uint64_t", "void", "int8_t"]
    sizes = [1, 1, 2, 2, 4, 4, 4, 4, 8, 8, 8, 0, 1]
    names = []

    def name(n):
        if n not in names:
            names.append(n)
        return names.index(n)

    structs = []

    def add_struct(tname, size, members):
        types.append(tname)
        sizes.append(size)
        ti = len(types) - 1
        structs.append((ti, [(types.index(t), name(n)) for t, n in members]))

    add_struct("Link", 2 * ptr, [("Link", "*next"), ("Link", "*prev")])
    add_struct("ListBase", 2 * ptr, [("void", "*first"), ("void", "*last")])
    add_struct("Vec3", 12, [("float", "x"), ("float", "y"), ("float", "z")])
    add_struct("ID", 2 * ptr + 72, [("ID", "*next"), ("ID", "*prev"), ("char", "name[66]"),
                                 ("short", "flag"), ("char", "pad[4]")])
    if extra_structs:
        add_struct("Mesh", 72 + 2 * ptr + ptr + 8 + 2 * ptr + 64,
               [("ID", "id"), ("Vec3", "*verts"), ("int", "totvert"), ("int", "pad"),
                ("ListBase", "edges"), ("float", "mat[4][4]")])
    out = b"SDNA" + b"NAME" + st.pack("<i", len(names))
    out += pad4(b"".join(n.encode() + b"\0" for n in names))
    out += b"TYPE" + st.pack("<i", len(types))
    out += pad4(b"".join(t.encode() + b"\0" for t in types))
    out += b"TLEN" + pad4(st.pack("<%dh" % len(sizes), *sizes))
    out += b"STRC" + st.pack("<i", len(structs))
    for ti, members in structs:
        out += st.pack("<hh", ti, len(members))
        for mt, mn in members:
            out += st.pack("<hh", mt, mn)
    return out


d = sys.argv[1]
open(d + "/sdna_64bit_mesh.bin", "wb").write(sdna(8, True))
open(d + "/sdna_64bit_min.bin", "wb").write(sdna(8, False))
open(d + "/sdna_32bit_mesh.bin", "wb").write(sdna(4, True))
