# anthy-cmake

Build [Anthy](https://github.com/fujiwarat/anthy-unicode) with CMake

## Usage

```sh
cmake -B build .
cmake --build build
cmake --install build --prefix install
```

## Build with Android NDK

Use [build-android.sh](./build-android.sh):

```sh
ANDROID_NDK_ROOT=$ANDROID_HOME/ndk/25.0.8775105 \
ANDROID_SDK_CMAKE_VERSION=3.22.1 \
ANDROID_PLATFORM=23 \
ANDROID_ABI=arm64-v8a,armeabi-v7a,x86,x86_64 \
./build-android.sh
```
