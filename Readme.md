xGVX64 - Dolphin-Rpi-5.0-4544

This is a fork of dolphin-emu that rolls back to commit 5.0-4544 (June 2017) 
and implements numerous changes intended to resolve the rendering issue due 
to the loss of alpha pass support that is currently impacting many Pi4/Pi5 users
who are running Bookworm OS. In particular, the following modifications to 
5.0-4544 have been made:
* Re-implements the alpha pass feature that was removed in 5.0-1653
* Implements changes in 5.0-13669 to resolve numerous compile-time errors
  of the form "index is not a constant expression" that occurs when 
  compiling legacy versions of dolphin on gcc-11
* Rolls forward code to add support for .rvz files that was implemented in 5.0-12188
* Rolls forwards code to add support for .m3u files and automatic disc changing implemented in 5.0-9343
* Implements patch from https://github.com/mimimi085181/dolphin which corrects rendering issues when EFBToTextureEnable is turned on in The Last Story
* Resolves some other minor compiler errors.

Note that when utilizing this build, it is recommended to only use the
Vulkan backend.  While alpha pass has also been reintroduced for the
GL/GLES backend, on Bookworm on the Pi there are major performance issues 
with GLES 3.1 and it is unusable.


Instructions for Building from Source:

1. cd /home/pi

2. sudo git clone https://github.com/gvx64/dolphin-rpi

3. cd ./dolphin-rpi

4. git submodule update --init --recursive

Edit CMakeLists.txt options if needed. I turned off/on the following flags in the CMake file by default (you may want pulseaudio turned on for your machine):

    USE_UPNP turned off
    ENABLE_PULSEAUDIO turned off
    ENABLE_ANALYTICS turned off
    ENCODE_FRAMEDUMPS turned off
    ENABLE_QT2 turned on

1. sudo nano ./CMakeLists.txt

If you haven't already done so, you may want to add the following packages:

1. sudo apt-get install libevdev-dev libgtk2.0-dev libopenal-dev qtbase5-private-dev

Also, make sure that Vulkan is installed on your Pi if you haven't already done so before proceeding further.

Now lets configure:

1. mkdir Build

2. cd ./Build

3. sudo cmake .. -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11

