@echo off
rem you need msys2 and the llvm, clang 3.9, libffi and vim packages installed.
rem see documentation for detailed installation instructions.
rem additionally, the built executable depends on
rem libstdc++-6.dll, libgcc_s_seh-1.dll, libffi-6.dll and libwinpthread-1.dll
setlocal
set DIR=%~dp0
set MSYSPATH=C:\msys64
set MINGWPATH=%MSYSPATH%\mingw64
set PATH=%MINGWPATH%\bin;%PATH%
echo bangra0.h
clang -x c -o %DIR%bangra0.h -P -E %DIR%bangra.cpp -I%DIR%win32 -I%MINGWPATH%/lib/libffi-3.2.1/include
if errorlevel 1 goto :fail
echo bangra.h
sed 's/_CRT_PACKING/8/g' %DIR%bangra0.h > %DIR%bangra.h
if errorlevel 1 goto :fail
echo bangra.bin.h
xxd -i bangra.h %DIR%bangra.bin.h
if errorlevel 1 goto :fail
echo bangra.b.bin.h
xxd -i bangra.b %DIR%bangra.b.bin.h
if errorlevel 1 goto :fail
echo mman.o
clang -c -o mman.o %DIR%win32\mman.c -O2 -Wno-shift-count-overflow
if errorlevel 1 goto :fail
echo realpath.o
clang -c -o realpath.o %DIR%win32\realpath.c -O2
if errorlevel 1 goto :fail
echo dlfcn.o
clang -c -o dlfcn.o %DIR%win32\dlfcn.c -O2
if errorlevel 1 goto :fail
echo linenoise.o
clang -c -o linenoise.o %DIR%external\linenoise-ng\src\linenoise.cpp -O2 ^
    -I%DIR%external\linenoise-ng\include -std=gnu++11 -fno-rtti
if errorlevel 1 goto :fail
echo ConvertUTF.o
clang -c -o ConvertUTF.o %DIR%external\linenoise-ng\src\ConvertUTF.cpp -O2 ^
    -I%DIR%external\linenoise-ng\include -std=gnu++11 -fno-rtti
if errorlevel 1 goto :fail
echo wcwidth.o
clang -c -o wcwidth.o %DIR%external\linenoise-ng\src\wcwidth.cpp -O2 ^
    -I%DIR%external\linenoise-ng\include -std=gnu++11 -fno-rtti
if errorlevel 1 goto :fail
echo bangra_luasrc.bin.h
%DIR%luajit/bin/mingw64/luajit -b %DIR%bangra.lua %DIR%bangra_luasrc.bin.h -g
if errorlevel 1 goto :fail
echo bangra.exe
clang++ -o bangra.exe %DIR%bangra.cpp ^
    -I%DIR%win32 ^
    -I%MINGWPATH%/lib/libffi-3.2.1/include -I%MINGWPATH%/include ^
    -I%DIR%luajit/csrc/luajit/src/src ^
    -Wno-vla -DBANGRA_CPP_IMPL -DBANGRA_MAIN_CPP_IMPL ^
    -D_GNU_SOURCE -pedantic -DBANGRA_DEBUG -ferror-limit=1 ^
    -D_LIBCPP_HAS_NO_CONSTEXPR ^
    -std=gnu++11 -O2 -DNDEBUG -fno-exceptions -fno-rtti ^
    -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS ^
    -L%MINGWPATH%/lib -lclangFrontend -lclangDriver -lclangSerialization ^
    -lclangCodeGen -lclangParse -lclangSema -lclangAnalysis -lclangEdit ^
    -lclangAST -lclangLex -lclangBasic ^
    -Wl,--whole-archive ^
    -Wl,--export-all-symbols ^
    %DIR%luajit/bin/mingw64/luajit.a ^
    mman.o realpath.o dlfcn.o linenoise.o ConvertUTF.o wcwidth.o ^
    -lLLVMLTO -lLLVMObjCARCOpts ^
    -lLLVMMIRParser -lLLVMSymbolize -lLLVMDebugInfoPDB -lLLVMDebugInfoDWARF ^
    -lLLVMTableGen -lLLVMLineEditor -lLLVMOrcJIT ^
    -lLLVMARMDisassembler -lLLVMARMCodeGen ^
    -lLLVMARMAsmParser -lLLVMARMDesc -lLLVMARMInfo -lLLVMARMAsmPrinter ^
    -lLLVMLibDriver -lLLVMOption -lLLVMX86Disassembler -lLLVMX86AsmParser ^
    -lLLVMX86CodeGen -lLLVMSelectionDAG -lLLVMAsmPrinter -lLLVMX86Desc ^
    -lLLVMMCDisassembler -lLLVMX86Info -lLLVMX86AsmPrinter -lLLVMX86Utils ^
    -lLLVMMCJIT -lLLVMPasses -lLLVMipo -lLLVMVectorize -lLLVMLinker ^
    -lLLVMIRReader -lLLVMAsmParser -lLLVMDebugInfoCodeView -lLLVMCoverage ^
    -lLLVMExecutionEngine -lLLVMRuntimeDyld -lLLVMCodeGen -lLLVMTarget ^
    -lLLVMScalarOpts -lLLVMInstCombine -lLLVMInstrumentation -lLLVMProfileData ^
    -lLLVMObject -lLLVMMCParser -lLLVMTransformUtils -lLLVMMC -lLLVMBitWriter ^
    -lLLVMBitReader -lLLVMAnalysis -lLLVMCore -lLLVMSupport ^
    -Wl,--no-whole-archive ^
    -lLLVMInterpreter ^
    -lffi -lole32 -luuid -lversion -lpsapi ^
    -fexceptions
if errorlevel 1 goto :fail
%DIR%bangra.exe %DIR%testing\test_all.b
if errorlevel 1 goto :fail
echo success.
goto :done
:fail
echo failed.
:done
