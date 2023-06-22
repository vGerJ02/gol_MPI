#include "./logic.h"
#include "./game.h"
#include "./mpi_logic.h"
#include <mpich-x86_64/mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void count_neighbors(board_t *board,
                     unsigned char neighbors[D_COL_NUM][D_ROW_NUM]) {
  count_neighbors_spherical_world(board, neighbors);
}

void printProcWorkToFile(unsigned char *subarray, int rank, int rows,
                         int cols) {
  char filename[100];
  sprintf(filename, "output_%d.txt", rank);
  FILE *file = fopen(filename, "a");
  if (file != NULL) {
    for (int i = 0; i < rows; i++) {
      fprintf(file, "ROW %d: ", i);
      for (int j = 0; j < cols; j++) {

        fprintf(file, "%d ", subarray[i * cols + j]);
      }
      fprintf(file, "\n");
    }
    fprintf(file, "\n");
    fclose(file);
  } else {
    printf("error file\n");
  }
}
void distributeRows(board_t *fullboard, data_mpi_t data) {
  int rank, size;

  unsigned char *subarray;
  unsigned char *recv_buffer = NULL;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // copy so it is not 4000x4000
  unsigned char board[fullboard->ROW_NUM][fullboard->COL_NUM];
  for (int i = 0; i < fullboard->ROW_NUM; i++)
    for (int j = 0; j < fullboard->COL_NUM; j++)
      board[i][j] = fullboard->cell_state[i][j];

  int local_size = data.sendcounts[rank];
  subarray = (unsigned char *)malloc(local_size * sizeof(unsigned char));

  MPI_Scatterv(board, data.sendcounts, data.displs, MPI_UNSIGNED_CHAR, subarray,
               local_size, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

  //  Count neighbors (and sends borders)
  int local_rows = (rank < data.remaining_rows) ? data.rows_per_process + 1
                                                : data.rows_per_process;
  unsigned char *partNeighbors = (unsigned char *)malloc(
      local_rows * fullboard->COL_NUM * sizeof(unsigned char));

  printf("[%d] Processing %d rows of %d\n", rank, local_rows,
         fullboard->COL_NUM);

  printProcWorkToFile(subarray, rank, local_rows, fullboard->COL_NUM);
  count_neighbors_spherical_world_mpi_v2(subarray, fullboard->COL_NUM,
                                         local_rows, partNeighbors, rank, size);
  evolve_mpi(subarray, partNeighbors, local_rows, fullboard->COL_NUM);

  if (rank == 0) {
    recv_buffer = (unsigned char *)malloc(
        fullboard->ROW_NUM * fullboard->COL_NUM * sizeof(unsigned char));
  }

  MPI_Gatherv(subarray, local_size, MPI_UNSIGNED_CHAR, recv_buffer,
              data.sendcounts, data.displs, MPI_UNSIGNED_CHAR, 0,
              MPI_COMM_WORLD);

  // convert recv_buffer to 2D array and print to file
  if (rank == 0) {
    FILE *ptr = fopen("iterations.txt", "a");
    for (int i = 0; i < fullboard->ROW_NUM; i++) {
      for (int j = 0; j < fullboard->COL_NUM; j++) {
        fullboard->cell_state[i][j] = recv_buffer[i * fullboard->COL_NUM + j];
        fprintf(ptr, "%d ", fullboard->cell_state[i][j]);
      }
      fprintf(ptr, "\n");
    }
    fprintf(ptr, "\n");
    fclose(ptr);

    free(recv_buffer);
  }
  MPI_Barrier(MPI_COMM_WORLD);
  free(partNeighbors);
  free(subarray);
}

// board and neighbors
void evolve_mpi(unsigned char *cell_state, unsigned char *neighbors, int rows,
                int cols) {
  int index;
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      index = i * cols + j;
      // underopulation case
      if (neighbors[index] < 2)
        cell_state[i * cols + j] = DEAD;
      // birth case
      else if (neighbors[index] == 3)
        cell_state[i * cols + j] = ALIVE;
      // overpopulation case
      else if (neighbors[index] > 3)
        cell_state[i * cols + j] = DEAD;
      // survival case is implicit, as only cells with 2 or 3 neighbors will
      // survive.
    }
  }
}

