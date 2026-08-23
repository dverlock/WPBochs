# WPBochs
![WPBochs logo](http://dverlock.top/wpbochs/wpbochslogo.png)

WPBochs is a port of [Bochs](https://bochs.sourceforge.io/), the x86 emulator, for Windows Phone 8.1, bringing the ability to run older Windows, MS-/FreeDOS and Linux. It features emulating an FPU, Sound Blaster 16 sound card, CDROM, and much more. You can also create floppy/hard disk images directly inside WPBochs through its Image creator. If you have no images to test WPBochs on, there already is a built-in demo floppy drive with FreeDOS and some freeware games on it.
### Screenshots
<img src="http://dverlock.top/wpbochs/wpbscreen1.png" alt="Screenshot 1" width="30%">

<img src="http://dverlock.top/wpbochs/wpbscreen2.png" alt="Screenshot 2" width="30%">

<img src="http://dverlock.top/wpbochs/wpbscreen3.png" alt="Screenshot 3" width="30%">

---
### Download
Get the latest version APPX in the Releases tab. It can be installed from both Windows Phone Application Deployment and CMD Injector.

**Please note that WPBochs depends on VCLibs 12.00. Install them before installing WPBochs.**

You can also get it from Live Store:

[![Get WPBochs from Live Store](https://edge.live.net.co/images/store/2025_GetButton_SmallBlack.png)](https://store.live.net.co/app/720)

or from my webpage: http://dverlock.top/wpbochs.

---
### How to build
Download the source code as a zip or a tar.gz file, extract it to any folder and open the WPBochs.sln file using Visual Studio 2015 with Windows Phone 8.1 SDK installed.

---
### Full feature list
 - FPU emulation
 - Mouse emulation
 - Sound Blaster 16 emulation (experimental)
 - Floppy, hard drive and CDROM emulation
 - i440fx emulation (experimental)
 - ACPI emulation (extremely experimental and limited)
 - Creating hard/floppy disk images inside the app
 - Slowdown timer (throttles clock to real-time speed)
---
### FAQ
#### I can't install the app, it fails with an error. How to fix it?
Try installing VCLibs 12.00. You can find it online or download directly on [my webpage](http://dverlock.top/wpbochs/vclibs12.appx). If you still can't install it, check if you are running WP8.1 or higher*. WPBochs is not installable on Windows Phone 8.0 or 7.x.

***WPBochs was not tested under Windows 10 Mobile.**
#### Why is emulation so slow?
Bochs itself is a *pure interpreter* of x86 code, which can be very slow depending on what architecture and clock speed of the host CPU you are using. In Windows Phone's case, it's an ARM processor with usually a low clock speed, so Bochs will run at about a speed of a Pentium 1 in a best case scenario.
#### Bochs is hanging on a black screen at startup. How to fix this?
There might be two solutions to your problem:
 - **Turn off ACPI emulation.** At this moment its emulation is extremely limited and some ACPI-based systems hang and refuse to boot while trying to access ACPI.
 - **Lower the memory amount.**  If you set the memory amount near or at WPBochs' memory limit (set by Windows Phone itself), Bochs will hang on startup because of failing to allocate required memory.
#### What OSes can I run inside WPBochs?
As far as my tests went, FreeDOS, MS-DOS 5.0, Windows 3.1, Windows 95, Windows XP and DeLi Linux successfully booted in WPBochs.
#### I still have some questions/need troubleshooting, or I want to request a feature. Where can I get help/in touch with the developer?
You can open an issue [here](https://github.com/dverlock/WPBochs/issues). I will try my best to respond as quickly as possible.

---
### Credits
Thanks to:
**The Bochs Project (MandrakeSoft)** - for the main emulator;

**The v86 project (copy.sh/v86)** - for the source of the FreeDOS floppy used as a demo image in WPBochs;

**dverlock (me)** - for porting Bochs to Windows Phone;

**You - for using WPBochs.**
