echo off

set testdir=/storage/emulated/0/Android/data/com.bairimeng.dmmdzz/files/Test
rem set testdir=/storage/emulated/legacy/Android/data/com.bairimeng.dmmdzz/files/Test

adb shell mkdir %testdir%
adb push D:\Projects\hide_2018_android2\ApksDiffZipGenerator\Generated\libil2cpp.so.new %testdir%/libil2cpp.so.new
adb push D:\Projects\hide_2018_android2\ApksDiffZipGenerator\Generated\assets_bin_Data\assets.bin %testdir%/assets_bin_Data/assets.bin