void click_on_cell(board_t *board, int row, int column) {
  if (board->game_state == PAUSE_STATE) {
    if (board->cell_state[column][row] == DEAD) {
      board->cell_state[column][row] = ALIVE;
      printf("Cell (%d,%d) set to ALIVE.\n", column, row);
    } else if (board->cell_state[column][row] == ALIVE) {
      board->cell_state[column][row] = DEAD;
      printf("Cell (%d,%d) set to DEAD.\n", column, row);
    }
  } else if (board->game_state == RUNNING_STATE)
    printf("Game is running, hit space to pause and edit.\n");
}

void count_neighbors_of_1_row(unsigned char *row, int cols,
                              unsigned char *neighbors, int offset) {
  // Counts the neighbors in this row in spherical
  // offset indicates if first row or last
  int i_prev, i_next;
  for (int i = 0; i < cols; i++) {
    i_prev = (i > 0) ? i - 1 : cols - 1;
    i_next = (i < cols - 1) ? i + 1 : 0;
    if (row[i] == ALIVE)
      neighbors[i + offset] += 1;
    if (row[i_prev] == ALIVE)
      neighbors[i + offset] += 1;
    if (row[i_next] == ALIVE)
      neighbors[i + offset] += 1;
  }
}
void send_borders(unsigned char *board, int cols, int rows, int rank,
                  int size) {
  MPI_Request r1, r2;
  MPI_Status st, st2;
  int dest;
  unsigned char *neighbors_to_send =
      (unsigned char *)malloc(2 * cols * sizeof(unsigned char));
  // Initializes to 0
  memset(neighbors_to_send, 0, 2 * cols * sizeof(unsigned char));

  // Count neighbors to send
  count_neighbors_of_1_row(&board[0], cols, neighbors_to_send, 0);
  dest = (rank - 1 < 0) ? size - 1 : rank - 1;

  // send the first row
  MPI_Isend(neighbors_to_send, cols, MPI_UNSIGNED_CHAR, dest, 0, MPI_COMM_WORLD,
            &r1);

  // Set dest to next border
  dest = (rank + 1 >= size) ? 0 : rank + 1;
  if (rows > 1) {
    // Count neighbors of last row
    count_neighbors_of_1_row(&board[(rows - 1) * cols], cols, neighbors_to_send,
                             cols);

    // MPI send neighbors from cols to 2*cols
    MPI_Isend(&neighbors_to_send[cols], cols, MPI_UNSIGNED_CHAR, dest, 1,
              MPI_COMM_WORLD, &r2);
  } else { // If block only has one row
    // Send the same as before but to next border
    MPI_Isend(neighbors_to_send, cols, MPI_UNSIGNED_CHAR, dest, 1,
              MPI_COMM_WORLD, &r2);
  }

  // Wait sending complete
  //  FIX: stauts is always other
  MPI_Wait(&r1, MPI_STATUS_IGNORE);
  MPI_Wait(&r2, MPI_STATUS_IGNORE);

  free(neighbors_to_send);
}

