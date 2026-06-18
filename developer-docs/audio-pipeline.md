# Audio Pipeline

This is a generalized view of the runtime audio flow. It ignores the optional external codec detail and models the
hardware side as one microphone input, one speaker output, and one Bluetooth audio input.

## Recording

```mermaid
flowchart LR
    recorder_file["Recording file<br/>microSD"]

    recorder["AudioFileRecorder::pump()"]
    encode["PCM/WAV/MP3<br/>recording writer"]

    subgraph mic_section["Mic section"]
        direction LR
        mic_in["Mic input<br/>I2S RX"]
        pump_mic["AudioManager::pump_mic2bt()"]
        hpf["MicGainManager.cpp<br/>High-pass filter"]
        agc["MicGainManager.cpp<br/>AGC"]
        mic2bt["g_fifo_mic2bt<br/>micToBluetoothFifo()"]
        mic2file["g_fifo_mic2file<br/>micToFileFifo()"]
        bt_out["Bluetooth output<br/>HFP outgoing PCM"]

        mic_in --> pump_mic
        pump_mic --> hpf
        hpf --> agc
        agc --> mic2bt
        agc --> mic2file
        mic2bt --> bt_out
    end

    subgraph bluetooth_section["Bluetooth section"]
        direction LR
        bt_in["Bluetooth input<br/>HFP incoming PCM"]
        queue_bt["AudioManager::queue_hfp_pcm()"]
        bt2spk["g_fifo_bt2spk<br/>bluetoothToSpeakerFifo()"]
        bt2file["g_fifo_bt2file<br/>bluetoothToFileFifo()"]
        pump_spk["AudioManager::pump_bt2spk()"]
        speaker_gain["AudioManager::apply_speaker_software_volume()"]
        speaker_out["Speaker output<br/>I2S TX"]

        bt_in --> queue_bt
        queue_bt --> bt2spk
        queue_bt --> bt2file
        bt2spk --> pump_spk
        pump_spk --> speaker_gain
        speaker_gain --> speaker_out
    end

    bt2file --> recorder
    mic2file --> recorder
    recorder --> encode
    encode --> recorder_file
```

## Playback

```mermaid
flowchart LR
    playback_file["Playback file<br/>microSD"]
    decoder["WavPlayback / Mp3Playback<br/>read and decode"]
    bt2spk["g_fifo_bt2spk<br/>bluetoothToSpeakerFifo()"]
    pump_spk["AudioManager::pump_bt2spk()"]
    speaker_gain["AudioManager::apply_speaker_software_volume()"]
    speaker_out["Speaker output<br/>I2S TX"]

    playback_file --> decoder
    decoder --> bt2spk
    bt2spk --> pump_spk
    pump_spk --> speaker_gain
    speaker_gain --> speaker_out
```

## Electrical

```mermaid
flowchart LR
    mic["Electret Mic"]
    hp["Headphones"]
    hp_mic["Headphone Inline Mic"]
    max4466["MAX4466 Preamp"]

    subgraph sgtl5000["SGTL5000 Codec"]
        direction LR
        line_in["Line Input"]
        mic_amp["Mic Amp"]
        adc_amp["ADC Amp"]
        adc["ADC"]
        dap["Digital Audio Processor"]
        mux_in["Input Mux"]
        headphone_out["Headphone Out<br/>DAC & Amp"]
        switch_out["Output Mux"]

        switch_out --> headphone_out
        mic_amp --> mux_in
        mux_in --> adc_amp
        adc_amp --> adc
        adc --> dap
    end

    subgraph m5stack["M5Stack"]
        direction LR
        i2s["I2S (audio data)"]
        i2c["I2C (control)"]
        gpio["GPIO"]
        ns4168["NS4168 internal speaker"]
        pmic["PMIC"]

        i2c --> pmic
        pmic --power--> ns4168
        i2s --> ns4168
    end

    dap --> i2s
    i2s --> switch_out
    mic --> max4466
    max4466 --line-in--> mux_in
    headphone_out --> hp
    hp_mic --mic--> mic_amp
    i2c --> sgtl5000
    hp_mic --detect--> gpio
    hp --detect--> gpio
```