Test date: June 4 2026

This was done after adding the code to halt GUI redraws after any file related web request functions are called

Notice the FPS dropping to zero and the core 1 frequency also dropping to nearly 0

```
diag time=00:06:23 stack_free_min=4568 heap_free_min=4130536 core0_hz=990.0 core1_hz=589.4 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:24 stack_free_min=4568 heap_free_min=4130536 core0_hz=991.0 core1_hz=588.0 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:25 stack_free_min=4568 heap_free_min=4130536 core0_hz=953.0 core1_hz=582.0 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:26 stack_free_min=4568 heap_free_min=4130536 core0_hz=927.1 core1_hz=582.4 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:27 stack_free_min=4568 heap_free_min=4130536 core0_hz=983.0 core1_hz=588.0 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:28 stack_free_min=4568 heap_free_min=4130536 core0_hz=818.4 core1_hz=361.4 fps=0.9 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:30 stack_free_min=4568 heap_free_min=4130536 core0_hz=571.1 core1_hz=0.8 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:31 stack_free_min=4568 heap_free_min=4130536 core0_hz=525.7 core1_hz=15.5 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:32 stack_free_min=4568 heap_free_min=4130536 core0_hz=526.6 core1_hz=29.0 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:33 stack_free_min=4568 heap_free_min=4130536 core0_hz=532.1 core1_hz=24.9 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:34 stack_free_min=4568 heap_free_min=4130536 core0_hz=540.1 core1_hz=29.9 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:35 stack_free_min=4568 heap_free_min=4130536 core0_hz=488.4 core1_hz=35.7 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:36 stack_free_min=4568 heap_free_min=4130536 core0_hz=537.2 core1_hz=43.6 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:37 stack_free_min=4568 heap_free_min=4130536 core0_hz=579.1 core1_hz=122.4 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:38 stack_free_min=4568 heap_free_min=4130536 core0_hz=576.7 core1_hz=62.1 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:39 stack_free_min=4568 heap_free_min=4130536 core0_hz=568.7 core1_hz=79.6 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:40 stack_free_min=4568 heap_free_min=4130536 core0_hz=524.9 core1_hz=36.3 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:42 stack_free_min=4568 heap_free_min=4130536 core0_hz=533.4 core1_hz=13.4 fps=0.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:43 stack_free_min=4568 heap_free_min=4130536 core0_hz=626.4 core1_hz=159.8 fps=1.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:44 stack_free_min=4568 heap_free_min=4130536 core0_hz=994.0 core1_hz=588.4 fps=2.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:45 stack_free_min=4568 heap_free_min=4130536 core0_hz=990.0 core1_hz=590.0 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
diag time=00:06:46 stack_free_min=4568 heap_free_min=4130536 core0_hz=988.0 core1_hz=587.4 fps=3.0 long_write_hz=0.0 i2s_in_sps=0.0 i2s_out_sps=0.0 fifo_over_hz=0.0 fifo_under_hz=0.0
```