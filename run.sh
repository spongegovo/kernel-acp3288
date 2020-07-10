#!/bin/bash

#make ARCH=arm acp_rockchip_defconfig
make ARCH=arm acp_rk3288_defconfig
make ARCH=arm acp-rk3288-android-lvds.img $1
#make ARCH=arm acp-rk3288-android-lvds-FHD.img $1
#make ARCH=arm acp-rk3288-android-mipi.img $1
#make ARCH=arm acp-rk3288-android-hdmi.img $1
#make ARCH=arm acp-rk3288-android-edp-FHD.img $1

#make ARCH=arm rockchip_defconfig
#make ARCH=arm rk3288-evb-android-rk808-edp.img $1
