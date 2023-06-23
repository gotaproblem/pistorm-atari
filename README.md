# PiStorm 4 Atari


# Join us on Discord

* There's a Discord server dedicated to PiStorm Atari discussion and development, which you can join through this handy invite link: https://discord.gg/QFSQam5P

* **IMPORTANT NOTE: Selling blank or complete PCBs or derivatives on eBay or elsewhere for excessive profit is frowned upon and may lead to forthcoming related projects being closed source.**
* Even with the current global chip shortage (October 2021), components are not **so** expensive that you should pay up to a hundred dollars or Euros for a board.
* The PiStorm is not a project for making money, it is meant to be an affordable way to replace and extend the functionality of EOL Motorola 68000 processors and have fun in the process.
* This is not meant to discourage you from making PiStorm boards for others to enjoy, but for instance selling the product as a commercial item and then pawning off support to the community if something doesn't work is absolutely not good™.

# Project information

* This fork is solely for the Atari platform. All Amiga code has been removed.
* Development is ongoing on an Atari STe, utilising an exxos PLLC adapter board to allow the fitting of the PiStorm interface. The ongoing work is a "proof-of-concept". If proven to offer acceptable performance, then bespoke hardware will be needed to allow installation within the confines of the Atari ST platform.
* The Atari platform differs greatly to the Amiga platform. Atari uses FC lines and depends heavily upon bus arbitration and interrupts.
* Initial development was on a PI3B, but performance was poor. A PI3A+ was tried and again the performance was poor. Finally, a PI4B was tried. Although initial performance was still poor in comparison to the Amiga, there was headroom for improvemnt. Over many months, performance has slowly increased, and at time of writing, performance is finally acceptable. 
# Performance with the current use of Musashi as the 68k CPU emulator is somewhere around a 100-125MHz 68030.


# Extended functionality

A virtual IDE interface is included which allows for two disk drive images to be attached. The BBaN Solutions HDC has been tested extensively and offers good performance via the ACSI bus.
* Atari ST ROM images can be loaded at initialisation. For example, the emulation can boot using TOS 1.4 (ST only), TOS 1.62, TOS 2.06 or even EMUTos
* TT-RAM option can be added to increase performance
* Additional interfaces will be added with time

# Simple quickstart

* Download Raspberry Pi OS from https://www.raspberrypi.org/software/operating-systems/, the Lite version is recommended as the windowing system of the Full version adds a lot of extra system load which may impact performance.
* Write the Image to a SD Card. For development, a minimum of 32GB is recommended for the PiStorm binaries and required libraries, but if you wish to use large hard drive images or sometthing with it, go with a bigger card.
* Install the PiStorm adapter in place of the orignal CPU in the system.
  Make sure the PiStorm sits flush and correct in the socket.
  Double check that all is properly in place and no pins are bent.
* Connect an HDMI Display and a USB keyboard to the PiStorm. Using a USB Hub is possible, an externally powered hub is recommended.
  Connect the Amiga to the PSU and PAL Monitor
* Insert the SD into the Raspberry Pi, Power on the Amiga now. You should see a Rainbow colored screen on the HDMI Monitor and the PiStorm booting.

* When the boot process is finished (on the first run it reboots automatically after resizing the filesystems to your SD) you should be greeted with the login prompt.
* Log in as the default user, typically user: `pi` and password: `raspberry`. (The keyboard is set to US Layout on first boot!)
* Run `sudo raspi-config`
* Set up your preferences like keyboard layout, language, etc. It is recommended to set the screen resolution to 1280x720.
* Set up your Wi-Fi credentials
* Enable SSH at boot time
* Exit raspi-config

You can now reach the PiStorm over SSH, check your router web/settings page to find the IP of the PiStorm, or run `ifconfig` locally on the PiStorm from the console.

Now the final steps to get things up and running, all of this is done from a command prompt (terminal) either locally on the PiStorm or over ssh:
* `sudo apt-get update`
* `sudo apt full-upgrade` (If you get mysterious 'not found' messages from running the line in the next step.)
* `sudo apt-get install git libasound2-dev`
* `git clone https://github.com/gotaproblem/pistorm-atari`
* `cd pistorm-atari`
* `make`

**Testing**
It is recommended you run ataritest to confirm basic functionality.
* cd pistorm-atari
* sudo ./ataritest --memory tests=rw

ataritest can do a lot more, like reading and writing (peek and poke), to Atari memory space, filling Atari memory with patterns.

**Starting the Emulator**
You can start the emulator with a basic default Atari config by typing `sudo ./emulator --config atari.cfg`.    
**Important note:** Try not to edit the sample config file - `atari.cfg`. It is advised you create three directories above the pistorm-atari installation, for configurations, rom images and disk images. 
* cd pistorm-atari
* cd ..
* mkdir configs
* mkdir roms
* mkdir dkimages
* cp pistorm-atari/atari.cfg configs/ 
* cd pistorm-atari
Run the emulator using `sudo ./emulator --config ../configs/atari.cfg`. This way, you will never have any problems using `git pull` to update your PiStorm repo to the latest commit.

To exit the emulator you can press `Ctrl+C`.

# FPGA bitstream update :

Install OpenOCD:
`sudo apt-get install openocd`

Run the FPGA update with `./flash.sh`, this will automatically detect your CPLD version and flash appropriately.

If successful "Flashing successful!" will appear, if not it will fail with "Flashing failed" and `nprog_log.txt` will be created with more details.
