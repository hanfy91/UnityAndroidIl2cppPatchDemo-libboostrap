echo building android...
call ./build_android.bat

set modulename=bootstrap
set modulefile=libs\armeabi-v7a\lib%modulename%.so

copy %modulefile% D:\Projects\hide_dev_2018_android\build_android_pro\public\main\jniLibs\armeabi-v7a\lib%modulename%.so

