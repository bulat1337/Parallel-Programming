#include <mpi.h>
#include <iostream>

int main(int argc, char** argv) {
    // Start the MPI runtime on all launched processes.
    MPI_Init(&argc, &argv);

    // rank: id of this process, size: total process count.
    int rank = -1;
    int size = 0;

    // Query this process id in MPI_COMM_WORLD.
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Query total number of processes in MPI_COMM_WORLD.
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Every process prints its own "Hello World" line.
    std::cout << "Hello World from rank " << rank
              << " of " << size << std::endl;

    // Cleanly shut down the MPI runtime.
    MPI_Finalize();

    return 0;
}
