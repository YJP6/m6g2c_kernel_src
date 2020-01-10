#!/bin/sh

make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- distclean &&

make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- epc_m6g2c_defconfig &&
#make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- epc_m6g2c_mfg_defconfig &&
#make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- epc_m6g2c_wifi_defconfig &&
#make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dcp_1000l_defconfig &&

make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- menuconfig

[ ${JOBS} ] || {
	# get cpu logic number
	JOBS=$(getconf _NPROCESSORS_ONLN || egrep -c '^processor|^CPU[0-9]' /proc/cpuinfo) 2>/dev/null
}

[ ${JOBS} ] || {
	JOBS=1
}

make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- all -j${JOBS}
