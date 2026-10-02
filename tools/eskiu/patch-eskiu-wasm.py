#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch-eskiu-wasm.py /path/to/eskiu")

root = Path(sys.argv[1])
cmake_path = root / "CMakeLists.txt"
codegen_path = root / "codegen" / "codegen_module.cpp"

cmake = cmake_path.read_text()
codegen = codegen_path.read_text()

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)

cmake = replace_once(
    cmake,
    '''if(NOT (ESKIU_STATIC AND APPLE))
    target_compile_definitions(eskiuc PRIVATE ESKIU_HAS_X86)
endif()

if(ESKIU_STATIC)''',
    '''if(NOT (ESKIU_STATIC AND APPLE))
    target_compile_definitions(eskiuc PRIVATE ESKIU_HAS_X86)
endif()

# Viro web probe: include LLVM's WebAssembly code-emission backend.
target_compile_definitions(eskiuc PRIVATE ESKIU_HAS_WASM)

if(ESKIU_STATIC)''',
    "compiler definition")

cmake = replace_once(
    cmake,
    '''set(STATIC_BACKENDS "aarch64codegen aarch64asmparser aarch64desc aarch64info armcodegen armasmparser armdesc arminfo")''',
    '''set(STATIC_BACKENDS "aarch64codegen aarch64asmparser aarch64desc aarch64info armcodegen armasmparser armdesc arminfo webassemblycodegen webassemblyasmparser webassemblydesc webassemblyinfo")''',
    "Apple static backend list")

cmake = replace_once(
    cmake,
    '''set(STATIC_BACKENDS "aarch64codegen aarch64asmparser aarch64desc aarch64info x86codegen x86asmparser x86desc x86info armcodegen armasmparser armdesc arminfo")''',
    '''set(STATIC_BACKENDS "aarch64codegen aarch64asmparser aarch64desc aarch64info x86codegen x86asmparser x86desc x86info armcodegen armasmparser armdesc arminfo webassemblycodegen webassemblyasmparser webassemblydesc webassemblyinfo")''',
    "non-Apple static backend list")

cmake = replace_once(
    cmake,
    '''        x86codegen x86asmparser x86desc x86info
        armcodegen armasmparser armdesc arminfo)''',
    '''        x86codegen x86asmparser x86desc x86info
        armcodegen armasmparser armdesc arminfo
        webassemblycodegen webassemblyasmparser webassemblydesc webassemblyinfo)''',
    "dynamic backend list")

codegen = replace_once(
    codegen,
    '''    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTargetMC();
#ifdef ESKIU_HAS_X86''',
    '''    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTargetMC();
#ifdef ESKIU_HAS_WASM
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTargetMC();
#endif
#ifdef ESKIU_HAS_X86''',
    "WebAssembly target init")

codegen = replace_once(
    codegen,
    '''        LLVMInitializeARMAsmPrinter();
        LLVMInitializeARMAsmParser();
#ifdef ESKIU_HAS_X86''',
    '''        LLVMInitializeARMAsmPrinter();
        LLVMInitializeARMAsmParser();
#ifdef ESKIU_HAS_WASM
        LLVMInitializeWebAssemblyAsmPrinter();
        LLVMInitializeWebAssemblyAsmParser();
#endif
#ifdef ESKIU_HAS_X86''',
    "WebAssembly asm init")

cmake_path.write_text(cmake)
codegen_path.write_text(codegen)
print("Patched Eskiu with LLVM WebAssembly backend support")
