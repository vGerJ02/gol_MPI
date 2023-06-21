#ifndef RENDER_H_
#define RENDER_H_

#include "./game.h"
#include "./mpi_logic.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

extern bool Graphical_Mode;
void render_board(SDL_Renderer *renderer, board_t *board,
                  unsigned char neighbors[D_ROW_NUM][D_COL_NUM],
                  data_mpi_t data);

void render_running_state(SDL_Renderer *renderer, board_t *board);

void render_square(SDL_Renderer *renderer, int pos_x, int pos_y,
                   board_t *board);

void render_pause_state(SDL_Renderer *renderer, board_t *board);

void pause_square(SDL_Renderer *renderer, int pos_x, int pos_y, board_t *board);

#endif // RENDER_H_