void count_neighbors_spherical_world_mpi_v2(unsigned char *board, int cols,
                                            int rows, unsigned char *neighbors,
                                            int rank, int size) {
  int i_prev, i_next, j_prev, j_next;
  // Before, we send the borders and prepare to receive them
  unsigned char *upperborder =
      (unsigned char *)malloc(cols * sizeof(unsigned char));
  unsigned char *lowerborder =
      (unsigned char *)malloc(cols * sizeof(unsigned char));
  int upperRank = (rank - 1 < 0) ? size - 1 : rank - 1;
  int lowerRank = (rank + 1 >= size) ? 0 : rank + 1;
  MPI_Request r1, r2;
  MPI_Status s1, s2;

  if (size != 0) {
    printf("[%d] Sending neighbors to %d and %d %d \n", rank, upperRank,
           lowerRank, size);
    send_borders(board, cols, rows, rank, size);
    MPI_Irecv(upperborder, cols, MPI_UNSIGNED_CHAR, upperRank, 1,
              MPI_COMM_WORLD, &r1);
    MPI_Irecv(lowerborder, cols, MPI_UNSIGNED_CHAR, lowerRank, 0,
              MPI_COMM_WORLD, &r2);
  }
  // Convert to 2D array just to be more easy
  unsigned char cell_state[rows][cols];
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      cell_state[i][j] = board[i * cols + j];
    }
  }

  // Clear neighbors
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      neighbors[i * cols + j] = DEAD;
    }
  }

  // Flat only on top bottom
  //  Inner cells
  for (int i = 1; i < rows - 1; i++) {
    for (int j = 0; j < cols; j++) {
      j_prev = (1 < j) ? j - 1 : cols - 1;
      j_next = (j < cols) ? j + 1 : 0;
      if (cell_state[i - 1][j_prev] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i][j_prev] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i + 1][j_prev] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i - 1][j] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i + 1][j] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i - 1][j_next] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i][j_next] == ALIVE) {
        neighbors[i * cols + j]++;
      }
      if (cell_state[i + 1][j_next] == ALIVE) {
        neighbors[i * cols + j]++;
      }
    }
  }

  // top cells
  for (int j = 0; j < cols; j++) {

    j_prev = (1 < j) ? j - 1 : cols - 1;
    j_next = (j < cols) ? j + 1 : 0;
    if (cell_state[0][j_prev] == ALIVE) {
      // 0 * cols + j = j
      neighbors[j]++;
    }
    if (cell_state[1][j_prev] == ALIVE) {
      neighbors[j]++;
    }
    if (cell_state[1][j] == ALIVE) {
      neighbors[j]++;
    }
    if (cell_state[1][j_next] == ALIVE) {
      neighbors[j]++;
    }
    if (cell_state[0][j_next] == ALIVE) {
      neighbors[j]++;
    }
  }

  // bottom cells
  int lastrow = (rows - 1) * cols;
  for (int j = 0; j < cols; j++) {
    j_prev = (1 < j) ? j - 1 : cols - 1;
    j_next = (j < cols) ? j + 1 : 0;
    if (cell_state[rows - 1][j_prev] == ALIVE) {
      // 0 * cols + j = j
      neighbors[lastrow + j]++;
    }
    if (cell_state[rows - 2][j_prev] == ALIVE) {
      neighbors[lastrow + j]++;
    }
    if (cell_state[rows - 2][j] == ALIVE) {
      neighbors[lastrow + j]++;
    }
    if (cell_state[rows - 2][j_next] == ALIVE) {
      neighbors[lastrow + j]++;
    }
    if (cell_state[rows - 1][j_next] == ALIVE) {
      neighbors[lastrow + j]++;
    }
  }

  if (size > 0) {
    // Wait for both recv
    MPI_Wait(&r1, MPI_STATUS_IGNORE);
    MPI_Wait(&r2, MPI_STATUS_IGNORE);

    printf("[%d] Received borders\n", rank);
    // add neighbors to first and last

    for (int j = 0; j < cols; j++) {
      neighbors[0 * cols + j] += upperborder[j];
      neighbors[(rows - 1) * cols + j] += lowerborder[j];
    }
  }
  free(upperborder);
  free(lowerborder);
}

