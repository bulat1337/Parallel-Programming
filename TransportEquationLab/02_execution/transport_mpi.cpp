#include "transport_common.hpp"

#include <mpi.h>

#include <algorithm>
#include <exception>
#include <iostream>
#include <vector>

namespace {

int first_global_node(int rank, int size, int M) {
    // В параллельной программе распределяются внутренние узлы 1..M.
    return 1 + (rank * M) / size;
}

int last_global_node(int rank, int size, int M) {
    return ((rank + 1) * M) / size;
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    transport::Options options;

    try {
        options = transport::parse_options(argc, argv);
        if (options.help) {
            if (rank == 0) {
                transport::print_usage(std::cout, argv[0]);
            }
            MPI_Finalize();
            return 0;
        }
        transport::validate_options(options);

        if (size > options.M) {
            throw std::invalid_argument("number of MPI processes must be <= M");
        }
    } catch (const std::exception& e) {
        if (rank == 0) {
            std::cerr << "error: " << e.what() << '\n';
            transport::print_usage(std::cerr, argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    const double dt = transport::tau(options);
    const double dx = transport::h(options);
    const double c = transport::courant(options);

    const int local_first = first_global_node(rank, size, options.M);
    const int local_last = last_global_node(rank, size, options.M);
    const int local_count = local_last - local_first + 1;

    // Каждый процесс хранит только свой непрерывный участок узлов x.
    std::vector<double> current(local_count, 0.0);
    std::vector<double> next(local_count, 0.0);

    for (int i = 0; i < local_count; ++i) {
        const int m = local_first + i;
        const double x = static_cast<double>(m) * dx;
        current[i] = transport::initial_phi(x);
    }

    const int left_rank = (rank == 0) ? MPI_PROC_NULL : rank - 1;
    const int right_rank = (rank == size - 1) ? MPI_PROC_NULL : rank + 1;

    MPI_Barrier(MPI_COMM_WORLD);
    const double time_start = MPI_Wtime();

    for (int k = 0; k < options.K; ++k) {
        const double t = static_cast<double>(k) * dt;
        const double send_right = current.back();
        double recv_left = 0.0;

        // Для первого локального узла нужен u_{m-1}^k.
        // Сосед слева присылает свое крайнее правое значение прошлого слоя.
        MPI_Sendrecv(
            &send_right,
            1,
            MPI_DOUBLE,
            right_rank,
            0,
            &recv_left,
            1,
            MPI_DOUBLE,
            left_rank,
            0,
            MPI_COMM_WORLD,
            MPI_STATUS_IGNORE
        );

        for (int i = 0; i < local_count; ++i) {
            const int m = local_first + i;
            const double x = static_cast<double>(m) * dx;
            const double left_value = (i == 0)
                                        ? (rank == 0 ? transport::boundary_psi(t) : recv_left)
                                        : current[i - 1];

            // Та же формула левого уголка, что и в последовательной программе.
            next[i] = current[i]
                    - c * (current[i] - left_value)
                    + dt * transport::source_f(t, x, options.a);
        }

        current.swap(next);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    const double local_seconds = MPI_Wtime() - time_start;

    double seconds = 0.0;
    MPI_Reduce(&local_seconds, &seconds, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    double local_max_error = 0.0;
    for (int i = 0; i < local_count; ++i) {
        const int m = local_first + i;
        const double x = static_cast<double>(m) * dx;
        local_max_error = std::max(
            local_max_error,
            std::fabs(current[i] - transport::exact_solution(options.T, x))
        );
    }

    double max_error = 0.0;
    MPI_Reduce(&local_max_error, &max_error, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    std::vector<int> counts(size, 0);
    std::vector<int> displacements(size, 0);
    for (int r = 0; r < size; ++r) {
        const int first = first_global_node(r, size, options.M);
        const int last = last_global_node(r, size, options.M);
        counts[r] = last - first + 1;
        displacements[r] = first - 1;
    }

    std::vector<double> gathered_unknowns;
    if (rank == 0) {
        gathered_unknowns.resize(options.M);
    }

    MPI_Gatherv(
        current.data(),
        local_count,
        MPI_DOUBLE,
        rank == 0 ? gathered_unknowns.data() : nullptr,
        counts.data(),
        displacements.data(),
        MPI_DOUBLE,
        0,
        MPI_COMM_WORLD
    );

    if (rank == 0) {
        try {
            if (!options.output.empty()) {
                std::vector<double> full_layer(options.M + 1, 0.0);
                full_layer[0] = transport::boundary_psi(options.T);
                for (int m = 1; m <= options.M; ++m) {
                    full_layer[m] = gathered_unknowns[m - 1];
                }
                transport::write_profile_csv(options.output, options, full_layer);
            }
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << '\n';
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        transport::print_summary("parallel", size, options, seconds, max_error);
    }

    MPI_Finalize();
    return 0;
}
