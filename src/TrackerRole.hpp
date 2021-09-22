/*
    SlimeVR Code is placed under the MIT license
    Copyright (c) 2021 Eiren Rain

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
    THE SOFTWARE.
*/
#pragma once

#include <string>
#include "DeviceType.hpp"

enum TrackerRole {
    NONE = 0,
    WAIST = 1,
    LEFT_FOOT = 2,
    RIGHT_FOOT = 3,
    CHEST = 4,
    LEFT_KNEE = 5,
    RIGHT_KNEE = 6,
    LEFT_ELBOW = 7,
    RIGHT_ELBOW = 8,
    LEFT_SHOULDER = 9,
    RIGHT_SHOULDER = 10,
    LEFT_HAND = 11,
    RIGHT_HAND = 12,
    LEFT_CONTROLLER = 13,
    RIGHT_CONTROLLER = 14,
    HEAD = 15,
    NECK = 16,
    CAMERA = 17,
    KEYBOARD = 18,
    HMD = 19,
    BEACON = 20,
    GENERIC_CONTROLLER = 21,
};

std::string getViveRoleHint(TrackerRole role);

std::string getViveRole(TrackerRole role);

DeviceType getDeviceType(TrackerRole role);
