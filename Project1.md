## Task 1
### Mission 1: Alarm Clock 忙等待
在 `devices/timer.c` 中有函数 `timer_sleep` 

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
在这个函数中， `start`记录了这个函数被调用的时间，当 `intr_get_level () == INTR_ON`，即系统启用了中断的时候（一般都启用的）进行判断。对于一个线程来说,因为不能无休无止的让它运行下去，因此需要用 ticks 限制其运行时。当 ticks 到的时候，调用 `thread_yield` 让线程休眠

对于 PintOS， 有 `thread_current()` 获得一个结构体，这个结构体返回了当前正在运行的 `thread` 通过查找 `stack pointer` 的方式

对于 `thread_yield()`， 它首先获得了正在运行的 thread 通过 `thread_current()` 函数。如果该进程不是 `idle_thread` 即主函数 (Main Function) 则将其推入一个队列，等待下次执行。由 `schedule()` 进行下一个进程的执行。

那对于 `timer_sleep()` 函数，它传入的参数 `ticks` 是一个强制性的值，也就是会让进程休眠特定的事件，这样会带来效率低下的问题，因为在单CPU情况下，等待进程循环探测竞争条件，浪费了时间片。[1](https://zhuanlan.zhihu.com/p/101914970) 