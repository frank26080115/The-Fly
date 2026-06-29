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
extern void test_micfilterperformance();
extern void test_micfilterresult();
extern void test_imu();
extern void test_bootreadfiles();
extern void test_fonts();
extern void test_pmic();
extern void test_webserver();
extern void test_btramusage();
extern void test_nvsloadsave();
extern void test_filelistbenchmark();
extern void test_pbkdfbenchmark();
extern void test_mp3encode();
extern void test_rtcvsmillis();
extern void test_hapticfeedback();
extern void test_ledcpwm();
extern void test_mcpwm();
extern void test_i2cscan();
extern void test_sgtl5000_pll_lock();
extern void test_continuous_sine();
extern void test_noisespectrumanalyzer();
extern void test_extcodec_adc();
extern void test_allmicgains();
extern void test_batteryburn();
