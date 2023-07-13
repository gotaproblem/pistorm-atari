if pgrep -x "emulator" > /dev/null
then
    echo "PiStorm emulator is running, please stop it first"
    exit 1
fi

# fpga jtag
# tck
#raspi-gpio set 26 pn
# tms
#raspi-gpio set 24 pn
# tdi
#raspi-gpio set 27 pn
# tdo
#raspi-gpio set 25 pu

echo "Flashing..."
#sudo openocd -f ./nprog/pi4.cfg > nprog_log.txt 2>&1
sudo openocd -f ./nprog/pi4-570.cfg > nprog_log.txt 2>&1
if [ $? -ne 0 ]
then
    echo "Flashing failed, please see nprog_log.txt for details"
    exit 1
else
    echo "Flashing successful!"
fi

