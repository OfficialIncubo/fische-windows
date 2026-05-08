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
```

---

## Building

From the project root folder, run:

```bat
build-vs2026.cmd
```

The script:
1. Activates the VS 2026 x64 developer environment
2. Configures CMake with NMake Makefiles and the vcpkg toolchain
3. Builds in Release mode

Output: `build-nmake-static\fische.exe`

---

## Distributing

Copy these two files to the same folder:

```
fische.exe          (from build-nmake-static\)
glfw3.dll           (from the project root)
```

No other runtime files are required. All other dependencies are linked statically.

---

## Rebuilding After Moving the Source

CMake embeds absolute paths in `CMakeCache.txt`. If you move the source folder to a different path, delete the build folder before rebuilding:

```bat
rmdir /s /q build-nmake-static
build-vs2026.cmd
```

---

## GitHub Actions

A workflow file is provided at `.github/workflows/build.yml`. It builds automatically on every push and pull request to `main`, and produces a `fische-windows` artifact containing `fische.exe` and `glfw3.dll`.

To trigger a release build with a downloadable artifact, push a tag:

```bat
git tag v1.0.0
git push origin v1.0.0
```
