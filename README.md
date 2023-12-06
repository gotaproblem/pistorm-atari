# PiStorm 4 Atari


# Join us on Discord

* There's a Discord server dedicated to PiStorm Atari discussion and development, which you can join through this handy invite link: https://discord.gg/XFEBA8QDzQ

* **IMPORTANT NOTE: Selling blank or complete PCBs or derivatives on eBay or elsewhere for excessive profit is frowned upon and may lead to forthcoming related projects being closed source.**
* Even with the current global chip shortage (October 2021), components are not **so** expensive that you should pay up to a hundred dollars or Euros for a board.
* The PiStorm is not a project for making money, it is meant to be an affordable way to replace and extend the functionality of EOL Motorola 68000 processors and have fun in the process.
* This is not meant to discourage you from making PiStorm boards for others to enjoy, but for instance selling the product as a commercial item and then pawning off support to the community if something doesn't work is absolutely not good™.

# Project information

* This fork is solely for the Atari platform. All Amiga code has been removed.
* Development is ongoing on an Atari STe, utilising an exxos PLLC adapter board to allow the fitting of the PiStorm interface. The ongoing work is a "proof-of-concept". If proven to offer acceptable performance, then bespoke hardware will be needed to allow installation within the confines of the Atari ST platform. **If you are lucky enough to own a Phoenix H5 board (exxos shop), then the PiStorm with Pi4 fits nicely :)**
* The Atari platform differs greatly to the Amiga platform. Atari uses FC lines and depends heavily upon bus arbitration and interrupts.
* Initial development was on a PI3B, but performance was poor. A PI3A+ was tried and again the performance was poor. Finally, a PI4B was tried. Although initial performance was still poor in comparison to the Amiga, there was headroom for improvemnt. Over many months, performance has slowly increased, and at time of writing, performance is finally acceptable. 

**NOTE**

~~PiStorm must have 374 latches. Any other are known not to be compatible for the moment.~~

Sep 2023 - Firmware development has resolved the different latch/flip-flop type dependancy. Meaning, both 373 and 374 parts now work.

Sep 2023 - Pi3A+ now offers acceptible performance

Oct 2023 - Pi3B is working but needs to be overclocked

Oct 2023 - Pi Zero 2W is working but needs to be overslocked

# Extended functionality

A virtual IDE interface is included which allows for two disk drive images to be attached, each up to 8GB in size. 
**UPDATE Oct 2023**
An additional three virtual IDE interfaces are now included, allowing for a total of eight disk drive images - a potential 64GB of disk space.

* Atari ST ROM images can be loaded at initialisation. For example, the emulation can boot using TOS 1.04 (ST only), TOS 1.06, TOS 1.62, TOS 2.06 or even EMUTos
* Alt-RAM/TT-RAM option can be added to increase performance
* 68020 CPU can be used with an FPU
* ET4000 SVGA graphics is now available with 1MB VRAM
* Screen dumps can be taken when using ET4000
* Additional interfaces will be added with time

# Now to get up-and-running
The following steps require basic knowledge of the linux operating system. Take your time!

* Download Raspberry Pi OS from https://www.raspberrypi.org/software/operating-systems/, you need the 32 bit Lite version.
**NOTE**
* Atari PiStorm installation has been tested with Bullseye and Bookworm

* Write the Image to an SD card. For development, a 32 GB SD card is recommended.
* Connect an HDMI Display and a USB keyboard to the Raspberry Pi.
* Insert the SD card into the Raspberry Pi4, and connect it to power. You should see a Rainbow colored screen on the HDMI Monitor followed by the booting sequence.
* When the boot process is finished (on the first run it reboots automatically after resizing the filesystems to your SD) you should be greeted with the login prompt.
* Log in as the default user, typically user: `pi` and password: `raspberry`. (The keyboard is set to US Layout on first boot!)
* Run `sudo raspi-config`
* Set up your preferences like keyboard layout, language, etc. It is recommended to set the screen resolution to 1280x720.
* Set up your Wi-Fi credentials
* Enable SSH at boot time
* Exit raspi-config

You can now reach the Pi over SSH, check your router web/settings page to find the IP of the PiStorm, or run `ifconfig` locally on the Pi from the console.

Now the final steps to get things up and running, all of this is done from a command prompt (terminal) either locally on the Pi or over ssh:
* `sudo apt-get update`
* `sudo apt full-upgrade` (If you get mysterious 'not found' messages from running the line in the next step.)
* ~~`sudo apt-get install git libasound2-dev`~~
* `sudo apt-get install git ffmpeg`
* `git clone https://github.com/gotaproblem/pistorm-atari`
* `cd pistorm-atari`
* `make clean`
**NOTE**
If using a Pi3, make must be given an option
* `make PIMODEL=PI3`
If using a Pi4, no option is needed
* `make`

~~**STRAM CACHE**~~
~~The experimental full memory cache-enabled version may be built with~~
~~* `make CACHE=ON`~~
~~or~~
~~* `make POMODEL=PI3 CACHE=ON`~~

