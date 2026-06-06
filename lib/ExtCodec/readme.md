ESP32 outputs MCLK at: 16000 Hz * 512 = 8.192 MHz

SGTL5000 is configured with CHIP_CLK_CTRL = 0x0010

 * SYS_FS = 32 kHz
 * RATE_MODE = 1/2, so actual audio sample rate is 16 kHz
 * MCLK_FREQ = 0, meaning 256 * SYS_FS
 * MCLK = 256 * 32 kHz = 8.192 MHz

CHIP_I2S_CTRL = 0x0130, so BCLK is 32 * Fs = 512 kHz for 16 kHz 16-bit stereo
