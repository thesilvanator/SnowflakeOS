# SnowflakeOS
A hobby OS to help me learn about kernel stuff, to eventually get into linux kernel developpement. Currently it can: boot in higher half, setup paging, handle hardware exceptions and IRQs, print things on the screen, get keyboard input (but it's useless right now), and count seconds with the PIC. I also aim to make the code readable and well-organized. 

# Build & Run in a QEMU
Run build.sh to get the binary, iso.sh to get the iso, and qemu.sh to run it in qemu.

# Installation on real hardware
`sudo dd if=SnowflakeOS.iso of=/dev/sdX # I'm not responsible if you break anything`
Where /dev/sdX is the device file representing an USB key. You can then boot from it.

# Requirements
* An i686 cross-compiler in your path (see osdev.org's article)
* Xorriso, found in the package `libisoburn` on Archlinux
* `grub` and its optional dependency `mtools`
* `qemu`, unless you run it on real hardware
