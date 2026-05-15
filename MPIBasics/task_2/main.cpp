#include <mpi.h>

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 0;

    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 2) {
        if (rank == 0) {
            std::cerr << "Usage: " << argv[0] << " N" << std::endl;
        }

        MPI_Finalize();
        return 1;
    }

    long long n = 0;
    try {
        std::size_t pos = 0;
        std::string arg = argv[1];
        n = std::stoll(arg, &pos);

        if (pos != arg.size()) {
            throw std::invalid_argument("Invalid integer");
        }
    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "error: " << e.what() << std::endl;
        }

        MPI_Finalize();
        return 1;
    }

    if (n <= 0) {
        if (rank == 0) {
            std::cerr << "N must be a positive integer" << std::endl;
        }

        MPI_Finalize();
        return 1;
    }

    const long long start = (rank * n) / size + 1;
    const long long end = ((rank + 1) * n) / size;

    long double local_sum = 0.0;
    for (long long i = start; i <= end; ++i) {
        local_sum += 1.0 / static_cast<long double>(i);
    }

    long double global_sum = 0.0L;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_LONG_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << std::setprecision(20);
        std::cout << "N = " << n << '\n';
        std::cout << "Sum = " << global_sum << std::endl;
    }

    MPI_Finalize();

    return 0;
}
