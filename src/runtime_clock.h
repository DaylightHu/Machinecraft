#ifndef MC_RUNTIME_CLOCK_H
#define MC_RUNTIME_CLOCK_H

/* Monotonic wall time, shared by the session driver and headless input loop. */
double runtime_clock_now(void);

#endif
