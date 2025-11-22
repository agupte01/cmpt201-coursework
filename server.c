#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#define MAX_CLIENTS 100
#define SHUTDOWN_WAIT_TIMEOUT_SEC 10

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port number> <# of clients>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  int port = atoi(argv[1]);
  int max_clients = atoi(argv[2]);
  if (max_clients > MAX_CLIENTS)
    max_clients = MAX_CLIENTS;

  int client_sockets[MAX_CLIENTS] = {0};
  int server_fd, new_socket, sd, max_sd, activity;
  struct sockaddr_in address;
  int addrlen = sizeof(address);
  fd_set readfds;

  int client_finished[MAX_CLIENTS] = {0};
  int total_connected_clients = 0;
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  int opt = 1;

  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind failure");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, max_clients) < 0) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  printf("Server is listening on port %d\n", port);

  char leftover[MAX_CLIENTS][1024] = {{0}};
  int leftover_len[MAX_CLIENTS] = {0};

  int shutting_down = 0;
  time_t shutdown_start_time = 0;
  while (1) {
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);
    max_sd = server_fd;

    for (int i = 0; i < MAX_CLIENTS; i++) {
      sd = client_sockets[i];
      if (sd > 0)
        FD_SET(sd, &readfds);
      if (sd > max_sd)
        max_sd = sd;
    }

    struct timeval timeout = {1, 0};
    activity = select(max_sd + 1, &readfds, NULL, NULL, &timeout);
    if (activity < 0) {
      perror("select error");
      exit(EXIT_FAILURE);
    }

    if (FD_ISSET(server_fd, &readfds) && !shutting_down) {
      new_socket =
          accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

      if (new_socket < 0) {
        perror("accept");
        exit(EXIT_FAILURE);
      }

      for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] == 0) {
          client_sockets[i] = new_socket;
          client_finished[i] = 0;
          total_connected_clients++;
          printf("Client added to slot %d, total: %d\n", i,
                 total_connected_clients);
          break;
        }
      }
    }

    for (int i = 0; i < MAX_CLIENTS; i++) {
      sd = client_sockets[i];
      if (sd > 0 && FD_ISSET(sd, &readfds)) {
        char buffer[1024];
        int valread = read(sd, buffer, sizeof(buffer));
        if (valread <= 0) {
          client_finished[i] = 1;
          printf("Client finished flags: ");
          for (int k = 0; k < max_clients; k++) {
            if (client_sockets[k] > 0)
              printf("%d:%d ", k, client_finished[k]);
          }
          printf("\n");
          close(sd);
          client_sockets[i] = 0;
          total_connected_clients--;
          leftover_len[i] = 0;
          printf("Client disconnected from slot %d, total: %d\n", i,
                 total_connected_clients);
          continue;
        }
        char recvbuf[2048];
        int rcvlen = leftover_len[i];
        if (rcvlen > 0)
          memcpy(recvbuf, leftover[i], rcvlen);
        memcpy(recvbuf + rcvlen, buffer, valread);
        rcvlen += valread;

        int start = 0;
        while (start < rcvlen) {
          if (rcvlen - start < 1)
            break;

          uint8_t msg_type = (uint8_t)recvbuf[start];

          if (msg_type == 0) {

            int end = start + 1;
            while (end < rcvlen && recvbuf[end] != '\n')
              end++;
            if (end >= rcvlen)
              break;
            int msg_data_len = end - (start + 1) + 1;

            uint8_t out_buffer[1024];
            size_t out_len = 1;
            out_buffer[0] = 0;

            struct sockaddr_in peer_addr;
            socklen_t peer_addrlen = sizeof(peer_addr);
            if (getpeername(sd, (struct sockaddr *)&peer_addr, &peer_addrlen) <
                0) {
              perror("getpeername");
              start = end + 1;
              continue;
            }
            uint32_t sender_ip = peer_addr.sin_addr.s_addr;
            uint16_t sender_port = peer_addr.sin_port;
            memcpy(out_buffer + out_len, &sender_ip, sizeof(sender_ip));
            out_len += sizeof(sender_ip);
            memcpy(out_buffer + out_len, &sender_port, sizeof(sender_port));
            out_len += sizeof(sender_port);

            memcpy(out_buffer + out_len, recvbuf + start + 1, msg_data_len);
            out_len += msg_data_len;

            for (int j = 0; j < max_clients; j++) {
              int cli_sd = client_sockets[j];
              if (cli_sd > 0) {
                ssize_t w = write(cli_sd, out_buffer, out_len);
                if (w != (ssize_t)out_len) {
                  perror("write message to client");
                }
              }
            }
            start = end + 1;
          } else if (msg_type == 1) {
            client_finished[i] = 1;
            printf("Client %d sent type 1\n", i);
            fflush(stdout);

            start += 1;
            if (start < rcvlen && recvbuf[start] == '\n') {
              start += 1;
            }
            int all_finished = 1;
            int finished_count = 0;
            for (int k = 0; k < max_clients; k++) {
              if (client_sockets[k] > 0) {
                if (client_finished[k] == 0) {
                  all_finished = 0;
                } else {
                  finished_count++;
                }
              }
            }
            printf("Finished: %d / %d\n", finished_count,
                   total_connected_clients);

            fflush(stdout);
            if (all_finished && total_connected_clients > 0 && !shutting_down) {
              shutting_down = 1;
              shutdown_start_time = time(NULL);
              printf("All clients finished.sending type 1 to all clients.\n");
              fflush(stdout);
              uint8_t type1_msg[2] = {1, '\n'};
              for (int k = 0; k < max_clients; k++) {
                if (client_sockets[k] > 0) {
                  ssize_t w = write(client_sockets[k], &type1_msg, 2);
                  printf("sent type 1 to client %d (socket %d), write "
                         "returned: %zd\n",
                         k, client_sockets[k], w);
                  fflush(stdout);
                  if (w != 1) {
                    perror("write type 1 message");
                  }
                }
              }
              printf("Done sending type 1 to all clients.\n");
              fflush(stdout);
            }
          } else {
            start += 1;
          }
        }
        if (start < rcvlen) {
          leftover_len[i] = rcvlen - start;
          memcpy(leftover[i], recvbuf + start, leftover_len[i]);
        } else {
          leftover_len[i] = 0;
        }
      }
    }
    if (shutting_down) {
      if (total_connected_clients == 0) {
        printf("All clients disconnected. shutting down server.\n");
        close(server_fd);
        exit(EXIT_SUCCESS);
      }
      time_t now = time(NULL);

      if (now - shutdown_start_time > SHUTDOWN_WAIT_TIMEOUT_SEC) {
        printf("Shutdown wait timeout reached. forcing shutdown\n");
        for (int i = 0; i < max_clients; i++) {
          if (client_sockets[i] > 0) {
            close(client_sockets[i]);
            client_sockets[i] = 0;
          }
        }
        close(server_fd);
        exit(EXIT_FAILURE);
      }
    }
  }
  return 0;
}
