#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <dirent.h>

// https://stackoverflow.com/questions/3219393/stdlib-and-colored-output-in-c
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

int run_test(const char* executable, int* exit_status) {
  pid_t pid = fork();

  // Failure
  if (pid == -1)
    return -2;

  // Child
  if (pid == 0) {
    char* argv[] = {NULL};
    execv(executable, argv);
    perror("execvp failed");
    exit(122);
  }

  // We're the parent process.
  if (waitpid(pid, exit_status, 0) == -1) {
    perror("waitpid failed");
    return -3;
  }
  return 0;
}


int main() {
  DIR* dir = opendir(".");
  if (dir == NULL) {
    perror("failed to open cwd:");
    return -1;
  }

  struct dirent* ent;
  int error = 0;
  printf("\n");
  while ((ent = readdir(dir)) != NULL) {
    char* suffix = strrchr(ent->d_name, '.');
    if (suffix != NULL && strcmp(suffix, ".test") == 0) {
      printf("RUN " ANSI_COLOR_CYAN "%s" ANSI_COLOR_RESET "\n", ent->d_name);

      int test_status;
      int run_status = run_test(ent->d_name, &test_status);

      printf("  ");

      if (run_status) {
        printf(ANSI_COLOR_RED "FAILED: cannot run test (%d)\n" ANSI_COLOR_RESET, test_status);
        error = run_status;
        continue;
      }
      if (WIFEXITED(test_status)) {
        int exit_code = WEXITSTATUS(test_status);
        if (exit_code == 0) {
          printf(ANSI_COLOR_GREEN "OK\n" ANSI_COLOR_RESET);
        } else {
          printf(ANSI_COLOR_RED "FAILED: exit status %d\n" ANSI_COLOR_RESET, test_status);
          error = exit_code;
        }
      }
      else if (WIFSIGNALED(test_status)) {
        printf(ANSI_COLOR_RED "FAILED: test terminated with signal %d\n" ANSI_COLOR_RESET, WTERMSIG(test_status));
        error = -4;
      }
      else {
        error = -5;
      }
      printf("\n");
    }
  }
  closedir(dir);

  return error;
}
