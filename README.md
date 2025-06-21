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

Clone the repo with `git clone --recurse-submodules https://github.com/SlimeVR/SlimeVR-OpenVR-Driver` to clone with all libraries and [vcpkg](https://vcpkg.io/en/getting-started.html) registry.

To build the project with VSCode on Windows you need to install [VS Build Tools](https://visualstudio.microsoft.com/downloads/).

Run the bootstrap script to build vcpkg binary `.\vcpkg\bootstrap-vcpkg.bat` or `./vcpkg/bootstrap-vcpkg.sh`.

After installing vcpkg if you're on Windows, you need to run `vcpkg integrate install` command from the vcpkg folder to integrate it for VSCode.

Then you will need to run `cmake -B build` once to setup CMake.

Finally, everytime you want to build binaries, just run `cmake --build build --config Release`. The newly built binary will be in the build/release directory.

For other systems and IDEs, instructions are not available as of now, but contributions are welcome.

### Updating vcpkg packages

To update vcpkg packages set the vcpkg registry submodule to a newer commit and rerun the bootstrap script.

### Updating protobuf messages

You can modify the protobuf messages for communication between the server and this driver at [src\bridge\ProtobufMessages.proto](src\bridge\ProtobufMessages.proto)

To update the protobuf messages on the server, you will need to install [protoc](https://protobuf.dev/installation/) (we use version 4.31.1), then make sure that your SlimeVR-Server and SlimeVR-OpenVR-Driver repositories are in the same parent directory and run `protobuf_update.bat` located at `SlimeVR-Server\server\desktop`