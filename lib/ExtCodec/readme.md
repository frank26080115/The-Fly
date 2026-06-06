## Configuration

ESP32 outputs MCLK at: 16000 Hz * 512 = 8.192 MHz

SGTL5000 is configured with CHIP_CLK_CTRL = 0x0010

 * SYS_FS = 32 kHz
 * RATE_MODE = 1/2, so actual audio sample rate is 16 kHz
 * MCLK_FREQ = 0, meaning 256 * SYS_FS
 * MCLK = 256 * 32 kHz = 8.192 MHz

CHIP_I2S_CTRL = 0x0130, so BCLK is 32 * Fs = 512 kHz for 16 kHz 16-bit stereo

## Operational Model

The external codec firmware module has these states:

 * EXTCODEC_UNAVAIL,
 * EXTCODEC_NO_EARBUD,
 * EXTCODEC_YES_EARBUD,
 * EXTCODEC_YES_EARBUD_WITH_MIC,

### Push-To-Talk

Without the external codec, the SGTL5000, we have the NS4168 which drives the speaker, and the SPM1423 microphone. These two share the same I2S signal pins but the NS4168 uses standard signalling and the SPM1423 uses PDM signalling, so they cannot work simultaneously. Thus, a push-to-talk scheme is used, when the mic is activated, the I2S is configured for PDM, and when the mic is off, the I2S configuration switches to standard.

With the external codec, we are currently unsure if the NS4168 can share a bus with the SGTL5000. In the case if it cannot, then we will continue to implement push-to-talk.

Under `EXTCODEC_NO_EARBUD`: when mic is enabled, configure I2S for SGTL5000 using Line-In, when mic is disabled, configure I2S for NS4168

Under `EXTCODEC_YES_EARBUD`: use full-duplex mode

Under `EXTCODEC_YES_EARBUD_WITH_MIC`: use full-duplex mode

### Full-Duplex

If we find that the external codec, the SGTL5000, can share the I2S bus with the NS4168, then we do not need to implement push-to-talk. Always configure the I2S in the same way that works for both the SGTL5000 and NS4168.

Under `EXTCODEC_NO_EARBUD`: power on the NS4168 via the PMIC

Under `EXTCODEC_YES_EARBUD`: power off the NS4168 via the PMIC, mic source is Line-In-Right

Under `EXTCODEC_YES_EARBUD_WITH_MIC`: power off the NS4168 via the PMIC, mic source is Mic-In

Volume is done via software
