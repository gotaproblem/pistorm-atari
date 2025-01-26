## Generated SDC file "pistormsxb_devEPM240.sdc"

## Copyright (C) 2020  Intel Corporation. All rights reserved.
## Your use of Intel Corporation's design tools, logic functions 
## and other software and tools, and any partner logic 
## functions, and any output files from any of the foregoing 
## (including device programming or simulation files), and any 
## associated documentation or information are expressly subject 
## to the terms and conditions of the Intel Program License 
## Subscription Agreement, the Intel Quartus Prime License Agreement,
## the Intel FPGA IP License Agreement, or other applicable license
## agreement, including, without limitation, that your use is for
## the sole purpose of programming logic devices manufactured by
## Intel and sold by Intel or its authorized distributors.  Please
## refer to the applicable agreement for further details, at
## https://fpgasoftware.intel.com/eula.


## VENDOR  "Altera"
## PROGRAM "Quartus Prime"
## VERSION "Version 20.1.0 Build 711 06/05/2020 SJ Lite Edition"

## DATE    "Thu Jan 02 23:55:23 2025"

##
## DEVICE  "EPM240T100C5"
##


#**************************************************************
# Time Information
#**************************************************************

set_time_format -unit ns -decimal_places 3



#**************************************************************
# Create Clock
#**************************************************************

create_clock -name {M68K_CLK} -period 125.000 -waveform { 0.000 62.500 } [get_ports {M68K_CLK}]
create_clock -name {PI_A[0]} -period 10.000 -waveform { 0.000 5.000 } 
create_clock -name {STATE[0]} -period 10.000 -waveform { 0.000 5.000 } 
create_clock -name {STATE[1]} -period 10.000 -waveform { 0.000 5.000 } 


#**************************************************************
# Create Generated Clock
#**************************************************************



#**************************************************************
# Set Clock Latency
#**************************************************************



#**************************************************************
# Set Clock Uncertainty
#**************************************************************



#**************************************************************
# Set Input Delay
#**************************************************************



#**************************************************************
# Set Output Delay
#**************************************************************



#**************************************************************
# Set Clock Groups
#**************************************************************



#**************************************************************
# Set False Path
#**************************************************************

set_false_path -from [get_ports {M68K_CLK M68K_DTACK_n M68K_VPA_n M68K_IPL_n[*] PI_A[*] PI_D[*] PI_RESET PI_WR}] 
set_false_path -to [get_ports {LTCH_A_0 LTCH_A_8 LTCH_A_16 LTCH_A_24 LTCH_A_OE_n LTCH_D_RD_L LTCH_D_RD_OE_n LTCH_D_RD_U LTCH_D_WR_L LTCH_D_WR_OE_n LTCH_D_WR_U M68K_AS_n M68K_BG_n M68K_E M68K_FC[*] M68K_HALT_n M68K_LDS_n M68K_RESET_n M68K_RW M68K_UDS_n M68K_VMA_n PI_TXN_IN_PROGRESS PI_IPL1 PI_IPL2 PI_D[*]}]


#**************************************************************
# Set Multicycle Path
#**************************************************************



#**************************************************************
# Set Maximum Delay
#**************************************************************



#**************************************************************
# Set Minimum Delay
#**************************************************************



#**************************************************************
# Set Input Transition
#**************************************************************