void count_neighbors_spherical_world(
    board_t *board, unsigned char neighbors[D_COL_NUM][D_ROW_NUM]) {
  int i_prev, i_next, j_prev, j_next;

  // Clear neighbors
  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      neighbors[i][j] = DEAD;
    }
  }

  // Inner cells
  for (int i = 0; i < (board->COL_NUM); i++) {
    for (int j = 0; j < (board->ROW_NUM); j++) {
      i_prev = (1 < i) ? i - 1 : board->COL_NUM;
      i_next = (i < board->COL_NUM) ? i + 1 : 0;
      j_prev = (1 < j) ? j - 1 : board->ROW_NUM;
      j_next = (j < board->ROW_NUM) ? j + 1 : 0;
      if (board->cell_state[i_prev][j_prev] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i][j_prev] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i_next][j_prev] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i_prev][j] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i_next][j] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i_prev][j_next] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i][j_next] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i_next][j_next] == ALIVE) {
        neighbors[i][j]++;
      }
    }
  }

  return;

  //-------------------------------------------------------------------------
  // Top cells
  for (int i = 1; i < (board->COL_NUM - 1); i++) {
    if (board->cell_state[i - 1][0] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i - 1][1] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i][1] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i + 1][1] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i + 1][0] == ALIVE) {
      neighbors[i][0]++;
    }
  }

  // Left cells
  for (int j = 1; j < (board->ROW_NUM - 1); j++) {
    if (board->cell_state[0][j - 1] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[1][j - 1] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[1][j] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[1][j + 1] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[0][j + 1] == ALIVE) {
      neighbors[0][j]++;
    }
  }

  // Bottom cells
  for (int i = 1; i < (board->COL_NUM - 1); i++) {
    if (board->cell_state[i - 1][board->ROW_NUM - 1] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i - 1][board->ROW_NUM - 2] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i][board->ROW_NUM - 2] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i + 1][board->ROW_NUM - 2] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i + 1][board->ROW_NUM - 1] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
  }
  // Right cells
  for (int j = 1; j < (board->ROW_NUM - 1); j++) {
    if (board->cell_state[board->COL_NUM - 1][j - 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 2][j - 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 2][j] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 2][j + 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 1][j + 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
  }

  // Top left corner
  if (board->cell_state[1][0] == ALIVE)
    neighbors[0][0]++;
  if (board->cell_state[1][1] == ALIVE)
    neighbors[0][0]++;
  if (board->cell_state[0][1] == ALIVE)
    neighbors[0][0]++;

  // Bottom left corner
  if (board->cell_state[1][board->ROW_NUM - 1] == ALIVE)
    neighbors[0][board->ROW_NUM - 1]++;
  if (board->cell_state[1][board->ROW_NUM - 2] == ALIVE)
    neighbors[0][board->ROW_NUM - 1]++;
  if (board->cell_state[0][board->ROW_NUM - 2] == ALIVE)
    neighbors[0][board->ROW_NUM - 1]++;

  // Bottom right corner
  if (board->cell_state[board->COL_NUM - 2][board->ROW_NUM - 1] == ALIVE)
    neighbors[board->COL_NUM - 1][board->ROW_NUM - 1]++;
  if (board->cell_state[board->COL_NUM - 1][board->ROW_NUM - 2] == ALIVE)
    neighbors[board->COL_NUM - 1][board->ROW_NUM - 1]++;
  if (board->cell_state[board->COL_NUM - 2][board->ROW_NUM - 2] == ALIVE)
    neighbors[board->COL_NUM - 1][board->ROW_NUM - 1]++;

  // Top left corner
  if (board->cell_state[board->COL_NUM - 1][1] == ALIVE)
    neighbors[board->COL_NUM - 1][0]++;
  if (board->cell_state[board->COL_NUM - 2][1] == ALIVE)
    neighbors[board->COL_NUM - 1][0]++;
  if (board->cell_state[board->COL_NUM - 2][0] == ALIVE)
    neighbors[board->COL_NUM - 1][0]++;
}

