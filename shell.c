#define _POSIX_C_SOURCE 200809L
#include "msgs.h"
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define PATH_MAX 4096
#define WRITE_OUT(fd, s) write((fd), (s), (sizeof(s) - 1))
#define HISTORY_MAX 10
#define CMDLEN_MAX 4096

char history[HISTORY_MAX][CMDLEN_MAX];
int history_count = 0;
int history_start = 0;

void add_history(const char *cmdline) {

  char clean[CMDLEN_MAX];
  strncpy(clean, cmdline, CMDLEN_MAX - 1);
  clean[CMDLEN_MAX - 1] = '\0';
  size_t len = strlen(clean);
  if (len > 0 && clean[len - 1] == '\n') {
    clean[len - 1] = '\0';
  }

  int x = (history_start + history_count) % HISTORY_MAX;
  if (history_count < HISTORY_MAX) {
    strncpy(history[x], cmdline, CMDLEN_MAX - 1);
    history[x][CMDLEN_MAX - 1] = '\0';
    history_count++;
  } else {
    strncpy(history[history_start], cmdline, CMDLEN_MAX - 1);
    history[history_start][CMDLEN_MAX - 1] = '\0';
    history_start = (history_start + 1) % HISTORY_MAX;
  }
}

void display_prompt() {

  char cwd[PATH_MAX];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    write(STDOUT_FILENO, cwd, strlen(cwd));
    WRITE_OUT(STDOUT_FILENO, "$");
  } else {
    WRITE_OUT(STDERR_FILENO, "shell: unable to get directory\n");
  }
}

ssize_t read_line(char *buffer, size_t size) {
  ssize_t b_read = read(STDIN_FILENO, buffer, size - 1);
  if (b_read > 0) {
    buffer[b_read] = '\0';
  }
  return b_read;
}

void parse_and_exec(char *line) {
  char *argv[128];
  int argc = 0;
  char *saveptr;
  char *token = strtok_r(line, "\t\n", &saveptr);
  while (token && argc < 127) {
    argv[argc++] = token;
    token = strtok_r(NULL, "\t\n", &saveptr);
  }
  argv[argc] = NULL;

  if (argc == 0) {
    return;
  }

  int background = 0;
  if (argc > 0) {
    char *last = argv[argc - 1];
    size_t len = strlen(last);
    if (strcmp(last, "&") == 0) {
      background = 1;
      argc--;
      argv[argc] = NULL;
    } else if (len > 1 && last[len - 1] == '&') {
      background = 1;
      last[len - 1] = '\0';
    }
  }

  if (argc == 0)
    return;

  if (strcmp(argv[0], "exit") == 0) {
    if (argc > 1) {
      const char *msg = FORMAT_MSG("exit", TMA_MSG);
      write(STDERR_FILENO, msg, strlen(msg));
      return;
    }
    exit(0);
  }

  if (strcmp(argv[0], "pwd") == 0) {
    if (argc > 1) {
      const char *msg = FORMAT_MSG("pwd", TMA_MSG);
      write(STDERR_FILENO, msg, strlen(msg));
      return;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
      const char *msg = FORMAT_MSG("pwd", GETCWD_ERROR_MSG);
      write(STDERR_FILENO, msg, strlen(msg));
      return;
    }
    write(STDOUT_FILENO, cwd, strlen(cwd));
    WRITE_OUT(STDOUT_FILENO, "\n");
    return;
  }

  if (strcmp(argv[0], "cd") == 0) {

    if (argc > 2) {
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("cd", TMA_MSG));
    }

    const char *path = NULL;
    char buf[PATH_MAX];

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "~") == 0)) {
      struct passwd *pw = getpwuid(getuid());
      if (pw && pw->pw_dir)
        path = pw->pw_dir;
    } else if (argc == 2 && strncmp(argv[1], "~/", 2) == 0) {
      struct passwd *pw = getpwuid(getuid());
      if (pw && pw->pw_dir) {
        size_t homelen = strlen(pw->pw_dir);
        size_t sublen = strlen(argv[1] + 1);
        if (homelen + sublen < sizeof(buf)) {
          strcpy(buf, pw->pw_dir);
          strcat(buf, argv[1] + 1);
          path = buf;
        }
      }
    } else if (argc == 2) {
      path = argv[1];
    }

    if (path == NULL || chdir(path) != 0) {
      WRITE_OUT(STDERR_FILENO, FORMAT_MSG("cd", CHDIR_ERROR_MSG));
    }
    return;
  }

  if (strcmp(argv[0], "history") == 0) {
    if (argc > 1) {
      WRITE_OUT(STDERR_FILENO, FORMAT_MSG("history", TMA_MSG));
      return;
    }
    int total = (history_count < HISTORY_MAX) ? history_count : HISTORY_MAX;
    int start_num =
        history_count + (history_count >= HISTORY_MAX ? history_start : 0) - 1;
    for (int i = 0; i < total; ++i) {
      int x = (history_start + total - i - 1) % HISTORY_MAX;
      int cmd_num = start_num - i;
      char numbuf[16];
      snprintf(numbuf, sizeof(numbuf), "%d", cmd_num);
      char outbuf[CMDLEN_MAX + 32];
      int outlen =
          snprintf(outbuf, sizeof(outbuf), "%s\t%s\n", numbuf, history[x]);
      write(STDOUT_FILENO, outbuf, outlen);
    }
    return;
  }

  if (strcmp(argv[0], "help") == 0) {

    if (argc > 2) {
      const char *msg = FORMAT_MSG("help", TMA_MSG);
      write(STDERR_FILENO, msg, strlen(msg));
      return;
    }
    if (argc == 1) {
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("exit", EXIT_HELP_MSG));
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("pwd", PWD_HELP_MSG));
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("cd", CD_HELP_MSG));
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("help", HELP_HELP_MSG));
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("history", HISTORY_HELP_MSG));
      return;
    }

    char const *cmd = argv[1];
    if (strcmp(cmd, "exit") == 0)
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("exit", EXIT_HELP_MSG));
    else if (strcmp(cmd, "pwd") == 0)
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("pwd", PWD_HELP_MSG));
    else if (strcmp(cmd, "cd") == 0)
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("cd", CD_HELP_MSG));
    else if (strcmp(cmd, "help") == 0)
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("help", HELP_HELP_MSG));
    else
      WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("ls", EXTERN_HELP_MSG));
    return;
  }

  pid_t pid = fork();

  if (pid < 0) {
    WRITE_OUT(STDERR_FILENO, "shell: unable to fork\n");
    return;
  }

  if (pid == 0) {
    execvp(argv[0], argv);
    WRITE_OUT(STDERR_FILENO, "shell: exec failed\n");
    _exit(1);
  } else {
    if (!background) {
      int status;
      waitpid(pid, &status, 0);
    } else {
      WRITE_OUT(STDOUT_FILENO, "background process started\n");
    }
  }
}

