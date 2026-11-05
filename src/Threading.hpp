// SPDX-License-Identifier: MIT OR Apache-2.0
// SPDX-FileCopyrightText: (c) 2026 Eiren Rain and SlimeVR Contributors

#include <string>

namespace Threading {

/**
 * @brief Set the name of the current thread. This is not guaranteed to have an effect on some systems.
 *
 * @param name new name of the thread, should be less than 16 characters otherwise it may be cut off.
 */
void SetThisThreadName(std::string name);

} // namespace Threading