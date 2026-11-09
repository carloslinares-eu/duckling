/**
 * @file test_receiver.cpp
 * @brief Hardware-free tests for io::Receiver helpers (task 1.3, issue #5).
 */
#include <doctest/doctest.h>

#include "io/receiver.hpp"

using duckling::io::collectionKey;

TEST_CASE("collectionKey pairs the two Windows collections of one interface") {
    // Task 1.3 probe (W1) on DESKTOP-CARLOS.
    const char* shortPath =
        R"(\\?\HID#VID_046D&PID_C548&MI_02&Col01#7&85d146e&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})";
    const char* longPath =
        R"(\\?\HID#VID_046D&PID_C548&MI_02&Col02#7&85d146e&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030})";
    CHECK(collectionKey(shortPath) == collectionKey(longPath));
    CHECK(collectionKey(shortPath) ==
          "hid#vid_046d&pid_c548&mi_02#7&85d146e&0#{4d1e55b2-f16f-11cf-88cb-001111000030}");
    // C4: lower-case spelling of the same path gives the same key.
    CHECK(collectionKey(
              R"(\\?\hid#vid_046d&pid_c548&mi_02&col02#7&85d146e&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030})") ==
          collectionKey(longPath));
}

TEST_CASE("collectionKey keeps different interfaces and receivers apart") {
    // C4: interface 1 (keyboard collections) of the same receiver.
    CHECK(collectionKey(
              R"(\\?\hid#vid_046d&pid_c548&mi_01&col01#7&1b7a2954&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})") !=
          collectionKey(
              R"(\\?\hid#vid_046d&pid_c548&mi_02&col01#7&85d146e&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})"));
    // A second receiver: other instance id.
    CHECK(collectionKey(
              R"(\\?\HID#VID_046D&PID_C548&MI_02&Col01#7&2a0b33c&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})") !=
          collectionKey(
              R"(\\?\HID#VID_046D&PID_C548&MI_02&Col02#7&85d146e&0&0001#{4d1e55b2-f16f-11cf-88cb-001111000030})"));
}

TEST_CASE("collectionKey leaves Linux hidraw paths alone") {
    CHECK(collectionKey("/dev/hidraw2") == "/dev/hidraw2");
    CHECK(collectionKey("/dev/hidraw2") != collectionKey("/dev/hidraw3"));
}
