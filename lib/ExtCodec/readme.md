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

### Earbud Detection

There is a switch on the "tip" of the TRRS jack. That signal is pulled-up to 3.3V. The ADC capable pin GPIO35 is connected to it. When the earbud is connected, then the switch is open, and the signal will go to a voltage very close to 3.3V

To sense the presence of an inline mic on the earbud, there is a pull-up resistor on the mic-bias signal. If there is no inline microphone on the earbud, then the ground and mic signal are both one continuous sleeve on the connector, a reading on the mic signal would result in a voltage very close to 0V. If an inline mic is available, then the voltage on that signal will be some mid-ranged voltage depending on the mic impedance and the mic-bias. This is sensed by an ADC capable GPIO pin GPIO36.

### Requirements for I2S Bus Sharing

In the ideal case, the NS4168 and SGTL5000 can share the same I2S bus

The first challenge is whether or not the BCLK signal can be duplicated to two pins instead of just one, because the extension port does not expose the NS4168's BCLK pin on GPIO12, so the SGTL5000 needs another BCLK pin, which our PCB layout uses GPIO13 for. BCLK must be driven on both GPIO12 and GPIO13 in the same active I2S configuration.

The second challenge is simply making sure the I2S bus configurations can match

NS4168 I2S configuration code (this is known to be working):

```
    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config   = {};
    config.clk_cfg            = I2S_STD_CLK_DEFAULT_CONFIG(sampleRateHz);
    config.slot_cfg           = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    config.slot_cfg.ws_width  = 16;
```

SGTL5000 I2S configuration code

```
    i2s_chan_config_t chan_config = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_config.dma_desc_num      = kDmaBufferCount;
    chan_config.dma_frame_num     = kPumpSamples;
    chan_config.auto_clear        = true;

    i2s_std_config_t config      = {};
    config.clk_cfg               = I2S_STD_CLK_DEFAULT_CONFIG(sampleRateHz);
    config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_512;
    config.slot_cfg              = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    config.slot_cfg.slot_mask    = I2S_STD_SLOT_BOTH;
    config.slot_cfg.ws_width     = 16;
```

The conclusion is that bus sharing is likely possible

Testing showed BCLK signal is successfully output through GPIO13

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

### Initialization Sequence

In the ideal case, when the I2S bus can be shared by both the NS4168 and SGTL5000, then the I2S needs to be initialized at boot and never reconfigured again. Then the I2C setup for the SGTL5000 can happen. This is because the only pins usable for the MCLK are the UART pins so we need to disable UART completely before the SGTL5000 is initialized.
