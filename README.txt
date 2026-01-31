Mega Tic-Tac-Toe
================

A multiplayer (3-5 players) text-based board game implementation in C.
Based on a 12x12 Grid with a 5-in-a-row win condition.

Features
--------
- **Single Machine Mode**: Unix Domain Sockets for local IPC.
- **Round Robin Scheduler**: Dedicated thread for managing turn order.
- **Concurrent Logging**: Pipe-based thread-safe logging to `game_log.txt`.
- **Multi-Game Support**: Server automatically resets and restarts new games.
- **Architecture**: Hybrid Model (Forked Processes + Threads + Shared Memory).

Compilation
-----------
To compile the project, run:

    make clean && make all

Execution
---------
Since this uses Unix Domain Sockets, all processes must run on the same machine.

1. Start the Server:
   Specify the number of players (3-5).
   
    ./server 3

2. Start Clients:
   Open separate terminal windows for each player. No arguments are needed.
   
    ./client

How to Play
-----------
1. The game waits for all players to connect.
2. Once started, Player 1 (Symbol 'X') goes first.
3. The server displays the board to the current player.
4. Enter your move as two integers: Row and Column.
   Example: 
   
     Your Turn! Enter Row and Col (e.g., 5 5): 0 0

5. The first player to align 5 symbols horizontally, vertically, or diagonally wins.
6. After a game ends, the server resets automatically. Keep your client windows open to play the next game!

Files
-----
- src/server.c: Main server logic (Fork + Scheduler Thread + Logger Thread + IPC).
- src/client.c: Client logic (Unix Domain Socket communication).
- src/game_logic.c: Game rules (Win check, Board helper).
- include/common.h: Shared constants and data structures.
- Makefile: Build script.
- README.txt: This file.
