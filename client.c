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
} ReceiverArgs;

void *receiver_thread(void *arg) {
  ReceiverArgs *params = (ReceiverArgs *)arg;
  FILE *logfile = fopen(params->log_file_path, "a");

  if (!logfile) {
    perror("fopen lof file");
    pthread_exit(NULL);
  }

  while (1) {
    uint8_t recv_buf[1024];
    ssize_t rlen = read(params->sockfd, recv_buf, sizeof(recv_buf));
    if (rlen < 0) {
      perror("read error");
      break;
    }
    if (rlen == 0) {
      printf("server closed connection or error. Exiting\n");
      break;
    }

    uint8_t msg_type = recv_buf[0];
    if (msg_type == 1) {
      printf("Recieved type 1 (shutdown) from server. Exiting\n");
      fprintf(logfile, "Received type 1 from server. Exiting\n");
      fflush(logfile);
      break;
    } else if (msg_type == 0 && rlen >= 7) {
      uint32_t sender_ip;
      uint16_t sender_port;
      memcpy(&sender_ip, recv_buf + 1, sizeof(sender_ip));
      memcpy(&sender_port, recv_buf + 5, sizeof(sender_port));
      struct in_addr ip_addr;
      ip_addr.s_addr = sender_ip;
      char *ip_str = inet_ntoa(ip_addr);
      int port = ntohs(sender_port);

      int msg_offset = 7;
      int msg_len = rlen - msg_offset;
      char message[1024];
      memcpy(message, recv_buf + msg_offset, msg_len);
      message[msg_len] = '\0';

      printf("%-15s%-10u%s", ip_str, port, message);
      fprintf(logfile, "%-15s%-10u%s", ip_str, port, message);
      fflush(logfile);
    }
  }
  fclose(logfile);
  close(params->sockfd);
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

  printf("Connected to server %s%d\n", server_ip, port);

  ReceiverArgs recv_args = {sockfd, log_file_path};
  pthread_t recv_tid;
  if (pthread_create(&recv_tid, NULL, receiver_thread, &recv_args) != 0) {
    perror("pthread_create");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  int msg_bytes = 16;
  uint8_t rand_bytes[msg_bytes];
  char hex_msg[msg_bytes * 2 + 1];

  for (int i = 0; i < num_messages; i++) {
    if (getentropy(rand_bytes, msg_bytes) != 0) {
      perror("getentropy");
      exit(EXIT_FAILURE);
    }
    if (convert(rand_bytes, msg_bytes, hex_msg, sizeof(hex_msg)) != 0) {
      fprintf(stderr, "Conversion error\n");
      exit(EXIT_FAILURE);
    }

    size_t hex_len = strlen(hex_msg);
    uint8_t send_buf[1 + msg_bytes * 2 + 2];
    send_buf[0] = 0;
    memcpy(send_buf + 1, hex_msg, hex_len);
    send_buf[1 + hex_len] = '\n';

    size_t total_len = 1 + hex_len + 1;
    ssize_t w = write(sockfd, send_buf, total_len);
    if (w != total_len) {
      perror("write");
      exit(EXIT_FAILURE);
    }
    printf("sent hex message %d: %s\n", i + 1, hex_msg);
  }

  uint8_t type1_msg = 1;
  if (write(sockfd, &type1_msg, 1) != 1) {
    perror("write type 1");
    close(sockfd);
    exit(EXIT_FAILURE);
  }
  printf("Sent type 1 (shutdown) message to server.\n");

  pthread_join(recv_tid, NULL);

  /*FILE *logfile = fopen(log_file_path, "a");
  if (!logfile) {
    perror("fopen log file");
    exit(EXIT_FAILURE);
  }

  while (1) {
    uint8_t recv_buf[1024];
    ssize_t rlen = read(sockfd, recv_buf, sizeof(recv_buf));
    if (rlen <= 0) {
      printf("server closed connection or error. Exiting\n");
      close(sockfd);
      exit(EXIT_SUCCESS);
    }

    uint8_t msg_type = recv_buf[0];
    if (msg_type == 1) {
      printf("Recieved type 1 (shutdown) from server. Exiting\n");
      fprintf(logfile, "Received type 1 from server. Exiting\n");
      fclose(logfile);
      close(sockfd);
      exit(EXIT_SUCCESS);
    } else if (msg_type == 0 && rlen >= 7) {
      uint32_t sender_ip;
      uint16_t sender_port;
      memcpy(&sender_ip, recv_buf + 1, sizeof(sender_ip));
      memcpy(&sender_port, recv_buf + 5, sizeof(sender_port));
      struct in_addr ip_addr;
      ip_addr.s_addr = sender_ip;
      char *ip_str = inet_ntoa(ip_addr);
      int port = ntohs(sender_port);

      int msg_offset = 7;
      int msg_len = rlen - msg_offset;
      char message[1024];
      memcpy(message, recv_buf + msg_offset, msg_len);
      message[msg_len] = '\0';

      printf("From %s:%d: %s", ip_str, port, message);
      fprintf(logfile, "From %s:%d %s", ip_str, port, message);
      fflush(logfile);
    }
   }*/

  // close(sockfd);
  return 0;
}
