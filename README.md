# LVGL for Linux on Microchip's MPU
This software ports LVGL to Microchip’s Linux build, with support for LCDC and XLCDC hardware overlay features.
# How to build
Update the SDK path to yours in the build/makefile
> SDK=/path_to_yours/arm-buildroot-linux-gnueabihf_sdk-buildroot

Run make in build directory
> $ make
# Run the demo
Upload build/app to target to run
>./app