void count_neighbors_flat_world(board_t *board,
                                unsigned char neighbors[D_COL_NUM][D_ROW_NUM]) {
  // Clear neighbors
  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      neighbors[i][j] = DEAD;
    }
  }

  // Inner cells
  for (int i = 1; i < (board->COL_NUM - 1); i++) {
    for (int j = 1; j < (board->ROW_NUM - 1); j++) {
      if (board->cell_state[i - 1][j - 1] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i][j - 1] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i + 1][j - 1] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i - 1][j] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i + 1][j] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i - 1][j + 1] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i][j + 1] == ALIVE) {
        neighbors[i][j]++;
      }
      if (board->cell_state[i + 1][j + 1] == ALIVE) {
        neighbors[i][j]++;
      }
    }
  }

  // Top cells
  for (int i = 1; i < (board->COL_NUM - 1); i++) {
    if (board->cell_state[i - 1][0] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i - 1][1] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i][1] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i + 1][1] == ALIVE) {
      neighbors[i][0]++;
    }
    if (board->cell_state[i + 1][0] == ALIVE) {
      neighbors[i][0]++;
    }
  }

  // Left cells
  for (int j = 1; j < (board->ROW_NUM - 1); j++) {
    if (board->cell_state[0][j - 1] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[1][j - 1] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[1][j] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[1][j + 1] == ALIVE) {
      neighbors[0][j]++;
    }
    if (board->cell_state[0][j + 1] == ALIVE) {
      neighbors[0][j]++;
    }
  }

  // Bottom cells
  for (int i = 1; i < (board->COL_NUM - 1); i++) {
    if (board->cell_state[i - 1][board->ROW_NUM - 1] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i - 1][board->ROW_NUM - 2] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i][board->ROW_NUM - 2] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i + 1][board->ROW_NUM - 2] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
    if (board->cell_state[i + 1][board->ROW_NUM - 1] == ALIVE) {
      neighbors[i][board->ROW_NUM - 1]++;
    }
  }
  // Right cells
  for (int j = 1; j < (board->ROW_NUM - 1); j++) {
    if (board->cell_state[board->COL_NUM - 1][j - 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 2][j - 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 2][j] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 2][j + 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
    if (board->cell_state[board->COL_NUM - 1][j + 1] == ALIVE) {
      neighbors[board->COL_NUM - 1][j]++;
    }
  }

  // Top left corner
  if (board->cell_state[1][0] == ALIVE)
    neighbors[0][0]++;
  if (board->cell_state[1][1] == ALIVE)
    neighbors[0][0]++;
  if (board->cell_state[0][1] == ALIVE)
    neighbors[0][0]++;

  // Bottom left corner
  if (board->cell_state[1][board->ROW_NUM - 1] == ALIVE)
    neighbors[0][board->ROW_NUM - 1]++;
  if (board->cell_state[1][board->ROW_NUM - 2] == ALIVE)
    neighbors[0][board->ROW_NUM - 1]++;
  if (board->cell_state[0][board->ROW_NUM - 2] == ALIVE)
    neighbors[0][board->ROW_NUM - 1]++;

  // Bottom right corner
  if (board->cell_state[board->COL_NUM - 2][board->ROW_NUM - 1] == ALIVE)
    neighbors[board->COL_NUM - 1][board->ROW_NUM - 1]++;
  if (board->cell_state[board->COL_NUM - 1][board->ROW_NUM - 2] == ALIVE)
    neighbors[board->COL_NUM - 1][board->ROW_NUM - 1]++;
  if (board->cell_state[board->COL_NUM - 2][board->ROW_NUM - 2] == ALIVE)
    neighbors[board->COL_NUM - 1][board->ROW_NUM - 1]++;

  // Top left corner
  if (board->cell_state[board->COL_NUM - 1][1] == ALIVE)
    neighbors[board->COL_NUM - 1][0]++;
  if (board->cell_state[board->COL_NUM - 2][1] == ALIVE)
    neighbors[board->COL_NUM - 1][0]++;
  if (board->cell_state[board->COL_NUM - 2][0] == ALIVE)
    neighbors[board->COL_NUM - 1][0]++;
}