~~This feature greatly improves STRAM read operations (and hence is mostly applicable to games), but is not yet compatible with operation of the Blitter, so please disable this at the Desktop.~~

Copy the linux boot configuration file:
* `sudo cp configs/config.txt /boot/`

Append the following to your /boot/cmdline.txt file
* `quiet vt.global_cursor_default=0 fsck.mode=force fsck.repair=yes`
**NOTE**
Do not add new-lines to this file. The entries must be one continuous line

Congratulate yourself - You've gotten this far! Shutdown the linux operating system
* `sudo halt`

# Install the hardware
**Power Caution**

How do you intend to power this thing? There are two possibilities
* 1 - power via the Atari
This is okay for the final-cut, but for development, option 2 is suggested
* 2 - power via USB
If you are developing, which requires numerous power cycles, then this is the option for you. You will need to make sure USB power (5v) does not reach the PiStorm. The Pi GPIO header supplies 5v power on pins 2 and 4. These two pins need to be cut.
As the Pi physically can not connect directly to the PiStorm, a header extension is needed - this is where you cut the pins - **do not cut the pins on the Raspberry Pi header.**
**NOTE**
DO NOT APPLY POWER FROM MULTIPLE SOURCES

If using a Pi4, a Header extension is needed, such as

https://www.amazon.co.uk/gp/product/B08C581XHV/ref=ppx_yo_dt_b_asin_title_o03_s00?ie=UTF8&th=1

https://www.amazon.co.uk/gp/product/B07NQ5Z7Y9/ref=ppx_yo_dt_b_asin_title_o04_s00?ie=UTF8&psc=1

* Attach the Pi to the PiStorm and install the PiStorm adapter in place of the orignal CPU in the Atari.
  Make sure the PiStorm sits flush and correct in the socket.
  Double check that all is properly in place and no pins are bent.

# FPGA bitstream update :
Before we can use the PiStorm, it needs to have firmware installed. 

~~Install OpenOCD:~~
~~**NOTE - v0.11 needed**~~
~~Check your openocd version~~
~~* `openocd -v`~~
~~If you have version 0.12 or later, follow these instructions:~~
~~* `sudo apt-get remove openocd`~~
~~* `sudo apt-get install openocd=0.11.0~rc2-1`~~

~~Run the FPGA update with `./flash.sh`, this will automatically detect your CPLD variant and flash appropriately.~~

~~If successful "Flashing successful!" will appear, if not it will fail with "Flashing failed" and `nprog_log.txt` will be created with more details.~~

Dec 2023 - openocd is no longer used

* Flash the PiStorm firmware using the included tool - replace xxx with your CPLD type eg. EPM240 or EPM570
`rtl/pistormflash -s rtl/EPMxxx_bitstream.svf`
Expect a "Warning" to be shown. This can be ignored.

# Running

**Testing**

As a confidence test, it is suggested you run ataritest to confirm basic functionality.
* `sudo ./ataritest --memory tests=rw`
If you see any errors, then discuss in the discord development channel. 

ataritest can do a lot more, like reading and writing (peek and poke), to Atari memory space, filling Atari memory with patterns.

**Setup file structure**

From the pistorm-atari directory, perform the following commands
* `mkdir ../roms`
* `mkdir ../dkimages`
* `mkdir ../configs`
* `mkdir ../screendumps`
* `cp configs/atari.cfg ../configs`
* `cp roms/roms.zip ../roms/`
* `cd ../roms`
* `unzip roms.zip`
* `cd ../pistorm-atari`

**Starting the Emulator**

Start the emulator with the default Atari configuration by typing `sudo ./emulator --config ../configs/atari.cfg`

A couple of disk images have been created to help you get started. These are optional. You could of course run programmes off your 
floppy disk drive and/or an attached ACSI bus device.

**The disk images are too large for GitHub. Please find the disk images at https://bbansolutions.co.uk**
Download the disks.zip file, copy to ../dkimages/ directory. Uncompress the file to give two disk images.
* `cd ../dkimages`
* `wget http://bbansolutions.co.uk/wp-content/uploads/2023/10/disks.zip`
* `unzip disks.zip`
* `cd ../pistorm-atari`

Run the emulator using `sudo ./emulator --config ../configs/atari.cfg`.

To exit the emulator you can press `Ctrl+C`.

# RTG Graphics ~~- Raylib~~
Sep 2023 - no-longer using raylib

Enhanced video modes are available for GEM and Mint.
* Changes to the emulator configuration file are needed
* edit your .cfg file and uncomment (remove the preceding hash), the following line
* #setvar rtg

**A mint specific configuration file is now included in the repository**
* Make a copy of configs/mint.cfg
* `cp configs/mint.cfg ../configs/`

~~Finally, perform a rebuild~~
~~To build now, using RTG, you must alter the git branch before compiling~~
~~* `git checkout et4000-dev`~~
~~* `make clean`~~
~~* `make`~~
