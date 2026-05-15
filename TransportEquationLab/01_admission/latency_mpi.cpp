#include <mpi.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int parse_positive_int(const char* value, const std::string& name) {
    std::size_t pos = 0;
    const int parsed = std::stoi(value, &pos);
    if (pos != std::string(value).size() || parsed <= 0) {
        throw std::invalid_argument(name + " must be a positive integer");
    }
    return parsed;
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [iterations] [message_bytes]\n"
              << "Example: mpirun -np 2 " << program << " 100000 1\n";
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int iterations = 100000;
    int message_bytes = 1;

    try {
        if (argc > 3) {
            throw std::invalid_argument("too many arguments");
        }
        if (argc >= 2) {
            iterations = parse_positive_int(argv[1], "iterations");
        }
        if (argc == 3) {
            message_bytes = parse_positive_int(argv[2], "message_bytes");
        }
        if (size != 2) {
            throw std::invalid_argument("latency test requires exactly 2 MPI processes");
        }
    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "error: " << e.what() << '\n';
            print_usage(argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    std::vector<char> buffer(message_bytes, 0);
    const int partner = (rank == 0) ? 1 : 0;
    const int tag = 7;
    const int warmup = 1000;

    // Разогрев убирает из измерений стартовые эффекты MPI-рантайма.
    for (int i = 0; i < warmup; ++i) {
        if (rank == 0) {
            MPI_Send(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD);
            MPI_Recv(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else {
            MPI_Recv(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    const double start = MPI_Wtime();

    // Ping-pong: rank 0 отправляет сообщение, rank 1 сразу возвращает его.
    // Полный круг делится на два, получается оценка задержки одной передачи.
    for (int i = 0; i < iterations; ++i) {
        if (rank == 0) {
            MPI_Send(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD);
            MPI_Recv(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else {
            MPI_Recv(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Send(buffer.data(), message_bytes, MPI_CHAR, partner, tag, MPI_COMM_WORLD);
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    const double finish = MPI_Wtime();

    if (rank == 0) {
        const double round_trip_seconds = (finish - start) / static_cast<double>(iterations);
        const double one_way_latency_seconds = round_trip_seconds / 2.0;

        std::cout << "iterations: " << iterations << '\n';
        std::cout << "message_bytes: " << message_bytes << '\n';
        std::cout << "round_trip_seconds: " << round_trip_seconds << '\n';
        std::cout << "one_way_latency_seconds: " << one_way_latency_seconds << '\n';
        std::cout << "one_way_latency_microseconds: " << one_way_latency_seconds * 1e6 << '\n';
    }

    MPI_Finalize();
    return 0;
}
