#include <mpi.h>

#include <iostream>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 0;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const int tag = 0;
    int value = 0;

    // corner case
    if (size == 1) {
        ++value;
        std::cout << "Rank " << rank << " received value = " << value << std::endl;
        MPI_Finalize();

        return 0;
    }

    if (rank == 0) {
        MPI_Send(&value, 1, MPI_INT, 1, tag, MPI_COMM_WORLD);

        MPI_Recv(&value, 1, MPI_INT, size - 1, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        ++value;

        std::cout << "Rank " << rank << " received value = " << value << std::endl;
    } else {
        const int prev = rank - 1;
        const int next = (rank + 1) % size;

        MPI_Recv(&value, 1, MPI_INT, prev, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        ++value;
        std::cout << "Rank " << rank << " received value = " << value << std::endl;

        MPI_Send(&value, 1, MPI_INT, next, tag, MPI_COMM_WORLD);
    }

    MPI_Finalize();
    
    return 0;
}
