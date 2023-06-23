#!/bin/bash
set -o pipefail
if pgrep -x "emulator" > /dev/null
then
    echo "PiStorm emulator is running, please stop it first"
    exit 1
fi
if ! command -v openocd &> /dev/null
then
    echo "openocd is not installed, please run \"sudo apt install openocd\""
    exit 1
fi

# fpga jtag
# tck
raspi-gpio set 26 pn
# tms
raspi-gpio set 24 pn
# tdi
raspi-gpio set 27 pn
# tdo
raspi-gpio set 25 pu

echo -ne "Detecting CPLD... "
version=$(sudo openocd -f nprog/detect.cfg | awk 'FNR == 3 { print $4 }')
if [ $? -ne 0 ]
then
	echo "Error detecting CPLD."
	exit 1
fi
case $version in
	"0x020a10dd")
		echo "EPM240 detected!"
		./nprog_pi4.sh #./nprog_240.sh
		;;
	"0x020a20dd")
		echo "EPM570 detected!"
		./nprog_pi4.sh
		;;
	"0x020a50dd")
		echo "MAXV240 detected!"
		echo ""
		echo "! ATTENTION ! MAXV SUPPORT IS EXPERIMENTAL ! ATTENTION !"
		echo ""
		./nprog_maxv.sh
		;;
	*)
		echo "Could not detect CPLD"
		exit 1
		;;
esac

