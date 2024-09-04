#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif
 
/* Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {	//타이머 설정
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {	// 타이머 틱 동안 수행할 수 있는 최대 루프수(loops_per_tick)를 보정
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {	// OS가 부팅된 이후 경과된 타이머 틱수를 반환
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {	// 특정시점(then) 이후 경과된 타이머 틱수를 반환
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
// busy - waiting
// void
// timer_sleep (int64_t ticks) {				// 스레드의 실행을 ticks 동안 일시 중단한다.
// 	int64_t start = timer_ticks ();			// start: 시작 시간

// 	ASSERT (intr_get_level () == INTR_ON);	// 인터럽트가 들어왔을 때에만 실행
// 	while (timer_elapsed (start) < ticks)	// 지난 시간 < ticks 보다 작을때까지
// 		thread_yield ();				// ready_list 맨 뒤로 이동
// }

/* Suspends execution for approximately TICKS timer ticks. */
// Sleep - Awake solution
void
timer_sleep (int64_t ticks) {				// 스레드의 실행을 ticks 동안 일시 중단한다.
	int64_t start = timer_ticks ();			// start: 시작 시간

	ASSERT (intr_get_level () == INTR_ON);	// 인터럽트가 들어왔을 때에만 실행
	thread_sleep(start + ticks );			// start + ticks 까지 자고 있어
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {	//현재 경과된 타이머 틱 수를 출력
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

// /* Timer interrupt handler. */
// static void
// timer_interrupt (struct intr_frame *args UNUSED) {	// 타이머 인터럽트 발생. 타이머 증가시키고 스레드 스케줄링 처리하는 thread_tick 호출
// 	ticks++;
// 	thread_tick ();

// }

/* Timer interrupt handler. */
/*--------------------------------------------------------project 1--------------------------------------------------------*/


/*
timer_interrupt: 매 tick마다 timer 인터럽트 시 호출하는 함수.
- sleep queue에서 깨어날 스레드 있는지 확인
	- sleep queue에서 가장 빨리 깨어날 스레드의 tick값을 확인
	- 있다면 sleep queue를 순회하며 스레드를 깨운다.
	- 이때 thread_awake()를 사용.

	timer_interrupt가 들어올 때(시간이 증가할 때),
	현재 ticks보다 작은 wakeup_tick(깨어나야 할 시간)이 있는지 판단.
	있다면 
*/
static void
timer_interrupt (struct intr_frame *args UNUSED) {	
	ticks++;
	thread_tick ();	//테스트용 무시

	int64_t next_tick;	
	next_tick = get_next_tick_to_awake();	// 다음에 깨울 next_tick 가져오기

	if(ticks >= next_tick){	//깨울 시간이 되면
		thread_awake(ticks);	//thread_awake로 깨우기
	}


}


/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {	// 루프가 너무 오래 걸리는지 체크
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {		//간단한 루프를 반복하여 짧은 지연을 구현
		while (loops-- > 0)
			barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {	//실수형 시간을 타이머 틱으로 변환하여 지연을 구현
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
