#define _XOPEN_SOURCE 700
#include "../include/common.h"
#include "../include/game_logic.h"
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

// Globals for cleanup signal handler
int shm_fd = -1;
GameState *game_state = NULL;
// sem_t *mutex = NULL; // REMOVED
sem_t *turn_sems[MAX_PLAYERS];
sem_t *sem_scheduler = NULL; // New Scheduler Semaphore
int server_socket = -1;
int log_pipe[2]; // [0] Read (Logger), [1] Write (Others)
FILE *log_file = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t server_running = 1;

// Helper to load scores from file
void load_scores(GameState *gs) {
  FILE *fp = fopen("score.txt", "r");
  if (!fp)
    return; // No scores yet

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    int pid;
    // Look for "Winner: Player X"
    if (sscanf(line, "[%*[^]]] Winner: Player %d", &pid) == 1) {
      if (pid > 0 && pid <= MAX_PLAYERS) {
        gs->win_counts[pid - 1]++;
      }
    }
  }
  fclose(fp);
  printf("[Server] Scores loaded from score.txt\n");
}

// Helper to send logs to the logger thread
void log_msg(const char *format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // Write to pipe (Atomic for < PIPE_BUF)
  // We append a newline if missing, but vsnprintf doesn't do that auto.
  // The logger expects lines? Or just raw bytes.
  // Let's ensure it ends with newline or handle it in logger.
  // Ideally, we write one atomic chunk.
  // Ideally, we write one atomic chunk.
  if (write(log_pipe[1], buffer, strlen(buffer)) == -1) {
    perror("log_msg write failed");
  }
}

// Logger Thread Function
void *logger_thread(void *arg) {
  (void)arg;

  // Close the write end in the logger thread (it only reads)
  // close(log_pipe[1]); // Caution: threads share FDs. If we close it here,
  // it might close for main too if not careful. But this is a thread.
  // Actually, for threads, FDs are shared. We shouldn't close the write end
  // if other threads (like main or scheduler) need to write to it.

  log_file = fopen("game_log.txt", "a");
  if (!log_file) {
    perror("Failed to open log file");
    pthread_exit(NULL);
  }
  printf("[Logger] Thread started. Writing to game_log.txt\n");

  char buffer[LOG_BUFFER_SIZE];
  ssize_t n;
  while ((n = read(log_pipe[0], buffer, sizeof(buffer) - 1)) > 0) {
    buffer[n] = '\0';
    if (log_file) {
      fprintf(log_file, "%s", buffer);
      fflush(log_file); // Ensure it's written immediately
    }

    // Also print to stdout for debugging visibility
    // printf("%s", buffer);
  }

  fclose(log_file);
  return NULL;
}

// Round Robin Scheduler Thread
void *scheduler_thread(void *arg) {
  (void)arg;
  printf("[Scheduler] Thread started. Controlling turn order.\n");

  while (1) {
    // Wait for a player to finish their turn
    // Wait for a player to finish their turn
    if (sem_wait(sem_scheduler) == -1) {
      if (errno != EINTR)
        perror("sem_wait scheduler");
      continue;
    }

    pthread_mutex_lock(&game_state->game_mutex);
    if (game_state->game_over) {
      pthread_mutex_unlock(&game_state->game_mutex);
      pthread_mutex_unlock(
          &game_state->game_mutex); // Unlock logic for wait (double post in old
                                    // code?)
      // Actually old code had double sem_post? Line 79 was sem_post(mutex).
      // Ah, wait logic below needs careful lock management.

      printf("[Scheduler] Game Over detected. Waiting for reset...\n");
      // Wait for Main to reset game_over flag
      while (1) {
        usleep(POLL_INTERVAL_US / 2); // 100ms
        pthread_mutex_lock(&game_state->game_mutex);
        if (!game_state->game_over) {
          pthread_mutex_unlock(&game_state->game_mutex);
          printf("[Scheduler] Reset detected. Resuming for new game.\n");
          break;
        }
        pthread_mutex_unlock(&game_state->game_mutex);
      }
      // Continue loop for next game
      continue;
    }

    // Determine next player (Round Robin)
    int current_id = game_state->current_player_index; // 0-based index
    int next_id = (current_id + 1) % game_state->player_count;

    // Update state
    game_state->current_player_index = next_id;
    pthread_mutex_unlock(&game_state->game_mutex);

    printf("[Scheduler] Signaling Player %d (Index %d) to go next.\n",
           next_id + 1, next_id);
    log_msg("[Scheduler] Player %d (%d) -> Player %d (%d)\n", current_id + 1,
            current_id, next_id + 1, next_id);
    if (sem_post(turn_sems[next_id]) == -1) {
      perror("sem_post turn_sems");
    }
  }
  return NULL;
}

