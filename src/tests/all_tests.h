#pragma once

#include <stdint.h>
#include <stdbool.h>

// function prototypes for all test functions, so that main.cpp doesn't get littered with func prototypes

extern void run_test();
extern void test_sdcard();
extern void test_speaker();
extern void test_micrec();
extern void test_screen();
extern void test_btpairing();
extern void test_btspeakerphone();
extern void test_pngdecode();
