/* Starts itself as two MPI processes under mpiexec, through the C interface
 * the package's `mpi` target supplies: the two reduce a value each
 * contributes, and each checks the result. */
#include <mpi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int process(int argc, char **argv) {
    int size = 0, rank = 0, mine, sum = 0;

    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) return 1;
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    mine = (rank + 1) * 10;
    MPI_Allreduce(&mine, &sum, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    MPI_Finalize();
    if (size != 2 || sum != 30) {
        fprintf(stderr, "FAIL: process %d of %d got the sum %d\n", rank, size, sum);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char command[4096];

    if (argc > 1 && strcmp(argv[1], "--process") == 0) return process(argc, argv);
    snprintf(command, sizeof command, "mpiexec -n 2 \"%s\" --process", argv[0]);
    if (system(command) != 0) {
        fprintf(stderr, "FAIL: `%s` did not succeed\n", command);
        return 1;
    }
    puts("boost-mpi: two processes reduced a value through the C interface");
    return 0;
}
