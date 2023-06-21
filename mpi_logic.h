#include "./game.h"

typedef struct {
  unsigned char *cell_state;
  unsigned char *neighbors;
  int COL_NUM;
  int ROW_NUM;
} board_mpi_t;

void send_borders(unsigned char *board, int cols, int rows, int rank, int size);
void count_neighbors_of_1_row(unsigned char *row, int cols, int *neighbors,
                              int offset);
void distributeRows(board_t *fullboard);
void count_neighbors_spherical_world_mpi(
    unsigned char *board, int cols, int rows,
    unsigned char neighbors[D_COL_NUM][D_ROW_NUM], int rank, int size);
void count_neighbors_spherical_world_mpi_v2(unsigned char *board, int cols,
                                            int rows, unsigned char *neighbors,
                                            int rank, int size);
void evolve_mpi(unsigned char *cell_state, unsigned char *neighbors, int rows,
                int cols);
