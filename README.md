# C64 Emulator

## Overview
This is a Commodore C64 emulator based on the Vice 64 emulator (currently v2.3)
The Emulator part is based on the app C64 Scene 1.2 (see Credits below).
Its been fairly stripped back and is more based around my requirements interface wise.
But it supports a single MFI Controller now (see below)

## Installing


As its written using the current Swift 3, you will need XCode 8 (currently Beta 6).
I'm also using Cocoapods to manage the dependancies

Clone or download this repository

Install the dependancies by running ```pod install```

Open C64Emulator.xcworkspace and build and run
  

## Basic usage instructions
On start, you'll be presented with an empty view controller saying no programs currently cataloged.  If you switch to Disks view  you'll see no disaks avaialble.
You need to either upload a disk image (tapes coming soon) by tapping 'Upload', or press 'Start'.

If you tap Start, the emulator will launch in a clean state and you can do stuff.

If you tap Upload, you can then upload d64 files using your browser (follow the on screen instructions).

Once you've uploaded some disks, the disk view will show the disks loaded BUT the Programs view will still be empty. This is because disk images contain lots of stuff - not just programs.  You need to select the disk (from the disk tab) and mark the files that can be loaded. these will then appear in the Programs view.

Tapping on a program will start the emulator and auto load and run the selected program.

Tapping Start on a Disk will start the emulator and attach the disk to drive 8.


Once the emulator is started, tap on the screen to bring up the menu bars.  
Here you have:  
 - Reset - Resets the emulator  
 - Pause - pauses the emulator - press again to resume  
 - Turbo mode - toggles turbo mode on and off  
 - Joystick - press to switch between off, joystick 1, and joystick 2  
 - Keyboard - press to switch into keyboard mode. **note** - the top keyboard bar can be swiped left and right tio show different command keys.  
 - Settings - show the settings menu (and attach different disks)  
 
## MFI Controller Support
Now has initial MFI controller support (single controller only at the moment).<br>
Uses the Digital pad and Button A for fire button</br>
Use the Right Shoulder Button to switch between Joystick Disabled, Port 1 and Port 2 (or tap on toolbar joystick icon)

## Troubleshooting
Sometimes, on a restart, you will be shown a black screen. Why this happens - I'm still trying to figure out, however you can fix it by simply tapping on the screen to bring up the menu and tapping on the Fast Forward icon twice (once to switch it on and once to switch it off).  that seems to cause the emulator to then start nicely!  Hopefully this will be fixed soon. 

## Why?
I had a C64 waaaaay back in time (and a Pet before that) and its played a huge part in my life.   
I've been in to C64 emulators probably for over 15 years now but have never deleved into how it works.  
This is my way of doing that!  

## Licence
The licence is the GPL V2 licence. I'd prefer a a slightly different licence however we are where we are. See COPYING for the licence details.  

## Credits
This wouldn't be at all possible without the sterling work done by the developers of VICE - an awesome C64 Emulator - http://vice-emu.sourceforge.net/

The bulk of the effort of porting Vice over to iOS was done by Mr. SID and is this work is heavily based on his app - C64 Scene 1.2 which can be found at: 
http://csdb.dk/release/?id=141613&show=summary

In addition, I'm making use of a couple of fantastic libraries:  
 - FMDB - A nice wrapper around the SQLite library  
 - GCDWebServer - A lovely little library that enables uploading of files from a web browser  