void sigint_help(int signo) {
  WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("exit", EXIT_HELP_MSG));
  WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("pwd", PWD_HELP_MSG));
  WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("cd", CD_HELP_MSG));
  WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("help", HELP_HELP_MSG));
  WRITE_OUT(STDOUT_FILENO, FORMAT_MSG("history", HISTORY_HELP_MSG));
  WRITE_OUT(STDOUT_FILENO, "\n");
  WRITE_OUT(STDOUT_FILENO, "$");
}

int main() {

  struct sigaction sa;
  sa.sa_handler = sigint_help;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGINT, &sa, NULL);

  char buffer[4096];

  while (1) {
    display_prompt();

    ssize_t b_read = read_line(buffer, sizeof(buffer));
    if (b_read == -1) {
      WRITE_OUT(STDERR_FILENO, FORMAT_MSG("shell", READ_ERROR_MSG));
      continue;
    }
    if (b_read == 0) {
      WRITE_OUT(STDOUT_FILENO, "\n");
      break;
    }
    if (strcmp(buffer, "!!") == 0) {
      if (history_count == 0) {
        WRITE_OUT(STDERR_FILENO, FORMAT_MSG("history", HISTORY_NO_LAST_MSG));
        continue;
      }
      int found = 0;
      int start_num = history_count +
                      (history_count >= HISTORY_MAX ? history_start : 0) - 1;
      for (int i = 0; i < history_count; ++i) {
        int cmd_x = (history_start + i) % HISTORY_MAX;
        int cmd_num = start_num - (history_count - 1) + i;
        if (cmd_num == start_num) {
          found = 1;
          char temp[CMDLEN_MAX];
          strncpy(temp, history[cmd_x], CMDLEN_MAX - 1);
          temp[CMDLEN_MAX - 1] = '\0';
          WRITE_OUT(STDOUT_FILENO, temp);
          WRITE_OUT(STDOUT_FILENO, "\n");
          add_history(temp);
          fprintf(stderr, "!! debug::");
          for (int i = 0; temp[i] != '\0'; i++) {
            fprintf(stderr, "%02x ", (unsigned char)temp[i]);
          }
          fprintf(stderr, "\n");
          parse_and_exec(temp);
          break;
        }
      }
      if (!found) {
        WRITE_OUT(STDERR_FILENO, FORMAT_MSG("history", HISTORY_NO_LAST_MSG));
      }
    }
    if (buffer[0] == '!' && buffer[1] != '!' && buffer[1] != '\n') {
      char *numstr = buffer + 1;
      char *endptr;
      long n = strtol(numstr, &endptr, 10);
      if (*endptr != '\0' && *endptr != '\n') {
        WRITE_OUT(STDERR_FILENO, FORMAT_MSG("history", HISTORY_INVALID_MSG));
        continue;
      }

      int found = 0;
      int start_num = history_count +
                      (history_count >= HISTORY_MAX ? history_start : 0) - 1;
      for (int i = 0; i < history_count; ++i) {
        int cmd_x = (history_start + i) % HISTORY_MAX;
        int cmd_num = start_num - (history_count - 1) + i;
        if (cmd_num == n) {
          found = 1;
          char temp[CMDLEN_MAX];
          strncpy(temp, history[cmd_x], CMDLEN_MAX - 1);
          temp[CMDLEN_MAX - 1] = '\0';
          WRITE_OUT(STDOUT_FILENO, temp);
          WRITE_OUT(STDOUT_FILENO, "\n");
          ;
          add_history(temp);
          parse_and_exec(temp);
          break;
        }
      }
      if (!found) {
        WRITE_OUT(STDERR_FILENO, FORMAT_MSG("history", HISTORY_INVALID_MSG));
      }
      continue;
    }
    add_history(buffer);
    parse_and_exec(buffer);
  }
  return 0;
}
