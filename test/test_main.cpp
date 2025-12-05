/* Copyright (C) 2023 - 2025 Gleb Bezborodov - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the MIT license.
 *
 * You should have received a copy of the MIT license with
 * this file. If not, please write to: bezborodoff.gleb@gmail.com, or visit : https://github.com/glensand/hope-io
 */

#include <gtest/gtest.h>
#include "hope-io/net/init.h"
#include <thread>
#include <chrono>

// Global test setup/teardown
class HopeIoTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        hope::io::init();
    }

    void TearDown() override {
        hope::io::deinit();
    }
};

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Register global test environment
    ::testing::AddGlobalTestEnvironment(new HopeIoTestEnvironment);
    
    return RUN_ALL_TESTS();
}

