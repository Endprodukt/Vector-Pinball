# Umgebung fuer Windows (Git Bash / MSYS2). Aufruf:  source tools/env.sh
# Pfade anpassen, falls die Werkzeuge woanders liegen.
ARM_ROOT="C:/PROGRA~2/ARMGNU~1/12977F~1.2MP"   # Arm GNU Toolchain (Kurzpfad wegen der Leerzeichen)
export UVM2_ARM_TOOLCHAIN="$ARM_ROOT"
export PATH="/c/PROGRA~2/ARMGNU~1/12977F~1.2MP/bin:/c/Users/ocare/AppData/Local/Microsoft/WinGet/Links:/c/Users/ocare/AppData/Local/Programs/Python/Python310:/c/Program Files/CMake/bin:/c/msys64/mingw64/bin:/c/Users/ocare/.cargo/bin:$PATH"
export CMAKE_GENERATOR=Ninja
