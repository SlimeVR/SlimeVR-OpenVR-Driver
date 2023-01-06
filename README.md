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
Any contributions submitted for inclusion in this repository will be dual-licensed under
either:

- MIT License ([LICENSE-MIT](/LICENSE-MIT))
- Apache License, Version 2.0 ([LICENSE-APACHE](/LICENSE-APACHE))

Unless you explicitly state otherwise, any contribution intentionally submitted for
inclusion in the work by you, as defined in the Apache-2.0 license, shall be dual
licensed as above, without any additional terms or conditions.

You also certify that the code you have used is compatible with those licenses or is
authored by you. If you're doing so on your work time, you certify that your employer is
okay with this and that you are authorized to provide the above licenses.

### Building

To build the project with VSCode you need to install two things: [vcpkg](https://vcpkg.io/en/getting-started.html) and [VS Build Tools](https://visualstudio.microsoft.com/downloads/).

After installing vcpkg if you're on Windows, you need to run `vcpkg integrate install` command from the vcpkg folder to integrate it for VSCode.

For other systems and IDEs instructions are not available as of now, contributions are welcome.
