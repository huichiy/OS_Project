#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "common.h"

void init_game_state(GameState *gs);
int is_valid_move(GameState *gs, int row, int col);
int check_win(GameState *gs, int row, int col, char symbol);
int is_board_full(GameState *gs);

#endif // GAME_LOGIC_H
