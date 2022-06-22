# SlimeVR-OpenVR-Driver
SlimeVR driver for OpenVR. Used to communicate between the SlimeVR server and SteamVR.

## How to get

It's recommended to install the driver via the SlimeVR installer here: https://github.com/SlimeVR/SlimeVR-Installer/releases/latest/download/slimevr_web_installer.exe

Alternatively, you can also manually install it by following one of the two methods below.

### Option one

Downloaded the latest release here: https://github.com/SlimeVR/SlimeVR-OpenVR-Driver/releases/latest/download/slimevr-openvr-driver-win64.zip.
Copy the `slimevr` folder in your SteamVR folder, usually it's located in `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers`. This should look like this:

![img](https://eiren.cat/SQpk)  

### Option two

Edit file `C:\Users\<Username>\AppData\Local\openvr\openvrpaths.vrpath`, add `"Path\\to\\slimevr",` to the list of external_drivers there, like this:

![img](https://eiren.cat/ib4_)  
*Don't forget to double backwards slashes!*

## Contributions

By contributing to this project you are placing all your code under MIT or less restricting licenses, and you certify that the code you have used is compatible with those licenses or is authored by you. If you're doing so on your work time, you certify that your employer is okay with this.