(note that you may need to sudo apt-get install gcc-11 and g++-11 if you don't have them installed by default on Bookworm)

Finally build (this will take a while):

1. sudo make -j4

(note that if you encounter errors you should rebuild with -j1 as the error messages are sometimes a lot more descriptive when building with single core for some reason)

EDIT: Optional step although possibly not recommended. This will put config/settings files in potentially inconvenient locations. I personally skip this step and just create my own config directory and specify its location with the "-u" option in the emulators.cfg file.

1. sudo make install

Anyways, assuming that there are no errors, this should get you three dolphin binaries in the ../Build/Binaries folder. You can add them to your /opt/retropie/configs/gc/emulators.cfg list the way that I have done. Note that I have set up both entries that can launch games from es as well as entries that are intended for settings configuration and that will only take you to the gui screen. If you would like to enable hotkeys, please setup your hotkeys using the hotkey-configure option below and note that in order for hotkeys to work you will need to launch your games using the dolphin-5.0-4544 core (the nogui core does not support hotkeys):

	dolphin-5.0-4544-nogui = "XINIT-WM: /home/pi/dolphin-rpi/dolphin-rpi/Build/Binaries/dolphin-emu-nogui -e %ROM% -u /home/pi/DolphinConfig5.0/"
	dolphin-5.0-4544 = "XINIT-WM: /home/pi/dolphin-rpi/dolphin-rpi/Build/Binaries/dolphin-emu -e %ROM% -u /home/pi/DolphinConfig5.0/"
	dolphin-5.0-4544-configure = "XINIT-WM: /home/pi/dolphin-rpi/dolphin-rpi/Build/Binaries/dolphin-emu -u /home/pi/DolphinConfig5.0/"
	dolphin-5.0-4544-configure-hotkeys = "XINIT-WM: /home/pi/dolphin-rpi/dolphin-rpi/Build/Binaries/dolphin-emu-qt2 -u /home/pi/DolphinConfig5.0/"



Instructions for creating and running m3u files:

Begin by adding the following line to /home/pi/DolphinConfig5.0/Config/Dolphin.ini under the "Core" heading or by checking "Change Discs Automatically" in Config->General Tab in the user interface.

	[Core]
	AutoDiscChange = True

Next, create an m3u file using a text editor such as nano in your /home/pi/RetroPie/roms/gc/ directory with the name of the multi-disc game:

	BatenKaitosEternalWings.m3u

Populate the m3u file with the name of the individual game files similar to as shown below (note: do not use quotations or escape characters in the filename):

	BatenKaitosEternalWingsDisc01.rvz
	BatenKaitosEternalWingsDisc02.rvz

Save and close the file.  You should now be able to open the m3u file in RetroPie using dolphin-rpi-nogui as the core or within the user interface of dolphin-rpi if you are in your Pi's deskop.  Note that m3u files will not show up on the gamelist within the dolphin-rpi gui, but can be accessed by selecting File->Open


# Dolphin - A GameCube and Wii Emulator

[Homepage](https://dolphin-emu.org/) | [Project Site](https://github.com/dolphin-emu/dolphin) | [Forums](https://forums.dolphin-emu.org/) | [Wiki](https://wiki.dolphin-emu.org/) | [Issue Tracker](https://bugs.dolphin-emu.org/projects/emulator/issues) | [Coding Style](https://github.com/dolphin-emu/dolphin/blob/master/Contributing.md) | [Transifex Page](https://www.transifex.com/projects/p/dolphin-emu/)

Dolphin is an emulator for running GameCube and Wii games on Windows,
Linux, macOS, and recent Android devices. It's licensed under the terms
of the GNU General Public License, version 2 or later (GPLv2+).

Please read the [FAQ](https://dolphin-emu.org/docs/faq/) before using Dolphin.

## System Requirements

### Desktop

* OS
    * Windows (7 SP1 or higher is officially supported, but Vista SP2 might also work).
    * Linux.
    * macOS (10.9 Mavericks or higher).
    * Unix-like systems other than Linux are not officially supported but might work.
* Processor
    * A CPU with SSE2 support.
    * A modern CPU (3 GHz and Dual Core, not older than 2008) is highly recommended.
* Graphics
    * A reasonably modern graphics card (Direct3D 10.0 / OpenGL 3.0).
    * A graphics card that supports Direct3D 11 / OpenGL 4.4 is recommended.

### Android

* OS
    * Android (5.0 Lollipop or higher).
* Processor
    * A processor with support for 64-bit applications (either ARMv8 or x86-64).
* Graphics
    * A graphics processor that supports OpenGL ES 3.0 or higher. Performance varies heavily with [driver quality](https://dolphin-emu.org/blog/2013/09/26/dolphin-emulator-and-opengl-drivers-hall-fameshame/).
    * A graphics processor that supports standard desktop OpenGL features is recommended for best performance.

Dolphin can only be installed on devices that satisfy the above requirements. Attempting to install on an unsupported device will fail and display an error message.

## Building for Windows

Use the solution file `Source/dolphin-emu.sln` to build Dolphin on Windows.
Visual Studio 2017 is a hard requirement. Other compilers might be
able to build Dolphin on Windows but have not been tested and are not
recommended to be used. Git and Windows 10 SDK 10.0.15063.0 must be installed when building.

An installer can be created by using the `Installer.nsi` script in the
Installer directory. This will require the Nullsoft Scriptable Install System
(NSIS) to be installed. Creating an installer is not necessary to run Dolphin
since the Binary directory contains a working Dolphin distribution.

## Building for Linux and macOS

Dolphin requires [CMake](http://www.cmake.org/) for systems other than Windows. Many libraries are
bundled with Dolphin and used if they're not installed on your system. CMake
will inform you if a bundled library is used or if you need to install any
missing packages yourself.

### macOS Build Steps:

1. `mkdir build`
2. `cd build`
3. `cmake ..`
4. `make`

An application bundle will be created in `./Binaries`.

### Linux Global Build Steps:

To install to your system.

1. `mkdir build`
2. `cd build`
3. `cmake ..`
4. `make`
5. `sudo make install`

### Linux Local Build Steps:

Useful for development as root access is not required.

1. `mkdir Build`
2. `cd Build`
3. `cmake .. -DLINUX_LOCAL_DEV=true`
4. `make`
5. `ln -s ../../Data/Sys Binaries/`

### Linux Portable Build Steps:

Can be stored on external storage and used on different Linux systems.
Or useful for having multiple distinct Dolphin setups for testing/development/TAS.

1. `mkdir Build`
2. `cd Build`
3. `cmake .. -DLINUX_LOCAL_DEV=true`
4. `make`
5. `cp -r ../Data/Sys/ Binaries/`
6. `touch Binaries/portable.txt`

## Building for Android

These instructions assume familiarity with Android development. If you do not have an
Android dev environment set up, see [AndroidSetup.md](AndroidSetup.md).

If using Android Studio, import the Gradle project located in `./Source/Android`.

Android apps are compiled using a build system called Gradle. Dolphin's native component,
however, is compiled using CMake. The Gradle script will attempt to run a CMake build
automatically while building the Java code.

## Uninstalling

When Dolphin has been installed with the NSIS installer, you can uninstall
Dolphin like any other Windows application.

Linux users can run `cat install_manifest.txt | xargs -d '\n' rm` as root from the build directory
to uninstall Dolphin from their system.

macOS users can simply delete Dolphin.app to uninstall it.

Additionally, you'll want to remove the global user directory (see below to
see where it's stored) if you don't plan to reinstall Dolphin.

## Command Line Usage

`Usage: Dolphin [-h] [-d] [-l] [-e <str>] [-b] [-V <str>] [-A <str>]`

* -h, --help Show this help message
* -d, --debugger Opens the debugger
* -l, --logger Opens the logger
* -e, --exec=<str> Loads the specified file (DOL,ELF,WAD,GCM,ISO)
* -b, --batch Exit Dolphin with emulator
* -V, --video_backend=<str> Specify a video backend
* -A, --audio_emulation=<str> Low level (LLE) or high level (HLE) audio

Available DSP emulation engines are HLE (High Level Emulation) and
LLE (Low Level Emulation). HLE is fast but often less accurate while LLE is
slow but close to perfect. Note that LLE has two submodes (Interpreter and
Recompiler), which cannot be selected from the command line.

Available video backends are "D3D" (only available on Windows) and
"OGL". There's also "Software Renderer", which uses the CPU for rendering and
is intended for debugging purposes only.

## Sys Files

* `wiitdb.txt`: Wii title database from [GameTDB](http://www.gametdb.com)
* `totaldb.dsy`: Database of symbols (for devs only)
* `GC/font_western.bin`: font dumps
* `GC/font_japanese.bin`: font dumps
* `GC/dsp_coef.bin`: DSP dumps
* `GC/dsp_rom.bin`: DSP dumps
* `Wii/clientca.pem`: Wii network certificate
* `Wii/clientcacakey.pem`: Wii network certificate
* `Wii/rootca.pem`: Wii network certificate

The DSP dumps included with Dolphin have been written from scratch and do not
contain any copyrighted material. They should work for most purposes, however
some games implement copy protection by checksumming the dumps. You will need
to dump the DSP files from a console and replace the default dumps if you want
to fix those issues.

Wii network certificates must be extracted from a Wii IOS. A guide for that can be found [here](https://wiki.dolphin-emu.org/index.php?title=Wii_Network_Guide).

## Folder Structure

These folders are installed read-only and should not be changed:

* `GameSettings`: per-game default settings database
* `GC`: DSP and font dumps
* `Maps`: symbol tables (dev only)
* `Shaders`: post-processing shaders
* `Themes`: icon themes for GUI
* `Resources`: icons that are theme-agnostic
* `Wii`: default Wii NAND contents

## Packaging and udev

The Data folder contains a udev rule file for the official GameCube controller
adapter and the Mayflash DolphinBar. Package maintainers can use that file in their packages for Dolphin.
Users compiling Dolphin on Linux can also just copy the file to their udev
rules folder.

## User Folder Structure

A number of user writeable directories are created for caching purposes or for
allowing the user to edit their contents. On macOS and Linux these folders are
stored in `~/Library/Application Support/Dolphin/` and `~/.dolphin-emu`
respectively. On Windows the user directory is stored in the `My Documents`
folder by default, but there are various way to override this behavior:

* Creating a file called `portable.txt` next to the Dolphin executable will
  store the user directory in a local directory called "User" next to the
  Dolphin executable.
* If the registry string value `LocalUserConfig` exists in
  `HKEY_CURRENT_USER/Software/Dolphin Emulator` and has the value **1**,
  Dolphin will always start in portable mode.
* If the registry string value `UserConfigPath` exists in
  `HKEY_CURRENT_USER/Software/Dolphin Emulator`, the user folders will be
  stored in the directory given by that string. The other two methods will be
  prioritized over this setting.

List of user folders:

* `Cache`: used to cache the ISO list
* `Config`: configuration files
* `Dump`: anything dumped from Dolphin
* `GameConfig`: additional settings to be applied per-game
* `GC`: memory cards and system BIOS
* `Load`: custom textures
* `Logs`: logs, if enabled
* `ScreenShots`: screenshots taken via Dolphin
* `StateSaves`: save states
* `Wii`: Wii NAND contents

## Custom Textures

Custom textures have to be placed in the user directory under
`Load/Textures/[GameID]/`. You can find the Game ID by right-clicking a game
in the ISO list and selecting "ISO Properties".
