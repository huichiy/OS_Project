#ifndef COMMON_H
#define COMMON_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// --- Game Constants ---
#define PORT 8888
#define MAX_PLAYERS 5
#define MIN_PLAYERS 3
#define BOARD_SIZE 12
#define WIN_COUNT 5
#define BUFFER_SIZE 256
#define NAME_LEN 32

// --- Shared Memory & Semaphores Names ---
#define SHM_NAME "/mega_ttt_shm"
#define SEM_MUTEX_NAME "/mega_ttt_mutex"
#define SEM_TURN_NAME_PREFIX "/mega_ttt_turn_"

// --- Data Structures ---

typedef struct {
  int id; // 1 to MAX_PLAYERS
  char name[NAME_LEN];
  char symbol;   // X, O, A, B, C
  int socket_fd; // Used by server child process
  int is_active;
} Player;

typedef struct {
  volatile char board[BOARD_SIZE][BOARD_SIZE];
  volatile int player_count;
  volatile int current_player_index; // 0 to player_count-1
  volatile int game_over;
  volatile int winner_id; // 0 if draw or none yet
  volatile int turn_count;
  Player players[MAX_PLAYERS];
} GameState;

// --- Helper Macros ---
#define ERR_EXIT(msg)                                                          \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

#endif // COMMON_H
