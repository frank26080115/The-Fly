Test date 2026-06-03

configuration is

```
AudioFifo g_fifo_bt2spk  (AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(100));
AudioFifo g_fifo_mic2bt  (AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(100));
AudioFifo g_fifo_bt2file (AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(50));
AudioFifo g_fifo_mic2file(AUDIOFIFO_MS_TO_SAMPLES_16K(500), AUDIOFIFO_MS_TO_SAMPLES_16K(50));
constexpr size_t   kHfpOutgoingNotifyMinSamples   = AUDIOFIFO_MS_TO_SAMPLES_16K(50);
with MP3 compression, no encryption
```

log:

```
diag time=14:33:05 stack_free_min=4560 heap_free_min=4025224 core0_hz=1000.0 core1_hz=713.0 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:06 stack_free_min=4560 heap_free_min=4025224 core0_hz=1000.0 core1_hz=719.3 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:07 stack_free_min=4560 heap_free_min=4025224 core0_hz=1000.0 core1_hz=720.3 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:08 stack_free_min=4560 heap_free_min=4025224 core0_hz=1000.0 core1_hz=719.3 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:09 stack_free_min=4560 heap_free_min=4025224 core0_hz=990.0 core1_hz=430.6 fps=2.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:11 stack_free_min=4560 heap_free_min=4025224 core0_hz=780.3 core1_hz=454.3 fps=3.1 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:12 stack_free_min=4560 heap_free_min=4025224 core0_hz=999.0 core1_hz=698.3 fps=60.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:13 stack_free_min=4560 heap_free_min=4025224 core0_hz=963.0 core1_hz=682.0 fps=61.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:14 stack_free_min=4560 heap_free_min=4025224 core0_hz=764.7 core1_hz=408.8 fps=51.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15058.8 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:15 stack_free_min=4560 heap_free_min=4025224 core0_hz=734.0 core1_hz=324.0 fps=47.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16200.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:16 stack_free_min=4560 heap_free_min=4025224 core0_hz=743.0 core1_hz=237.0 fps=36.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15720.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:17 stack_free_min=4560 heap_free_min=4025224 core0_hz=745.5 core1_hz=283.4 fps=40.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16047.9 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:18 stack_free_min=4560 heap_free_min=4025224 core0_hz=771.4 core1_hz=337.0 fps=47.7 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15268.4 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:19 stack_free_min=4560 heap_free_min=4025224 core0_hz=760.0 core1_hz=341.0 fps=47.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15600.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:20 stack_free_min=4560 heap_free_min=4025224 core0_hz=755.0 core1_hz=320.0 fps=46.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=17280.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:21 stack_free_min=4560 heap_free_min=4025224 core0_hz=765.9 core1_hz=289.8 fps=39.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15537.8 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:22 stack_free_min=4560 heap_free_min=4025224 core0_hz=756.2 core1_hz=300.7 fps=42.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15584.4 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:23 stack_free_min=4560 heap_free_min=4025224 core0_hz=759.0 core1_hz=292.8 fps=41.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15537.8 fifo_over_hz=0.0 fifo_under_hz=0.0
E (669173) BT_APPL: bta_dm_pm_btm_status hci_status=32
diag time=14:33:24 stack_free_min=4560 heap_free_min=4025224 core0_hz=742.0 core1_hz=312.7 fps=41.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=17211.2 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:25 stack_free_min=4560 heap_free_min=4025224 core0_hz=762.8 core1_hz=310.0 fps=45.3 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15236.2 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:26 stack_free_min=4560 heap_free_min=4025224 core0_hz=755.4 core1_hz=291.7 fps=44.2 long_write_hz=1.0 i2s_in_sps=0.0 i2s_out_sps=16267.2 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:27 stack_free_min=4560 heap_free_min=4025224 core0_hz=754.0 core1_hz=297.0 fps=45.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16440.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:28 stack_free_min=4560 heap_free_min=4025224 core0_hz=754.0 core1_hz=319.7 fps=44.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16015.9 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:29 stack_free_min=4560 heap_free_min=4025224 core0_hz=761.0 core1_hz=290.8 fps=40.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15298.8 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:30 stack_free_min=4560 heap_free_min=4025224 core0_hz=762.9 core1_hz=381.0 fps=49.6 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15595.2 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:31 stack_free_min=4560 heap_free_min=4025224 core0_hz=769.0 core1_hz=392.0 fps=50.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=17040.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:32 stack_free_min=4560 heap_free_min=4025224 core0_hz=770.0 core1_hz=384.0 fps=50.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15600.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:33 stack_free_min=4560 heap_free_min=4025224 core0_hz=766.2 core1_hz=384.6 fps=51.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15584.4 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:34 stack_free_min=4560 heap_free_min=4025224 core0_hz=763.0 core1_hz=380.0 fps=50.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15600.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:35 stack_free_min=4560 heap_free_min=4025224 core0_hz=757.7 core1_hz=305.9 fps=39.7 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=17279.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:36 stack_free_min=4560 heap_free_min=4025224 core0_hz=756.0 core1_hz=311.8 fps=45.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15298.8 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:37 stack_free_min=4560 heap_free_min=4025216 core0_hz=535.0 core1_hz=122.0 fps=29.0 long_write_hz=0.0 i2s_in_sps=12960.0 i2s_out_sps=3600.0 fifo_over_hz=0.0 fifo_under_hz=7.0
diag time=14:33:38 stack_free_min=4560 heap_free_min=4025216 core0_hz=505.5 core1_hz=187.8 fps=39.0 long_write_hz=0.0 i2s_in_sps=16063.9 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.0
diag time=14:33:39 stack_free_min=4560 heap_free_min=4025216 core0_hz=484.6 core1_hz=158.6 fps=34.7 long_write_hz=0.0 i2s_in_sps=15936.6 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.9
diag time=14:33:40 stack_free_min=4560 heap_free_min=4025216 core0_hz=502.0 core1_hz=196.3 fps=37.1 long_write_hz=0.0 i2s_in_sps=15937.5 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:41 stack_free_min=4560 heap_free_min=4025216 core0_hz=499.5 core1_hz=161.3 fps=35.4 long_write_hz=0.0 i2s_in_sps=16047.2 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=8.8
diag time=14:33:42 stack_free_min=4560 heap_free_min=4025216 core0_hz=510.3 core1_hz=192.6 fps=36.2 long_write_hz=0.0 i2s_in_sps=15953.1 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:43 stack_free_min=4560 heap_free_min=4025216 core0_hz=521.0 core1_hz=191.6 fps=36.2 long_write_hz=0.0 i2s_in_sps=16187.7 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=8.8
diag time=14:33:44 stack_free_min=4560 heap_free_min=4025216 core0_hz=491.1 core1_hz=152.2 fps=35.6 long_write_hz=0.0 i2s_in_sps=15889.3 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=8.9
diag time=14:33:45 stack_free_min=4560 heap_free_min=4025216 core0_hz=493.1 core1_hz=202.9 fps=34.3 long_write_hz=0.0 i2s_in_sps=16000.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:46 stack_free_min=4560 heap_free_min=4025216 core0_hz=550.0 core1_hz=253.9 fps=38.2 long_write_hz=0.0 i2s_in_sps=16000.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:47 stack_free_min=4560 heap_free_min=4025216 core0_hz=535.6 core1_hz=221.5 fps=38.0 long_write_hz=0.0 i2s_in_sps=15922.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:48 stack_free_min=4560 heap_free_min=4025216 core0_hz=551.7 core1_hz=253.2 fps=39.4 long_write_hz=0.0 i2s_in_sps=16078.8 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=8.9
diag time=14:33:49 stack_free_min=4560 heap_free_min=4025216 core0_hz=544.2 core1_hz=253.4 fps=39.3 long_write_hz=0.0 i2s_in_sps=16031.4 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:50 stack_free_min=4560 heap_free_min=4025216 core0_hz=531.5 core1_hz=220.8 fps=38.0 long_write_hz=0.0 i2s_in_sps=16063.9 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.0
diag time=14:33:51 stack_free_min=4560 heap_free_min=4025216 core0_hz=501.0 core1_hz=190.3 fps=37.5 long_write_hz=0.0 i2s_in_sps=15858.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.9
diag time=14:33:52 stack_free_min=4560 heap_free_min=4025216 core0_hz=492.0 core1_hz=164.0 fps=37.0 long_write_hz=0.0 i2s_in_sps=16080.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=10.0
diag time=14:33:53 stack_free_min=4560 heap_free_min=4025216 core0_hz=516.4 core1_hz=191.0 fps=35.8 long_write_hz=0.0 i2s_in_sps=16000.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.0
diag time=14:33:54 stack_free_min=4560 heap_free_min=4025216 core0_hz=515.2 core1_hz=190.6 fps=36.2 long_write_hz=0.0 i2s_in_sps=15953.1 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=9.8
diag time=14:33:55 stack_free_min=4560 heap_free_min=4025212 core0_hz=587.9 core1_hz=114.2 fps=30.8 long_write_hz=0.0 i2s_in_sps=9056.6 i2s_out_sps=5481.6 fifo_over_hz=0.0 fifo_under_hz=5.0
diag time=14:33:56 stack_free_min=4560 heap_free_min=4025212 core0_hz=739.6 core1_hz=243.6 fps=40.4 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15858.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:57 stack_free_min=4560 heap_free_min=4025212 core0_hz=756.0 core1_hz=299.0 fps=44.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16080.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:58 stack_free_min=4560 heap_free_min=4025212 core0_hz=757.4 core1_hz=286.8 fps=43.2 long_write_hz=1.0 i2s_in_sps=0.0 i2s_out_sps=16031.4 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:33:59 stack_free_min=4560 heap_free_min=4025212 core0_hz=757.2 core1_hz=311.4 fps=43.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16000.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:00 stack_free_min=4560 heap_free_min=4025212 core0_hz=753.7 core1_hz=294.1 fps=43.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16031.9 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:01 stack_free_min=4560 heap_free_min=4025212 core0_hz=762.6 core1_hz=374.9 fps=50.4 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=17092.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:02 stack_free_min=4560 heap_free_min=4025212 core0_hz=778.0 core1_hz=391.0 fps=52.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15600.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:03 stack_free_min=4560 heap_free_min=4025212 core0_hz=768.2 core1_hz=381.6 fps=50.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15584.4 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:04 stack_free_min=4560 heap_free_min=4025212 core0_hz=764.2 core1_hz=385.6 fps=50.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16063.9 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:05 stack_free_min=4560 heap_free_min=4025212 core0_hz=770.0 core1_hz=387.0 fps=48.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=15600.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:06 stack_free_min=4560 heap_free_min=4025212 core0_hz=757.2 core1_hz=300.5 fps=44.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=17194.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:07 stack_free_min=4560 heap_free_min=4025212 core0_hz=755.7 core1_hz=323.7 fps=44.7 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=16206.6 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:12 stack_free_min=4560 heap_free_min=4025212 core0_hz=973.2 core1_hz=18.3 fps=2.8 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=1331.6 fifo_over_hz=0.2 fifo_under_hz=0.0
diag time=14:34:13 stack_free_min=4560 heap_free_min=4025212 core0_hz=962.0 core1_hz=382.0 fps=34.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:14 stack_free_min=4560 heap_free_min=4025212 core0_hz=1000.0 core1_hz=704.3 fps=61.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:15 stack_free_min=4560 heap_free_min=4025212 core0_hz=1000.0 core1_hz=697.3 fps=61.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:21 stack_free_min=4560 heap_free_min=4025212 core0_hz=999.3 core1_hz=52.4 fps=4.7 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:22 stack_free_min=4560 heap_free_min=4025212 core0_hz=1001.0 core1_hz=709.0 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:23 stack_free_min=4560 heap_free_min=4025212 core0_hz=999.0 core1_hz=709.0 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:24 stack_free_min=4560 heap_free_min=4025212 core0_hz=1000.0 core1_hz=707.3 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:25 stack_free_min=4560 heap_free_min=4025212 core0_hz=1000.0 core1_hz=707.0 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=14:34:26 stack_free_min=4560 heap_free_min=4025212 core0_hz=1001.0 core1_hz=708.0 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
```

Conclusions:

 * Actually listening to the remote side (smartphone recorder app and sine wave), there's no problems
 * GUI frame rate dip to 30 FPS from a 60 FPS limit, which is fine. This is caused by the MP3 compression
 * I2S sample rate, outbound to speaker, it is very spikey, but I do not hear anything weird
 * I2S sample rate, inbound from mic, a lot more steady than the outbound
 * I2S sample rate, close to 16000 Hz as expected
 * fifo_under_hz is not zero during mic activity, increasing watermark levels of the FIFO helps with this, unsure if I should be worried. I don't hear audio drops from a real test
 * at a pump size of 240 samples, the core 0 only needs to execute at 67 Hz, and the lowest that core has dipped is in the 400 Hz range
 * core 1 is the GUI and file writing core, it gets busy with more MP3 compression to do. In theory, again, this needs to run at 67 Hz minimum, lowest observed is 114
 * heap memory healthy
 * stack memory is healthy (it's the thread's stack so it's out of about 8K)
