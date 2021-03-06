/* 计时器相关 */

#include "bootpack.h"

#define PIT_CTRL 0x0043
#define PIT_CNT0 0x0040

timerctl_t timerctl;

#define TIMER_FLAGS_ALLOC 1 /* 已配置状态 */
#define TIMER_FLAGS_USING 2 /* 定时器使用中 */

void init_pit(void)
{
    io_out8(PIT_CTRL, 0x34);
    /* 11932 = 0x2e9c */
    io_out8(PIT_CNT0, 0x9c);
    io_out8(PIT_CNT0, 0x2e);
    timerctl.count = 0;
    int i;
    for (i = 0; i < MAX_TIMER; i++) {
        timerctl.timers0[i].flags = 0; /* 未使用 */
    }
    timer_t* t = timer_alloc(); /* 取得一个 */
    t->timeout = 0xffffffff;
    t->flags = TIMER_FLAGS_USING;
    t->next = 0; /* 最后 */
    timerctl.t0 = t; /* 哨兵在最前 */
    timerctl.next = 0xffffffff; /* 下次超时 */
    return;
}

timer_t* timer_alloc(void)
{
    int i;
    for (i = 0; i < MAX_TIMER; i++) {
        if (timerctl.timers0[i].flags == 0) {
            timerctl.timers0[i].flags = TIMER_FLAGS_ALLOC;
            timerctl.timers0[i].flags2 = 0;
            return &timerctl.timers0[i];
        }
    }
    return 0; /* 没找到 */
}

void timer_free(timer_t* timer)
{
    timer->flags = 0; /* 未使用 */
    return;
}

void timer_init(timer_t* timer, fifo32* fifo, int data)
{
    timer->fifo = fifo;
    timer->data = data;
    return;
}

void timer_settime(timer_t* timer, unsigned int timeout)
{
    timer->timeout = timeout + timerctl.count;
    timer->flags = TIMER_FLAGS_USING;
    int e = io_load_eflags();
    io_cli();
    timer_t *t = timerctl.t0, *s;
    if (timer->timeout <= t->timeout) {
        /* 插入前面 */
        timerctl.t0 = timer;
        timer->next = t; /* 下个是t */
        timerctl.next = timer->timeout;
        io_store_eflags(e);
        return;
    }
    /* 寻找插入位置 */
    for (;;) {
        s = t;
        t = t->next;
        if (timer->timeout <= t->timeout) {
            /* 插入s和t之间 */
            s->next = timer; /* s下个是timer */
            timer->next = t; /* timer下个是t */
            io_store_eflags(e);
            return;
        }
    }
}

void inthandler20(int* esp)
{
    io_out8(PIC0_OCW2, 0x60); /* 接收到IRQ-00后通知PIC */
    timerctl.count++;
    if (timerctl.next > timerctl.count) {
        return; /* 还不到下个时刻 */
    }
    timer_t* timer = timerctl.t0; /* 首地址 */
    char ts = 0;
    for (;;) {
        /* timers定时器都在使用中，不确认flags */
        if (timer->timeout > timerctl.count) {
            break;
        }
        /* 超时 */
        timer->flags = TIMER_FLAGS_ALLOC;
        if (timer != task_timer) {
            fifo32_put(timer->fifo, timer->data);
        } else {
            ts = 1;
        }
        timer = timer->next; /* 代入下个地址 */
    }
    /* 新版移位 */
    timerctl.t0 = timer;
    timerctl.next = timer->timeout;
    if (ts != 0) {
        task_switch();
    }
    return;
}

int timer_cancel(timer_t* timer)
{
    int e;
    timer_t* t;
    e = io_load_eflags();
    io_cli(); /* 在设置过程中禁止改变定时器状态 */
    if (timer->flags == TIMER_FLAGS_USING) { /* 是否需要取消？ */
        if (timer == timerctl.t0) {
            /* 第一个定时器的取消 */
            t = timer->next;
            timerctl.t0 = t;
            timerctl.next = t->timeout;
        } else {
            /* 其他定时器的取消 */
            /* 找到前一个定时器 */
            t = timerctl.t0;
            for (;;) {
                if (t->next == timer) {
                    break;
                }
                t = t->next;
            }
            t->next = timer->next; /* 将「timerの直前」的下一个指向「timerの次」 */
        }
        timer->flags = TIMER_FLAGS_ALLOC;
        io_store_eflags(e);
        return 1; /* 取消成功 */
    }
    io_store_eflags(e);
    return 0; /* 不需要取消处理 */
}

void timer_cancelall(fifo32* fifo)
{
    int e, i;
    timer_t* t;
    e = io_load_eflags();
    io_cli(); /* 在设置过程中禁止改变定时器状态 */
    for (i = 0; i < MAX_TIMER; i++) {
        t = &timerctl.timers0[i];
        if (t->flags != 0 && t->flags2 != 0 && t->fifo == fifo) {
            timer_cancel(t);
            timer_free(t);
        }
    }
    io_store_eflags(e);
    return;
}