void evolve(board_t *board,
            const unsigned char neighbors[D_COL_NUM][D_ROW_NUM]) {
  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      // underopulation case
      if (neighbors[i][j] < 2)
        board->cell_state[i][j] = DEAD;
      // birth case
      else if (neighbors[i][j] == 3)
        board->cell_state[i][j] = ALIVE;
      // overpopulation case
      else if (neighbors[i][j] > 3)
        board->cell_state[i][j] = DEAD;
      // survival case is implicit, as only cells with 2 or 3
      // neighbors will survive.
    }
  }
}

/******************************************************************************/

void life_read(char *filename, board_t *board)

/******************************************************************************/
/*
  Purpose:
    LIFE_READ reads a file to a grid.

  Parameters:

    Input, char *OUTPUT_FILENAME, the input file name.

*/

{
  FILE *input_unit;
  /*
    input the file.
  */
  input_unit = fopen(filename, "rt");
  if (input_unit == NULL)
    perror("Reading input file:");
  /*
    Read the data.
  */
  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      fscanf(input_unit, "%hhu", &(board->cell_state[i][j]));
    }
  }
  /*
    Close the file.
  */
  fclose(input_unit);

  return;
}

/******************************************************************************/

void life_write(char *output_filename, board_t *board)

/******************************************************************************/
/*
  Purpose:

    LIFE_WRITE writes a boad to a file.

  Parameters:

    Input, char *OUTPUT_FILENAME, the output file name.

*/
{
  FILE *output_unit;
  /*
    Open the file.
  */
  output_unit = fopen(output_filename, "wt");
  /*
    Write the data.
  */
  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      fprintf(output_unit, " %hhu", board->cell_state[i][j]);
    }
    fprintf(output_unit, "\n");
  }
  /*
    Close the file.
  */
  fclose(output_unit);

  return;
}
/******************************************************************************/

double r8_uniform_01(int *seed)

/******************************************************************************/
/*
  Purpose:

    R8_UNIFORM_01 returns a pseudorandom R8 scaled to [0,1].

  Discussion:

    This routine implements the recursion

      seed = 16807 * seed mod ( 2^31 - 1 )
      r8_uniform_01 = seed / ( 2^31 - 1 )

    The integer arithmetic never requires more than 32 bits,
    including a sign bit.

    If the initial seed is 12345, then the first three computations
  are

      Input     Output      R8_UNIFORM_01
      SEED      SEED

         12345   207482415  0.096616
     207482415  1790989824  0.833995
    1790989824  2035175616  0.947702

  Parameters:

    Input/output, int *SEED, the "seed" value.  Normally, this
    value should not be 0.  On output, SEED has been updated.

    Output, double R8_UNIFORM_01, a new pseudorandom variate, strictly
  between 0 and 1.
*/
{
  int i4_huge = 2147483647;
  int k;
  double r;

  k = *seed / 127773;

  *seed = 16807 * (*seed - k * 127773) - k * 2836;

  if (*seed < 0) {
    *seed = *seed + i4_huge;
  }

  r = ((double)(*seed)) * 4.656612875E-10;

  return r;
}

/******************************************************************************/

void life_init(board_t *board, double prob, int *seed)

/******************************************************************************/
/*
  Purpose:

    LIFE_INIT initializes the life grid.

  Parameters:

    Input, double PROB, the probability that a grid cell
    should be alive.

    Input/output, int *SEED, a seed for the random
    number generator.

*/
{
  double r;

  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      board->cell_state[i][j] = 0;
    }
  }

  for (int i = 0; i < board->COL_NUM; i++) {
    for (int j = 0; j < board->ROW_NUM; j++) {
      r = r8_uniform_01(seed);
      if (r <= prob) {
        board->cell_state[i][j] = 1;
      }
    }
  }
}