// Cleanup function
void cleanup() {
  printf("\n[Server] Cleaning up resources...\n");

  // Close log pipe
  close(log_pipe[0]);
  close(log_pipe[1]);

  // Unlink socket
  unlink(SOCKET_PATH);
  if (game_state)
    munmap(game_state, sizeof(GameState));
  if (shm_fd != -1)
    close(shm_fd);
  shm_unlink(SHM_NAME);

  if (game_state) {
    pthread_mutex_destroy(&game_state->game_mutex);
  }
  // if (mutex) { sem_close(mutex); sem_unlink(SEM_MUTEX_NAME); }

  if (sem_scheduler) {
    sem_close(sem_scheduler);
    sem_unlink(SEM_SCHEDULER_NAME);
  }

  for (int i = 0; i < MAX_PLAYERS; i++) {
    char sem_name[64];
    snprintf(sem_name, sizeof(sem_name), "%s%d", SEM_TURN_NAME_PREFIX, i);
    sem_unlink(sem_name);
  }

  if (server_socket != -1)
    close(server_socket);
  // Kill all children if necessary (handled by OS usually on group exit)
}

// Signal Handler
void handle_signal(int sig) {
  printf(
      "\n[Server] Shutdown Signal Received. Initiating graceful shutdown...\n");
  server_running = 0;
  // Interrupt any blocking calls if necessary (though SIGINT usually interrupts
  // accept/read)
}

