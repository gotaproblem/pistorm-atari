#!/bin/bash
# Script: my-pi-temp.sh
# Purpose: Display the ARM CPU and GPU  temperature of Raspberry Pi 2/3 
# Author: Vivek Gite <www.cyberciti.biz> under GPL v2.x+
# -------------------------------------------------------
cpu=$(</sys/class/thermal/thermal_zone0/temp)
echo "$(date) @ $(hostname)"
echo "-------------------------------------------"
echo "GPU => $(vcgencmd measure_temp | grep -o -E '[[:digit:]].*')"
echo "CPU => $((cpu/1000))'C"
echo "CPU frequency => $(sudo cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq)"
vcgencmd measure_volts core
vcgencmd measure_clock arm
vcgencmd measure_clock core

