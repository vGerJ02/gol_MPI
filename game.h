#ifndef GAME_H_
#define GAME_H_

#define D_COL_NUM 4000 // size of game universe, only windowed is shown.
#define D_ROW_NUM 4000
#define ALIVE 1
#define DEAD 0

#define RUNNING_STATE 0
#define PAUSE_STATE 1

// #define GRAPHICAL_MODE

typedef struct {
  unsigned char cell_state[D_ROW_NUM][D_COL_NUM];
  int game_state;
  int COL_NUM;
  int ROW_NUM;
  int CELL_WIDTH;
  int CELL_HEIGHT;
} board_t;

#endif // GAME_H_
