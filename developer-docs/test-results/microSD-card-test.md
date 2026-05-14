Card is a PNY 32 GB `Elite Class 10 U1 V10 A1 microSDHC Trail Camera Flash Memory Card – Read Up to 100MB/s, UHS-I`. The memory size is legit, not a scam.

Test date is 2026-05-14

starting microSD recorder smoke test
free heap before FIFO init: 267572
[  1032][I][esp32-hal-i2c.c:125] i2cInit(): Initializing I2C Master: sda=21 scl=22 freq=100000
[  1053][I][M5GFX.cpp:1114] autodetect(): [M5GFX] [Autodetect] M5StackCore2
[  1212][I][esp32-hal-i2c.c:125] i2cInit(): Initializing I2C Master: sda=21 scl=22 freq=100000
[  1222][I][esp32-hal-i2c.c:125] i2cInit(): Initializing I2C Master: sda=21 scl=22 freq=100000
[  1267][I][esp32-hal-i2c.c:125] i2cInit(): Initializing I2C Master: sda=21 scl=22 freq=100000
[  1278][I][esp32-hal-i2c.c:125] i2cInit(): Initializing I2C Master: sda=21 scl=22 freq=100000
[  1593][I][MicroSdCard.cpp:65] tryBeginAtFrequency(): [MicroSdCard] microSD initialized at 20 MHz
[  1602][I][MicroSdCard.cpp:100] begin(): [MicroSdCard] microSD space: total=31255953408 used=20381696 free=31235571712 ; freq=20 MHz
free heap after FIFO init: 263660

microSD card statistics
ready: yes
card type: SDHC/SDXC
sector count: 61071360
raw capacity: 31268536320 bytes
filesystem: FAT32
sectors/cluster: 64
bytes/cluster: 32768
cluster count: 953856
free clusters: 953234
total bytes: 31255953408
used bytes: 20381696
free bytes: 31235571712
manufacturer ID: 0x9F
OEM ID: TI
product: 00000
revision: 6.5
serial: 0x33E0F659
manufactured: 11/2025
CID: 9F544930303030306533E0F659019B5D
CSD capacity sectors: 61071360
erase size blocks: 128
erase single block: yes
CSD: 400E00325B590000E8F77F800A400039
OCR: 0xC0FF8000
SD spec version: 6.00
data after erase: ones
SCR: 02B5848300000000

