## Actions ATJ2127 firmware dumper for Linux

Firmware dumper for MP3 players on the ATJ2127 chip.

When connected in player mode it shows as `10d6:1101 HS USB FlashDisk`. The specific key on the device is the boot key, when you turn off and connect while holding that key, it shows as `10d6:10d6`.

* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, USE AT YOUR OWN RISK!

* Unfinished. The `read_lfi` command requires loading special binaries (dangerous, can brick the device) from firmware update file to read/write flash memory. The `read_nand` command requires manually searching for firmware in gigabytes of raw data, often firmware is not written sequentially. The `find_lfi` command searches for specific tags in the NAND memory, outputs two copies of the firmware, both may be corrupted in some places.

### Build

There are two options:

1. Using `libusb` for Linux and Windows (MSYS2):  
Use `make`, `libusb/libusb-dev` packages must be installed.

* For Windows users - please read how to install a [driver](https://github.com/libusb/libusb/wiki/Windows#driver-installation) for `libusb`.

2. Using the USB serial, **Linux only**:  
Use `make LIBUSB=0`.
If you're using this mode, you must initialize the USB serial driver before using the tool (every boot):
```
$ sudo modprobe ftdi_sio
$ echo 10d6 10d6 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
```

* On Linux you must run the tool with `sudo`, unless you are using special udev rules (see below).

### Instructions

The device can be rebooted from flash disk mode to ADFU mode:
```
$ sudo ./atj2127_dump --id 10d6:1101 adfu_reboot
```

To make a RAM dump from ADFU mode (first 96K bytes containing ROM are zero):
```
sudo ./atj2127_dump read_mem 0xbfc00000 256K dump.bin
```

* Where 256K is the expected RAM length in bytes.
* An example payload is [here](payload) (you can read the chip's ROM with it).

#### Commands

Flash disk mode commands:

`inquiry` - print SCSI device information.  
`adfu_reboot` - reboot to ADFU mode.  

Basic commands supported by the chip's boot ROM:

`adfu_info` - print some info from the chip ROM (seems to be the chip ID, the `adfus` binary doesn't support this command).  
`write_mem <addr> <file_offset> <size> <input_file>` - zero size means until the end of the file.  
`switch <addr>` - switch to `adfus` code.  
`exec_ret <addr> <ret_size>` - execute the code and read the result (use `ret_size` = -1 if size can vary).  
`read_mem <addr> <size> <output_file>` - read memory, can't read ROM.  
`simple_switch <addr> <file>` - equivalent to `write_mem <addr> 0 0 <file> switch <addr>`.  
`simple_exec <addr> <file> <ret_size>` - equivalent to `write_mem <addr & ~1> 0 0 <file> exec_ret <addr> <ret_size>`.  

The commands below require loading the `adfus` binary that comes with the tool (using the command `simple_switch 0xbfc18000 adfus.bin`).

`reset` - reboot the device.  
`read_mem2 <addr> <size> <output_file>` - read memory (uses tiny payload, loaded at 0xbfc1e000).  
`read_lfi <addr> <size> <output_file>` - read the firmware (requires correct `fwscfNNN.bin`).  
`write_flash <sector> <file_offset> <size> <input_file>` - write flash (requires correct `fwscfNNN.bin`).  
`read_brec <payload/readnand.bin> <mbrec_dump.bin> <brec_idx> <brec_dump.bin>` - read boot record (`brec_idx` is 0 or 1).  
`read_nand <payload/readnand.bin> <rowaddr> <count> <output_file>` - read raw pages from nand flash.  
`find_lfi <payload/readnand.bin> <brec_idx> <lfi_dump.bin>` - tries to find and dump the LFI chain.  

#### Repeating the flashing process

If you capture all the data sent by the firmware update program using `usbmon` and Wireshark, the update process will look like this:

```
sudo ./atj2127_dump \
	simple_switch 0xbfc18000 adfus.bin \
	write_mem 0xbfc1e000 0 0 nandhwsc.bin exec_ret 0xbfc1e000 0x9c \
	write_mem 0x9fc24c00 0 0 nandinfo.bin \
	timeout 30000 \
	write_mem 0xbfc1e000 0 0 fwsc.bin exec_ret 0xbfc1e200 512 \
	write_flash 0x40000000 0 0 mbrec.bin \
	write_flash 0x46000000 0 0 brec.bin \
	write_flash 0xc0000000 0 0 fwim.bin \
	write_mem 0x9fc25400 0 0 flash_end.bin \
	write_flash 0xff000000 0 0 flash_end.bin \
	reset
```

#### Using the tool without sudo

If you create `/etc/udev/rules.d/80-actions.rules` with these lines:
```
# Actions
SUBSYSTEMS=="usb", ATTRS{idVendor}=="10d6", ATTRS{idProduct}=="10d6", MODE="0666", TAG+="uaccess"
SUBSYSTEMS=="usb", ATTRS{idVendor}=="10d6", ATTRS{idProduct}=="1101", MODE="0666", TAG+="uaccess"
```
...then you can run `atj2127_dump` without root privileges.

### Useful links

1. [atj2127decrypt](https://github.com/nfd/atj2127decrypt) (doesn't decrypt the latest firmware)
2. [PD196_ATJ2127](https://github.com/Suber/PD196_ATJ2127)
3. [atjboottool](https://github.com/Rockbox/rockbox/blob/master/utils/atj2137/atjboottool) (can decrypt the latest firmware)

* Also I have the [tool](https://github.com/ilyakurdyukov/smartlink_flash) for MP3 players with YP3 chip.
