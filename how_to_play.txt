Mega Tic-Tac-Toe
================

A multiplayer (3-5 players) text-based board game implementation in C.
Based on a 12x12 Grid with a 5-in-a-row win condition.

Supported Mode
--------------
This project supports Single-machine (IPC) and separate machines (TCP Sockets).
The architecture uses a Hybrid Model:
- Multiprocessing (fork) for player connection handling.
- Multithreading (pthread) for server logging.
- Shared Memory & Semaphores for game state synchronization.

Compilation
-----------
To compile the project, run:

    make all

Execution
---------
1. Start the Server:
   You can specify the number of players (default is 3).
   
    ./server 3

2. Start Clients:
   Open separate terminal windows for each player.
   
    ./client 127.0.0.1

How to Play
-----------
1. The game waits for all players to connect.
2. Once started, Player 1 (Symbol 'X') goes first.
3. The server displays the board to the current player.
4. Enter your move as two integers: Row and Column.
   Example: 
   
     Your Turn! Enter Row and Col: 0 0

5. The first player to align 5 symbols horizontally, vertically, or diagonally wins.

Files
-----
- src/server.c: Main server logic (Fork + Threads + IPC).
- src/client.c: Client logic (Socket communication).
- src/game_logic.c: Game rules (Win check, Board helper).
- include/common.h: Shared constants and data structures.
- Makefile: Build script.
- README.txt: This file.
