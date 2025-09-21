#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  char *line = NULL;
  size_t n = 0;

  while (1) {
    printf("Enter programs to run.\n");
    printf("> ");
    if (getline(&line, &n, stdin) == -1) {
      break;
    }

    size_t length = strlen(line);
    if (length > 0 && line[length - 1] == '\n') {
      line[length - 1] = '\0';
    }

    pid_t pid = fork();
    if (pid == 0) {
      if (execl(line, line, (char *)NULL) == -1) {
        printf("Exec failure\n");
        exit(1);
      }
    } else {
      int status;
      waitpid(pid, &status, 0);
    }
  }
  free(line);
  return 0;
}
