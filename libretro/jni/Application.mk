APP_ABI := armeabi-v7a arm64-v8a x86 x86_64
# Frontend debuggability must not change core optimization or assertion behavior.
override APP_OPTIM := release
APP_STL := c++_static
APP_PLATFORM := android-18
APP_SHORT_COMMANDS := true
