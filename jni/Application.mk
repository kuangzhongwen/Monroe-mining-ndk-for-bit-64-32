NDK_TOOLCHAIN_VERSION:=4.9

APP_ABI := armeabi armeabi-v7a arm64-v8a
APP_STL := gnustl_static
APP_PLATFORM :=  android-16
APP_CPPFLAGS += -fexceptions -fpermissive

