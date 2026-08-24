![WPBochs logo](http://dverlock.top/wpbochs/wpbochslogo.png)
---
WPBochs is a port of [Bochs](https://bochs.sourceforge.io/), the x86 emulator, for Windows Phone 8.1, bringing the ability to run older versions of Windows, MS-DOS/FreeDOS and Linux to Windows Phones. It features emulating an FPU, Sound Blaster 16 sound card, CD-ROM drive, and much more. You can also create floppy/hard disk images directly inside WPBochs through its Image creator. If you have no images to test WPBochs on, there already is a built-in demo floppy drive with FreeDOS and some freeware games on it.
> **Note: Bochs is not affiliated with Microsoft.**
### Screenshots
| <img src="http://dverlock.top/wpbochs/wpbscreen1.png" alt="Screenshot 1" width="260"> | <img src="http://dverlock.top/wpbochs/wpbscreen2.png" alt="Screenshot 2" width="260"> | <img src="http://dverlock.top/wpbochs/wpbscreen3.png" alt="Screenshot 3" width="260"> |
|:---:|:---:|:---:|

---
### Download
Get the latest version APPX in the [Releases](https://github.com/dverlock/WPBochs/releases) tab. The APPX can be installed through both Windows Phone Application Deployment 8.1 and CMD Injector (you can find guides on installing it on WP8.1/W10M online).

> **Note: WPBochs depends on VCLibs 12.00. Install them before installing WPBochs.**

> **Note: The APPX files provided here are only for ARM processors (physical devices). x86 (emulator-compatible) APPX files are not provided here. You can build one yourself, though. See the [How to build](#how-to-build) section for more information.**

You can also get it from Live Store:

[![Get WPBochs from Live Store](https://edge.live.net.co/images/store/2025_GetButton_SmallBlack.png)](https://store.live.net.co/app/720)

or from my webpage: http://dverlock.top/wpbochs.

---
### How to build
1. Download the source code as a zip archive (green Code button -> Download ZIP), or clone into the repository: `git clone https://github.com/dverlock/WPBochs.git`.
2. Extract the archive to any folder.
3. Open the WPBochs.sln file using Visual Studio 2015 with Windows Phone 8.1 SDK installed.
4. Right-click the WPBochs project in the Solution Explorer and click on Store -> Create App Packages...
5. Select No on the first page, click Next.
6. On the second page, set Generate app bundle to Never, choose the required architecture you want (either x86 or ARM, choose only one), set Solution Configuration to Release and click Create.
7. The final package will be located in `WPBochs\AppPackages\WPBochs_(version)_(architecture)_Test\WPBochs_(version)_(architecture).appx`.

---
### Full feature list
 - FPU emulation
 - Mouse emulation
 - Sound Blaster 16 emulation (experimental)
 - Floppy, hard drive and CD-ROM emulation
 - i440fx emulation (experimental)
 - ACPI emulation (extremely experimental and limited)
 - Creating hard/floppy disk images inside WPBochs
 - Slowdown timer (throttles internal clock to real-time speed)
---
### Known issues
 - Opening/closing in-app log freezes the whole app for a couple of seconds on lower-end devices

---
### FAQ
#### I can't install WPBochs, it fails with an error. How to fix it?
Try installing VCLibs 12.00. You can find it online or download directly on [my webpage](http://dverlock.top/wpbochs/vclibs12.appx). If you still can't install WPBochs, check if you are running WP8.1 or higher. WPBochs is not installable on Windows Phone 8.0 or 7.x.

If you're trying to install WPBochs on Windows 10 Mobile, it will fail with an error about an untrusted certificate. Because of that, on Windows 10 Mobile WPBochs could only be installed through CMD Injector.

> **Note: WPBochs was not tested under Windows 10 Mobile.**
#### Why is emulation so slow?
Bochs itself is a *pure software interpreter* of x86 code, which can be very slow depending on what architecture and clock speed of the host CPU you are using. In Windows Phone's case, it's an ARM processor with usually a low clock speed, so Bochs will run at about a speed of a Pentium 1 in the best-case scenario.
#### Bochs hangs on a black screen at startup. How to fix this?
There might be two solutions to your problem:
 - **Turn off ACPI emulation.** At this moment its emulation is extremely limited and some ACPI-based systems hang and refuse to boot while trying to access ACPI features.
 - **Lower the memory amount.**  If you set the memory amount near or at WPBochs' memory limit (set by Windows Phone's system itself), Bochs will hang on startup because of failing to allocate required memory.
#### What OSes can I run inside WPBochs?
As far as my tests went, FreeDOS, MS-DOS 5.0, Windows 3.1, Windows 95, Windows XP and DeLi Linux successfully booted and worked in WPBochs.
#### Why make such an app?
You might already know that x86 emulators have been ported everywhere (macOS, Linux, Android, iOS etc.). After noticing that there wasn't any x86 emulator available for Windows Phones (except DOSBox, but it's limited to only DOS apps and not full x86 emulation), I decided to take the port of Bochs myself.
#### I still have some questions/need troubleshooting, or I want to request a feature. Where can I get help/in touch with the developer?
You can always open an issue [here](https://github.com/dverlock/WPBochs/issues). I will try my best to respond as quickly as possible.

---
### Credits
Thanks to:

**The Bochs Project (MandrakeSoft)** - for the main emulator;

**The v86 project (https://copy.sh/v86)** - for the source of the FreeDOS floppy used as a demo image in WPBochs;

**dverlock (me)** - for porting Bochs to Windows Phone;

**You - for using WPBochs.**
