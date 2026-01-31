#include "../include/common.h"

void init_game_state(GameState *gs) {
  memset((void *)gs->board, ' ', sizeof(gs->board));
  gs->player_count = 0;
  gs->current_player_index = 0;
  gs->game_over = 0;
  gs->winner_id = 0;
  gs->turn_count = 0;
}

int is_valid_move(GameState *gs, int row, int col) {
  if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
    return 0; // Out of bounds
  }
  if (gs->board[row][col] != ' ') {
    return 0; // Already occupied
  }
  return 1;
}

int check_win(GameState *gs, int row, int col, char symbol) {
  // Check 4 directions: Horizontal, Vertical, Diagonal 1 (\), Diagonal 2 (/)
  int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

  for (int d = 0; d < 4; d++) {
    int count = 1;
    int dr = directions[d][0];
    int dc = directions[d][1];

    // Check forward
    for (int i = 1; i < WIN_COUNT; i++) {
      int r = row + i * dr;
      int c = col + i * dc;
      if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE &&
          gs->board[r][c] == symbol) {
        count++;
      } else {
        break;
      }
    }

    // Check backward
    for (int i = 1; i < WIN_COUNT; i++) {
      int r = row - i * dr;
      int c = col - i * dc;
      if (r >= 0 && r < BOARD_SIZE && c >= 0 && c < BOARD_SIZE &&
          gs->board[r][c] == symbol) {
        count++;
      } else {
        break;
      }
    }

    if (count >= WIN_COUNT) {
      return 1;
    }
  }
  return 0;
}

int is_board_full(GameState *gs) {
  if (gs->turn_count >= BOARD_SIZE * BOARD_SIZE) {
    return 1;
  }
  return 0;
}
