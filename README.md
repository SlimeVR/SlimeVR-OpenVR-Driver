# SlimeVR-OpenVR-Driver
SlimeVR driver for OpenVR

## Installation

Downloaded the latest release, and use one of two options to install the driver (in the future Server will do it for you).

### Option one

Copy the `slimevr` folder in your SteamVR folder, usually it's located in `C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers`. This should look like this:

![img](https://eiren.cat/SQpk)

### Option two

Edit file `C:\Users\<Username>\AppData\Local\openvr\openvrpaths.vrpath`, add `"Path\\to\\slimevr",` to the list of external_drivers there, like this:

![img](https://eiren.cat/ib4_)
*Don't forget to double backwards slashes!*

## Building On Linux

Note: Only for Arch 

Dependencies needed:
    vcpkg(Aur),
    cmake

Note: On others

Dependencies Needed:
    git,
    g++

Run `git clone https://github.com/Microsoft/vcpkg.git`

Run `./vcpkg/bootstrap-vcpkg.sh`

### Step One

Go to the root of the project and `mkdir build`

### Step Two

In the terminal run `vcpkg install protobuf`

### Step Three

Run `cmake -B build/ -S . -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake` in the terminal

### Step Four

Run `cmake --build build/`

Should finish building if it doesn't ask for help in the discord
https://discord.gg/SlimeVR

## Contributions

By contributing to this project you are placing all your code under MIT or less restricting licenses, and you certify that the code you have used is compatible with those licenses or is authored by you. If you're doing so on your work time, you certify that your employer is okay with this.
