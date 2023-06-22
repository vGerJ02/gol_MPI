#include "./game.h"

typedef struct {
  int *sendcounts;
  int *displs;
  int rows_per_process, remaining_rows;
} data_mpi_t;

void distributeRows(board_t *board, data_mpi_t data);
void send_borders(unsigned char *board, int cols, int rows, int rank, int size);
void count_neighbors_of_1_row(unsigned char *row, int cols,
                              unsigned char *neighbors, int offset);
void count_neighbors_spherical_world_mpi(
    unsigned char *board, int cols, int rows,
    unsigned char neighbors[D_COL_NUM][D_ROW_NUM], int rank, int size);
void count_neighbors_spherical_world_mpi_v2(unsigned char *board, int cols,
                                            int rows, unsigned char *neighbors,
                                            int rank, int size);
void evolve_mpi(unsigned char *cell_state, unsigned char *neighbors, int rows,
                int cols);

void basic_load_balance(data_mpi_t *data, int size, int rows, int cols);
