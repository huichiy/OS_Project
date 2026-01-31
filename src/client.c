#define _XOPEN_SOURCE 700
#include "../include/common.h"
#include <unistd.h>

int main(int argc, char *argv[]) {
  int sock = 0;
  struct sockaddr_un serv_addr;
  char buffer[BUFFER_SIZE];
  char acc_buffer[4096]; // Accumulation buffer
  int acc_len = 0;

  if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    printf("\n Socket creation error \n");
    return -1;
  }

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;
  strncpy(serv_addr.sun_path, SOCKET_PATH, sizeof(serv_addr.sun_path) - 1);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    perror("Connection Failed");
    return -1;
  }

  printf("Connected to Mega Tic-Tac-Toe Server at %s\n", SOCKET_PATH);
  printf("Waiting for game to start...\n");

  memset(acc_buffer, 0, sizeof(acc_buffer));

  while (1) {
    memset(buffer, 0, BUFFER_SIZE);
    int valread = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (valread <= 0) {
      printf("Server disconnected.\n");
      break;
    }

    // Handle Immediate Commands (Small packets like INVALID or GAME_OVER might
    // come alone or mixed) Simplified Logic: Determine type by prefix. But
    // framing means we should buffer everything until delimiters. However,
    // GAME_OVER and INVALID often come alone.

    // Append to accumulation buffer
    if (acc_len + valread < sizeof(acc_buffer)) {
      memcpy(acc_buffer + acc_len, buffer, valread);
      acc_len += valread;
      acc_buffer[acc_len] = '\0';
      // printf("[DEBUG-CLIENT] Recv %d bytes. Buffer: %s\n", valread,
      // acc_buffer);
    } else {
      // Buffer overflow safety
      acc_len = 0;
      memset(acc_buffer, 0, sizeof(acc_buffer));
      printf("Error: Message too large.\n");
    }

    // Check for Markers
    char *end_ptr = strstr(acc_buffer, "END\n");
    char *game_over_ptr = strstr(acc_buffer, "GAME_OVER");
    char *invalid_ptr = strstr(acc_buffer, "INVALID");
    char *turn_ptr = strstr(acc_buffer, "YOUR_TURN");

    if (end_ptr)
      printf("[DEBUG-CLIENT] Found END\n");
    if (turn_ptr)
      printf("[DEBUG-CLIENT] Found YOUR_TURN\n");

    if (game_over_ptr) {
      // Check if we have a final board update pending before the Game Over msg
      if (end_ptr && end_ptr < game_over_ptr) {
        *end_ptr = '\0';
        printf("\033[H\033[J");
        printf("%s", acc_buffer);
      }

      int winner_id;
      sscanf(game_over_ptr, "GAME_OVER %d", &winner_id);
      if (winner_id == 0) {
        printf("\n--- GAME OVER: DRAW ---\n");
      } else {
        printf("\n--- GAME OVER: Player %d WINS! ---\n", winner_id);
      }
      // break; // Removed to support multi-game.
      // Reset buffer for next game
      memset(acc_buffer, 0, sizeof(acc_buffer));
      acc_len = 0;
      printf("Waiting for next game...\n");
    }

    if (invalid_ptr) {
      printf("Invalid Move! Try again (Row Col): ");
      // Reset buffer after handling
      memset(acc_buffer, 0, sizeof(acc_buffer));
      acc_len = 0;
      // Get Input immediately
      char input[64];
      if (fgets(input, sizeof(input), stdin) != NULL) {
        send(sock, input, strlen(input), 0);
      }
      continue;
    }

    if (end_ptr) {
      // Cut the string at END
      *end_ptr = '\0';

      // Clear Screen
      printf("\033[H\033[J");

      // Print Board
      printf("%s", acc_buffer);

      // Reset Buffer
      memset(acc_buffer, 0, sizeof(acc_buffer));
      acc_len = 0;
    }

    if (turn_ptr) {
      printf("[DEBUG-CLIENT] Processing YOUR_TURN prompt...\n");
      printf("\nYour Turn! Enter Row and Col (e.g., 5 5): ");
      memset(acc_buffer, 0, sizeof(acc_buffer)); // Clear command from buffer
      acc_len = 0;

      // Get Input
      char input[64];
      if (fgets(input, sizeof(input), stdin) != NULL) {
        send(sock, input, strlen(input), 0);
        printf("Move sent. Waiting for other players...\n");
      }
    }
  }

  close(sock);
  return 0;
}