[ 33762][I][AudioFileRecorder.cpp:398] grow_file(): [AudioFileRecorder] file reserved 2147483648 bytes
[ 33771][I][AudioFileRecorder.cpp:273] startRecording(): [AudioFileRecorder] recording started: /S-2000-01-30-11-31-19-U.rec
recording path: /S-2000-01-30-11-31-19-U.rec
test_sdcard: throughput test: step=30000 ms report=5000 ms chunk=64 max_chunk=8192 samples
test_sdcard: throughput section: chunk=64 samples
test_sdcard: chunk=64 submitted samples=108160 interval_bytes_per_second=43255 file_bytes=899080 write_avg_ms=2.936 write_max_ms=17.284
test_sdcard: chunk=64 submitted samples=187520 interval_bytes_per_second=31737 file_bytes=1558760 write_avg_ms=4.121 write_max_ms=53.698
test_sdcard: chunk=64 submitted samples=290304 interval_bytes_per_second=41088 file_bytes=2413152 write_avg_ms=2.999 write_max_ms=46.349
test_sdcard: chunk=64 submitted samples=401408 interval_bytes_per_second=44432 file_bytes=3336704 write_avg_ms=3.121 write_max_ms=11.678
test_sdcard: chunk=64 submitted samples=512768 interval_bytes_per_second=44535 file_bytes=4262384 write_avg_ms=2.728 write_max_ms=11.693
test_sdcard: throughput section: chunk=128 samples
test_sdcard: chunk=128 submitted samples=797440 interval_bytes_per_second=84360 file_bytes=5751984 write_avg_ms=2.855 write_max_ms=46.120
test_sdcard: chunk=128 submitted samples=1019136 interval_bytes_per_second=88572 file_bytes=6673408 write_avg_ms=3.007 write_max_ms=14.375
test_sdcard: chunk=128 submitted samples=1240832 interval_bytes_per_second=88642 file_bytes=7594832 write_avg_ms=2.867 write_max_ms=11.702
test_sdcard: chunk=128 submitted samples=1383168 interval_bytes_per_second=56934 file_bytes=8186416 write_avg_ms=2.953 write_max_ms=50.932
test_sdcard: chunk=128 submitted samples=1605120 interval_bytes_per_second=88780 file_bytes=9108904 write_avg_ms=2.717 write_max_ms=14.349
test_sdcard: throughput section: chunk=256 samples
test_sdcard: chunk=256 submitted samples=2251520 interval_bytes_per_second=169510 file_bytes=10911320 write_avg_ms=5.445 write_max_ms=45.080
test_sdcard: chunk=256 submitted samples=2542848 interval_bytes_per_second=116414 file_bytes=11516736 write_avg_ms=2.904 write_max_ms=51.419
test_sdcard: chunk=256 submitted samples=2984704 interval_bytes_per_second=176530 file_bytes=12434968 write_avg_ms=2.856 write_max_ms=14.363
test_sdcard: chunk=256 submitted samples=3426560 interval_bytes_per_second=176671 file_bytes=13353200 write_avg_ms=2.793 write_max_ms=14.137
test_sdcard: chunk=256 submitted samples=3844352 interval_bytes_per_second=167049 file_bytes=14221424 write_avg_ms=4.040 write_max_ms=44.897
test_sdcard: throughput section: chunk=512 samples
test_sdcard: chunk=512 submitted samples=4579584 interval_bytes_per_second=172747 file_bytes=15749328 write_avg_ms=2.817 write_max_ms=18.422
test_sdcard: chunk=512 submitted samples=5021952 interval_bytes_per_second=176841 file_bytes=16668624 write_avg_ms=2.818 write_max_ms=11.682
test_sdcard: chunk=512 submitted samples=5427456 interval_bytes_per_second=161974 file_bytes=17511312 write_avg_ms=3.863 write_max_ms=44.770
test_sdcard: chunk=512 submitted samples=5751040 interval_bytes_per_second=129097 file_bytes=18183760 write_avg_ms=2.923 write_max_ms=52.888
test_sdcard: chunk=512 submitted samples=6193408 interval_bytes_per_second=176699 file_bytes=19103056 write_avg_ms=2.741 write_max_ms=14.439
test_sdcard: throughput section: chunk=1024 samples
test_sdcard: chunk=1024 submitted samples=7009536 interval_bytes_per_second=155648 file_bytes=20799072 write_avg_ms=4.277 write_max_ms=48.891
test_sdcard: chunk=1024 submitted samples=7347456 interval_bytes_per_second=134844 file_bytes=21501312 write_avg_ms=2.866 write_max_ms=52.303
test_sdcard: chunk=1024 submitted samples=7789824 interval_bytes_per_second=176947 file_bytes=22420608 write_avg_ms=2.765 write_max_ms=14.360
test_sdcard: chunk=1024 submitted samples=8232192 interval_bytes_per_second=176382 file_bytes=23339904 write_avg_ms=2.756 write_max_ms=11.691
test_sdcard: chunk=1024 submitted samples=8598784 interval_bytes_per_second=145617 file_bytes=24101728 write_avg_ms=6.294 write_max_ms=50.504
test_sdcard: throughput section: chunk=2048 samples
test_sdcard: chunk=2048 submitted samples=9385216 interval_bytes_per_second=176664 file_bytes=25736032 write_avg_ms=2.769 write_max_ms=11.709
test_sdcard: chunk=2048 submitted samples=9827584 interval_bytes_per_second=176488 file_bytes=26655328 write_avg_ms=2.835 write_max_ms=11.681
test_sdcard: chunk=2048 submitted samples=10179840 interval_bytes_per_second=140565 file_bytes=27387360 write_avg_ms=4.225 write_max_ms=51.669
test_sdcard: chunk=2048 submitted samples=10552576 interval_bytes_per_second=148975 file_bytes=28161952 write_avg_ms=2.752 write_max_ms=46.184
test_sdcard: chunk=2048 submitted samples=10994944 interval_bytes_per_second=176101 file_bytes=29081248 write_avg_ms=2.853 write_max_ms=11.672
test_sdcard: throughput section: chunk=4096 samples
test_sdcard: chunk=4096 submitted samples=11773184 interval_bytes_per_second=139204 file_bytes=30676184 write_avg_ms=4.303 write_max_ms=50.527
test_sdcard: chunk=4096 submitted samples=12158208 interval_bytes_per_second=153396 file_bytes=31475780 write_avg_ms=2.901 write_max_ms=46.188
test_sdcard: chunk=4096 submitted samples=12600576 interval_bytes_per_second=174435 file_bytes=32397736 write_avg_ms=2.834 write_max_ms=14.123
test_sdcard: chunk=4096 submitted samples=13042944 interval_bytes_per_second=175125 file_bytes=33315436 write_avg_ms=2.834 write_max_ms=11.724
test_sdcard: chunk=4096 submitted samples=13362432 interval_bytes_per_second=126605 file_bytes=33979904 write_avg_ms=5.712 write_max_ms=51.455
test_sdcard: throughput section: chunk=8192 samples
test_sdcard: chunk=8192 submitted samples=14181632 interval_bytes_per_second=173989 file_bytes=35681772 write_avg_ms=2.849 write_max_ms=11.695
test_sdcard: chunk=8192 submitted samples=14624000 interval_bytes_per_second=174229 file_bytes=36599472 write_avg_ms=2.881 write_max_ms=17.046
test_sdcard: chunk=8192 submitted samples=14935296 interval_bytes_per_second=123603 file_bytes=37246916 write_avg_ms=5.448 write_max_ms=50.308
test_sdcard: chunk=8192 submitted samples=15344896 interval_bytes_per_second=162217 file_bytes=38100244 write_avg_ms=2.845 write_max_ms=46.113
test_sdcard: chunk=8192 submitted samples=15770880 interval_bytes_per_second=170257 file_bytes=38983896 write_avg_ms=2.949 write_max_ms=20.091
[278559][I][AudioFileRecorder.cpp:330] stopRecording(): [AudioFileRecorder] recording stopped: /S-2000-01-30-11-31-19-U.rec bytes=39891488
recording complete: /S-2000-01-30-11-31-19-U.rec bytes=39891488
microSD recorder smoke test finished