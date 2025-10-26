#include <stdlib.h>
#include <stdio.h>
#include <time.h>

// https://stackoverflow.com/questions/3219393/stdlib-and-colored-output-in-c
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

void init_test() {
  // Color all test binary output
  printf("%s", ANSI_COLOR_CYAN);
  eprintf("%s", ANSI_COLOR_YELLOW);
}

typedef struct timespec Time;

// Retrieves the current time from the system clock.
void get_time(Time* ptr) {
  clock_gettime(CLOCK_MONOTONIC_RAW, ptr);
}

// Returns the number of nanoseconds between `start` and `end`.
time_t time_delta_ns(Time start, Time end) {
  return (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
}

// Returns the number of milliseconds between `start` and `end`.
double time_delta_ms(Time start, Time end) {
  return time_delta_ns(start, end) / 1e6;
}
