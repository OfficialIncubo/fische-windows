# Build Instructions

## Prerequisites

Install the following once on your development machine:

| Tool | Notes |
|------|-------|
| Visual Studio 2026 Professional | Include the **Desktop development with C++** workload |
| CMake | Must be on `PATH` — installer option or add `C:\Program Files\CMake\bin` manually |
| vcpkg | Clone or extract to `C:\vcpkg` |

Install the required vcpkg packages (one-time):

```bat
C:\vcpkg\vcpkg.exe install glfw3:x64-windows-static glm:x64-windows-static
C:\vcpkg\vcpkg.exe install glfw3:x86-windows-static glm:x86-windows-static
```

---

## Building

From the project root folder, run:

```bat
build-vs2026.cmd
```

The script:
1. Activates the VS 2026 x64 and x86 developer environments
2. Configures CMake with NMake Makefiles and the matching vcpkg triplet
3. Builds separate Release executables

Outputs:

```text
dist\fische-x64.exe
dist\fische-x86.exe
```

To build only one architecture:

```bat
build-vs2026.cmd x64
build-vs2026.cmd x86
```

---

## Distributing

Copy the executable for your PC to any folder you want:

- `dist\fische-x64.exe` for 64-bit Windows
- `dist\fische-x86.exe` for 32-bit Windows

No other library and runtime files are required. All other dependencies are linked statically.

---

## Rebuilding After Moving the Source

CMake embeds absolute paths in `CMakeCache.txt`. If you move the source folder to a different path, delete the build folder before rebuilding:

```bat
rmdir /s /q build-nmake-static
rmdir /s /q build-nmake-static-x86
build-vs2026.cmd
```

---

## GitHub Actions

A workflow file is provided at `.github/workflows/build.yml`. It builds automatically on every push and pull request to `main`, and produces separate x64 and x86 artifacts containing `fische-x64.exe` and `fische-x86.exe`.

To trigger a release build with a downloadable artifact, push a tag:

```bat
git tag v1.0.0
git push origin v1.0.0
```