void handle_sigchld(int sig) {
  (void)sig; // unused
  // Reap all dead children
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

void handle_client(int player_id, int client_sock) {
  // Child process logic
  GameState *gs = game_state; // Shared memory mapping is inherited

  Player *me = &gs->players[player_id];
  printf("[Player %d] Handler started. Symbol: %c\n", me->id, me->symbol);

  char buffer[BUFFER_SIZE];

  int last_turn_count = gs->turn_count;

  while (1) {
    // --- POLLING LOOP FOR TURN OR UPDATES ---
    while (1) {
      // Try to acquire Turn Semaphore
      int ret = sem_trywait(turn_sems[player_id]);
      if (ret == 0) {
        // Got the semaphore! It is my turn.
        // Check game_over status immediately for debugging
        pthread_mutex_lock(&gs->game_mutex);
        int go_debug = gs->game_over;
        pthread_mutex_unlock(&gs->game_mutex);
        // printf("[DEBUG] Player %d acquired semaphore (My Turn).
        // game_over=%d\n",
        //        player_id + 1, go_debug);
        break;
      }

      // Check for updates
      pthread_mutex_lock(&gs->game_mutex);
      int current_turn_count = gs->turn_count;
      int game_over = gs->game_over;
      pthread_mutex_unlock(&gs->game_mutex);

      if (game_over) {
        // Break the polling loop to handle game over logic below
        break;
      }

      if (current_turn_count > last_turn_count) {
        // printf("[DEBUG] Player %d sending spectator update (Turn %d > %d)\n",
        //        player_id + 1, current_turn_count, last_turn_count);
        last_turn_count = current_turn_count;

        // Send Board State (Spectator View)
        char board_str[2048];
        int offset = 0;
        offset +=
            sprintf(board_str + offset, "BOARD %c (Spectating)\n", me->symbol);
        offset += sprintf(board_str + offset, "   ");
        for (int c = 0; c < BOARD_SIZE; c++)
          offset += sprintf(board_str + offset, "%2d ", c);
        offset += sprintf(board_str + offset, "\n");

        for (int r = 0; r < BOARD_SIZE; r++) {
          offset += sprintf(board_str + offset, "%2d ", r);
          for (int c = 0; c < BOARD_SIZE; c++) {
            offset += sprintf(board_str + offset, "[%c]", gs->board[r][c]);
          }
          offset += sprintf(board_str + offset, "\n");
        }
        offset += sprintf(board_str + offset, "END\n");
        if (send(client_sock, board_str, strlen(board_str), 0) == -1) {
          perror("send board_str");
        }
      }

      // Sleep a bit to avoid CPU spin
      // Sleep a bit to avoid CPU spin
      usleep(POLL_INTERVAL_US);
    }

    // --- MY TURN or GAME OVER ---
    pthread_mutex_lock(&gs->game_mutex);
    int game_over = gs->game_over;
    int winner = gs->winner_id;
    pthread_mutex_unlock(&gs->game_mutex);

    if (game_over) {
      printf("[DEBUG] Player %d entering Game Over sequence.\n", me->id);
      // Send final board
      char final_board[2048];
      int off = 0;
      off += sprintf(final_board + off, "BOARD %c\n", me->symbol);
      off += sprintf(final_board + off, "   ");
      for (int c = 0; c < BOARD_SIZE; c++)
        off += sprintf(final_board + off, "%2d ", c);
      off += sprintf(final_board + off, "\n");
      for (int r = 0; r < BOARD_SIZE; r++) {
        off += sprintf(final_board + off, "%2d ", r);
        for (int c = 0; c < BOARD_SIZE; c++) {
          off += sprintf(final_board + off, "[%c]", gs->board[r][c]);
        }
        off += sprintf(final_board + off, "\n");
      }
      off += sprintf(final_board + off, "END\n");

      printf("[DEBUG] Player %d sending Final Board...\n", me->id);
      send(client_sock, final_board, strlen(final_board), 0);
      printf("[DEBUG] Player %d sent Final Board.\n", me->id);

      // Send Game Over
      snprintf(buffer, sizeof(buffer), "GAME_OVER %d\n", winner);
      printf("[DEBUG] Player %d sending GAME_OVER...\n", me->id);
      send(client_sock, buffer, strlen(buffer), 0);
      printf("[DEBUG] Player %d sent GAME_OVER.\n", me->id);

      // Propagate signal -> To SCHEDULER (which will stop) or next player?
      // During Game Over processing, we DO NOT need to signal scheduler again.
      // The winner already did (or the turn-finisher).
      // Excess signals cause the scheduler to skip turns in the next game.
      // sem_post(sem_scheduler); // REMOVED
      printf("[DEBUG] Player %d Game Over processed. Waiting for reset.\n",
             me->id);
      printf("[DEBUG] Player %d propagated signal. Exiting loop.\n", me->id);

      // Wait for Game Reset
      printf("[Player %d] Waiting for new game...\n", me->id);
      while (1) {
        sleep(1);
        pthread_mutex_lock(&gs->game_mutex);
        if (!gs->game_over) {
          pthread_mutex_unlock(&gs->game_mutex);
          break;
        }
        pthread_mutex_unlock(&gs->game_mutex);
      }
      printf("[Player %d] New game started! Resetting local state.\n", me->id);
      last_turn_count = -1; // Force board refresh
      continue;             // Restart the outer 'while(1)' loop
    }

    // Send Board State (My Turn View)
    char board_str[2048];
    int offset = 0;
    offset += sprintf(board_str + offset, "BOARD %c\n", me->symbol);

    // Add column headers
    offset += sprintf(board_str + offset, "   ");
    for (int c = 0; c < BOARD_SIZE; c++)
      offset += sprintf(board_str + offset, "%2d ", c);
    offset += sprintf(board_str + offset, "\n");

    for (int r = 0; r < BOARD_SIZE; r++) {
      offset += sprintf(board_str + offset, "%2d ", r);
      for (int c = 0; c < BOARD_SIZE; c++) {
        offset += sprintf(board_str + offset, "[%c]", gs->board[r][c]);
      }
      offset += sprintf(board_str + offset, "\n");
    }
    offset += sprintf(board_str + offset, "END\n");
    send(client_sock, board_str, strlen(board_str), 0);

    // Send YOUR_TURN Command to prompt input
    char *turn_cmd = "YOUR_TURN\n";
    send(client_sock, turn_cmd, strlen(turn_cmd), 0);

    // Update tracking
    last_turn_count = gs->turn_count;

    // Receive Move
    memset(buffer, 0, BUFFER_SIZE);
    int bytes = recv(client_sock, buffer, BUFFER_SIZE, 0);
    if (bytes <= 0) {
      log_msg("[Connection] Player %d disconnected.\n", me->id);
      break; // Client disconnected
    }

    int row, col;
    if (sscanf(buffer, "%d %d", &row, &col) == 2) {
      pthread_mutex_lock(&gs->game_mutex);
      if (is_valid_move(gs, row, col)) {
        gs->board[row][col] = me->symbol;
        gs->turn_count++;
        // removed last_turn_count update so I get the spectator update showing
        // my own move
        // printf("[DEBUG] Player %d placed at %d, %d\n", me->id, row, col);
        log_msg("[Gameplay] Player %d placed '%c' at (%d, %d)\n", me->id,
                me->symbol, row, col);

        if (check_win(gs, row, col, me->symbol)) {
          gs->game_over = 1;
          gs->winner_id = me->id;
          printf("[Server] Player %d WINS!\n", me->id);
        } else if (is_board_full(gs)) {
          gs->game_over = 1;
          gs->winner_id = 0; // Draw
          printf("[Server] Draw!\n");
        }

        // Next player logic handled by Scheduler
        // gs->current_player_index = (gs->current_player_index + 1) %
        // gs->player_count; // REMOVED: Scheduler does this

        // printf("[DEBUG] Player %d finished. Signaling Scheduler.\n", me->id);
        // printf("[DEBUG] Player %d finished. Signaling Scheduler.\n", me->id);
        if (sem_post(sem_scheduler) == -1)
          perror("sem_post scheduler"); // Signal Scheduler

      } else {
        // Invalid move, signal SAME player to try again
        char *msg = "INVALID\n";
        send(client_sock, msg, strlen(msg), 0);
        printf("[DEBUG] Player %d Invalid Move. Posting self.\n", me->id);
        sem_post(turn_sems[player_id]); // Signal myself again
      }
      pthread_mutex_unlock(&gs->game_mutex);
    } else {
      sem_post(turn_sems[player_id]); // Try again
    }
  }

  close(client_sock);
  exit(0);
}

int main(int argc, char *argv[]) {
  signal(SIGINT, handle_signal);

  // Register SIGCHLD handler to reap zombies
  struct sigaction sa;
  sa.sa_handler = &handle_sigchld;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, 0) == -1) {
    ERR_EXIT("sigaction");
  }

  // Parse arguments
  int players_needed = MIN_PLAYERS; // Default
  if (argc > 1) {
    players_needed = atoi(argv[1]);
    if (players_needed < MIN_PLAYERS || players_needed > MAX_PLAYERS) {
      fprintf(stderr, "Usage: %s [num_players 3-5]\n", argv[0]);
      exit(1);
    }
  }

  printf("[Server] Starting Mega Tic-Tac-Toe Server for %d players...\n",
         players_needed);

  // 0. Setup Logger Pipe
  if (pipe(log_pipe) == -1) {
    ERR_EXIT("pipe");
  }

  // 1. Setup Shared Memory
  shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
  if (shm_fd == -1)
    ERR_EXIT("shm_open");
  ftruncate(shm_fd, sizeof(GameState));

  game_state =
      mmap(0, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
  if (game_state == MAP_FAILED)
    ERR_EXIT("mmap");

  // Initialize Game State
  // Initialize Game State
  init_game_state(game_state);
  // Reset win counts
  memset((void *)game_state->win_counts, 0, sizeof(game_state->win_counts));
  load_scores(game_state); // Load historical data

  game_state->player_count = players_needed;

  // 2. Setup Mutex (Process Shared)
  // sem_unlink(SEM_MUTEX_NAME);
  // mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);
  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&game_state->game_mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);

  for (int i = 0; i < players_needed; i++) {
    char sem_name[64];
    snprintf(sem_name, sizeof(sem_name), "%s%d", SEM_TURN_NAME_PREFIX, i);
    sem_unlink(sem_name);
    // Initialize to 0 (locked)
    turn_sems[i] = sem_open(sem_name, O_CREAT, 0666, 0);
    if (turn_sems[i] == SEM_FAILED)
      ERR_EXIT("sem_open turn");
  }

  // Setup Scheduler Semaphore
  sem_unlink(SEM_SCHEDULER_NAME);
  sem_scheduler = sem_open(SEM_SCHEDULER_NAME, O_CREAT, 0666, 0);
  if (sem_scheduler == SEM_FAILED)
    ERR_EXIT("sem_open scheduler");

  // 3. Setup Socket (Unix Domain)
  struct sockaddr_un address;
  server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_socket == -1) // Corrected error check for socket
    ERR_EXIT("socket");

  // Clean up old socket file if it exists
  unlink(SOCKET_PATH);

  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1);

  if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
    ERR_EXIT("bind");

  // Set permissions so clients can access it
  chmod(SOCKET_PATH, 0666);

  if (listen(server_socket, 5) < 0)
    ERR_EXIT("listen");

  printf("[Server] Listening on %s. Waiting for players...\n", SOCKET_PATH);

  // 4. Accept Players
  int connected_count = 0;
  char symbols[] = {'X', 'O', 'A', 'B', 'C'};

  while (connected_count < players_needed && server_running) {
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    int new_socket =
        accept(server_socket, (struct sockaddr *)&client_addr, &addrlen);
    if (new_socket < 0)
      ERR_EXIT("accept");

    printf("[Server] Player %d connected!\n", connected_count + 1);
    log_msg("[Connection] Player %d connected from %s\n", connected_count + 1,
            "local");

    pthread_mutex_lock(&game_state->game_mutex);
    game_state->players[connected_count].id = connected_count + 1; // 1-based ID
    game_state->players[connected_count].socket_fd = new_socket;
    game_state->players[connected_count].symbol = symbols[connected_count];
    game_state->players[connected_count].is_active = 1;
    pthread_mutex_unlock(&game_state->game_mutex);

    // Fork child process for this player
    pid_t pid = fork();
    if (pid == 0) {
      // Child
      handle_client(connected_count, new_socket);
    } else if (pid < 0) {
      ERR_EXIT("fork");
    }

    // Parent continues
    close(new_socket); // Parent doesn't need this specific socket fd
    connected_count++;
  }

  printf("[Server] All players connected! Starting game...\n");
  log_msg("[Game] All players connected. Game Starting.\n");

  // Start the Logger Thread
  pthread_t logger_tid;
  if (pthread_create(&logger_tid, NULL, logger_thread, NULL) != 0) {
    ERR_EXIT("pthread_create logger");
  }

  // Start the Scheduler Thread
  pthread_t scheduler_tid;
  if (pthread_create(&scheduler_tid, NULL, scheduler_thread, NULL) != 0) {
    ERR_EXIT("pthread_create scheduler");
  }

  // Start the First Player
  // We need to kickstart the loop.
  // Option A: Signal Scheduler? No, scheduler waits for someone to finish.
  // Option B: Just signal Player 1 directly to start.
  // Let's signal Player 1 directly, as they are "Index 0".
  // The Scheduler picks up only after a turn is COMPLETED.
  sem_post(turn_sems[0]);

  // Parent Process Monitor Loop
  while (server_running) {
    sleep(1);
    if (!server_running)
      break;
    pthread_mutex_lock(&game_state->game_mutex);
    if (game_state->game_over) {
      // Capture state atomically while holding lock
      int winner = game_state->winner_id;
      int turns = game_state->turn_count;
      int total_wins = 0;

      if (winner > 0 && winner <= game_state->player_count) {
        game_state->win_counts[winner - 1]++; // Update in-memory score
        total_wins = game_state->win_counts[winner - 1];
      }

      char winner_symbol = '?';
      if (winner > 0 && winner <= game_state->player_count) {
        winner_symbol = game_state->players[winner - 1].symbol;
      }
      pthread_mutex_unlock(&game_state->game_mutex);

      printf("[Main] Game Over detected. Writing to score.txt...\n");
      log_msg("[Game] Game Over. Winner: %d\n", winner);

      FILE *fp = fopen("score.txt", "a");
      if (fp) {
        time_t now = time(NULL);
        char *time_str = ctime(&now);
        time_str[strlen(time_str) - 1] = '\0'; // Remove newline

        if (winner == 0) {
          fprintf(fp, "[%s] Draw! Total Turns: %d\n", time_str, turns);
        } else {
          fprintf(fp,
                  "[%s] Winner: Player %d (%c) | Total Turns: %d | Total Wins: "
                  "%d\n",
                  time_str, winner, winner_symbol, turns, total_wins);
        }
        fclose(fp);
        printf("[Main] Score saved.\n");
      } else {
        perror("[Main] Failed to open score.txt");
      }

      printf("[Main] Cleaning up in 5 seconds...\n");
      sleep(5);

      // RESET GAME STATE
      pthread_mutex_lock(&game_state->game_mutex);
      printf("[Main] Resetting game state for new game...\n");
      memset((void *)game_state->board, ' ', sizeof(game_state->board));
      game_state->turn_count = 0;
      game_state->winner_id = 0;
      game_state->current_player_index = 0;
      game_state->game_over = 0;
      pthread_mutex_unlock(&game_state->game_mutex);

      printf("[Main] Draining semaphores to ensure clean state...\n");
      // Drain Scheduler Semaphore
      while (sem_trywait(sem_scheduler) == 0)
        ;

      // Drain Player Turn Semaphores
      for (int i = 0; i < players_needed; i++) {
        while (sem_trywait(turn_sems[i]) == 0)
          ;
      }

      printf("[Main] Game State Reset. Signaling Player 1 to start.\n");
      // Signal Player 1 to start
      sem_post(turn_sems[0]);
    }
    pthread_mutex_unlock(&game_state->game_mutex);
  }

  // Graceful Exit
  if (game_state) {
    printf("\n[Server] Final Leaderboard:\n");
    printf("--------------------------------\n");
    for (int i = 0; i < game_state->player_count; i++) {
      printf("Player %d: %d Wins\n", i + 1, game_state->win_counts[i]);
    }
    printf("--------------------------------\n");
  }

  // Cancel and Join Threads
  pthread_cancel(scheduler_tid);
  pthread_join(scheduler_tid, NULL);
  pthread_cancel(logger_tid);
  pthread_join(logger_tid, NULL);

  cleanup();
  return 0;
}
