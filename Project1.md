## Task 1
### Mission 1: Alarm Clock 忙等待
In `devices/timer.c` we have `timer_sleep` 

```C
/* Sleeps for approximately TICKS timer ticks. Interrupts must
   be turned on. */
void
timer_sleep (int64_t ticks)
{
  int64_t start = timer_ticks ();
  ASSERT (intr_get_level () == INTR_ON);
  while (timer_elapsed (start) < ticks)
    thread_yield ();
}
```
In this function， `start` will record the time when the function is called，when `intr_get_level () == INTR_ON`，where the interupt is enabled（default）。For a single thread, we can't let it run endlessly，a `tick` is needed。When `tick` is down to 0，call `thread_yield` to let the thread yield.

For PintOS， 有 `thread_current()` to obtain a struct，which returns the current running `thread` by look up to `stack pointer`.

For `thread_yield()`， it first obtain the running thread by calling `thread_current()` 。If that thread is not `idle_thread` ,which is the main thread or Main Function, it will push it into a list，waiting to be excecuted next time。The `schedule()` will be responsible for picking which function to run next。

For `timer_sleep()` ，its input args `ticks` is a value that force the thread to wait for a certain number of time，which causes low efficiency. Under single CPU，waiting `timer_sleep()` and `scheduler()` to detect the conditions，waste the time and resources。[1](https://zhuanlan.zhihu.com/p/101914970) 

To solve this issue, we should let the thread block. When interupt happens, the `block_time` will -1. If `block_time` down to 0, we will unlock the thread for upcoming schduling.

So, 
1. add `block_tick` in `.h`
2. initialize to 0 when create thread
3. add `check_thread_if_block()`
4. Change `timer_interupt()`

Then Mission1 is finished.

### Misson 2: Priority Scheduling

