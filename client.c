#define _POSIX_C_SOURCE 200102L
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int convert(uint8_t *buf, ssize_t buf_size, char *str, ssize_t str_size) {
  if (buf == NULL || str == NULL || buf_size <= 0 ||
      str_size < (buf_size * 2 + 1)) {
    return -1;
  }
  for (int i = 0; i < buf_size; i++)
    sprintf(str + i * 2, "%02X", buf[i]);
  str[buf_size * 2] = '\0';
  return 0;
}

typedef struct {
  int sockfd;
  char *log_file_path;
  volatile int *done;
} ReceiverArgs;

void *receiver_thread(void *arg) {
  ReceiverArgs *params = (ReceiverArgs *)arg;
  FILE *logfile = fopen(params->log_file_path, "w");

  if (!logfile) {
    perror("fopen lof file");
    pthread_exit(NULL);
  }

  uint8_t buffer[4096];
  int buf_len = 0;
  while (1) {
    uint8_t recv_buf[1024];
    ssize_t rlen = read(params->sockfd, recv_buf, sizeof(recv_buf));
    if (rlen < 0) {
      perror("read error");
      break;
    }
    if (rlen == 0) {
      printf("server closed connection. Exiting receiver\n");
      break;
    }

    if (buf_len + rlen > (int)sizeof(buffer)) {
      fprintf(stderr, "Buffer Overflow\n");
      break;
    }

    memcpy(buffer + buf_len, recv_buf, rlen);
    buf_len += rlen;

    int pos = 0;
    while (pos < buf_len) {
      uint8_t msg_type = buffer[pos];
      if (msg_type == 1) {
        printf("Recieved type 1 (shutdown) from server. Exiting\n");
        *(params->done) = 1;
        fclose(logfile);
        pthread_exit(NULL);
      } else if (msg_type == 0) {

        if (buf_len - pos < 7)
          break;
        int newline_pos = -1;
        for (int j = pos + 7; j < buf_len; j++) {
          if (buffer[j] == '\n') {
            newline_pos = j;
            break;
          }
        }
        if (newline_pos == -1)
          break;

        uint32_t sender_ip;
        uint16_t sender_port;
        memcpy(&sender_ip, buffer + pos + 1, sizeof(sender_ip));
        memcpy(&sender_port, buffer + pos + 5, sizeof(sender_port));
        struct in_addr ip_addr;
        ip_addr.s_addr = sender_ip;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_addr, ip_str, sizeof(ip_str));
        unsigned int port_num = ntohs(sender_port);

        int content_start = pos + 7;
        int content_len = newline_pos - content_start;
        char message[1024];
        if (content_len > 0 && content_len < 1024) {
          memcpy(message, buffer + content_start, content_len);
        }
        message[content_len > 0 ? content_len : 0] = '\0';

        printf("%-15s%-10u%s", ip_str, port_num, message);
        fprintf(logfile, "%-15s%-10u%s", ip_str, port_num, message);
        fflush(logfile);
        pos = newline_pos + 1;
      } else {
        pos += 1;
      }
    }
    if (pos > 0) {
      if (pos < buf_len) {
        memmove(buffer, buffer + pos, buf_len - pos);
      }
      buf_len -= pos;
    }
  }
  fclose(logfile);
  pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
  if (argc != 5) {
    fprintf(stderr,
            "Usage: %s <IP address> <port number> <# of messages> <log file "
            "path>\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }

  char *server_ip = argv[1];
  int port = atoi(argv[2]);
  int num_messages = atoi(argv[3]);
  char *log_file_path = argv[4];

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);

  if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    perror("inet_pton");
    exit(EXIT_FAILURE);
  }

  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connect");
    exit(EXIT_FAILURE);
  }

  printf("Connected to server %s:%d\n", server_ip, port);

  volatile int done = 0;
  ReceiverArgs recv_args = {sockfd, log_file_path, &done};
  pthread_t recv_tid;
  if (pthread_create(&recv_tid, NULL, receiver_thread, &recv_args) != 0) {
    perror("pthread_create");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  int msg_bytes = 16;
  uint8_t rand_bytes[16];
  char hex_msg[16 * 2 + 1];

  for (int i = 0; i < num_messages && !done; i++) {

    if (getentropy(rand_bytes, msg_bytes) != 0) {
      perror("getentropy");
      exit(EXIT_FAILURE);
    }
    if (convert(rand_bytes, msg_bytes, hex_msg, sizeof(hex_msg)) != 0) {
      fprintf(stderr, "Conversion error\n");
      exit(EXIT_FAILURE);
    }

    size_t hex_len = strlen(hex_msg);
    uint8_t send_buf[64];
    send_buf[0] = 0;
    memcpy(send_buf + 1, hex_msg, hex_len);
    send_buf[1 + hex_len] = '\n';

    size_t total_len = 1 + hex_len + 1;
    ssize_t w = write(sockfd, send_buf, total_len);
    if (w != total_len) {
      perror("write");
      exit(EXIT_FAILURE);
    }
    printf("sent message %d: %s\n", i + 1, hex_msg);
  }

  uint8_t type1_msg = 1;
  if (write(sockfd, &type1_msg, 1) != 1) {
    perror("write type 1");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  printf("Sent type 1 (shutdown) message to server.\n");

  pthread_join(recv_tid, NULL);

  close(sockfd);
  return 0;
}
