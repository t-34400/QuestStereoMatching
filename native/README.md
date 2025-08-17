# Meta Quest Stereo Matching Native Plugin

This directory contains the native plugin source code for **stereo matching** on Meta Quest, including camera access and the image processing pipeline.

## Prerequisites

1. **Android NDK**
   Install the Android NDK (version 26 or newer recommended) and note its installation path.
   You will need to provide this path in the build command as `<ANDROID_NDK_PATH>`.

2. **OpenCV Core + ximgproc**
    The required OpenCV core and ximgproc binaries are already included in `third_party/opencv`.
    If you want to build them yourself for Meta Quest, please refer to the workflow in:
    [https://github.com/t-34400/android-native-build](https://github.com/t-34400/android-native-build)
---

## Building

### Windows (PowerShell + Ninja)

```powershell
cmake -S native -B build `
  -G "Ninja" `
  -D CMAKE_TOOLCHAIN_FILE="<ANDROID_NDK_PATH>/build/cmake/android.toolchain.cmake" `
  -D ANDROID_ABI=arm64-v8a `
  -D ANDROID_PLATFORM=26 `
  -D CMAKE_BUILD_TYPE=Release `
  -D CMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build -j
```

---

### Linux / macOS (bash/zsh + Ninja)

```bash
cmake -S native -B build \
  -G "Ninja" \
  -D CMAKE_TOOLCHAIN_FILE="<ANDROID_NDK_PATH>/build/cmake/android.toolchain.cmake" \
  -D ANDROID_ABI=arm64-v8a \
  -D ANDROID_PLATFORM=26 \
  -D CMAKE_BUILD_TYPE=Release \
  -D CMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build build -j
```

> Replace `<ANDROID_NDK_PATH>` with the full path to your installed Android NDK.
> On macOS/Linux, you can find it via `sdkmanager --list` or by checking your Android SDK folder.