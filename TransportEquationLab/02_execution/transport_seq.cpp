#include "transport_common.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <vector>

int main(int argc, char** argv) {
    transport::Options options;

    try {
        options = transport::parse_options(argc, argv);
        if (options.help) {
            transport::print_usage(std::cout, argv[0]);
            return 0;
        }
        transport::validate_options(options);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        transport::print_usage(std::cerr, argv[0]);
        return 1;
    }

    const double dt = transport::tau(options);
    const double dx = transport::h(options);
    const double c = transport::courant(options);

    // current[m] хранит слой k, next[m] - слой k+1.
    // Узел m=0 является граничным условием psi(t).
    std::vector<double> current(options.M + 1, 0.0);
    std::vector<double> next(options.M + 1, 0.0);

    for (int m = 0; m <= options.M; ++m) {
        const double x = static_cast<double>(m) * dx;
        current[m] = transport::initial_phi(x);
    }

    const auto time_start = std::chrono::steady_clock::now();

    for (int k = 0; k < options.K; ++k) {
        const double t = static_cast<double>(k) * dt;

        // Левый край задается точно из условия u(t, 0) = psi(t).
        next[0] = transport::boundary_psi(t + dt);

        for (int m = 1; m <= options.M; ++m) {
            const double x = static_cast<double>(m) * dx;

            // Явный левый уголок:
            // u_m^{k+1}=u_m^k-c*(u_m^k-u_{m-1}^k)+tau*f(t_k,x_m).
            next[m] = current[m]
                    - c * (current[m] - current[m - 1])
                    + dt * transport::source_f(t, x, options.a);
        }

        current.swap(next);
    }

    const auto time_finish = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(time_finish - time_start).count();

    double max_error = 0.0;
    for (int m = 0; m <= options.M; ++m) {
        const double x = static_cast<double>(m) * dx;
        max_error = std::max(max_error, std::fabs(current[m] - transport::exact_solution(options.T, x)));
    }

    try {
        if (!options.output.empty()) {
            transport::write_profile_csv(options.output, options, current);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }

    transport::print_summary("sequential", 1, options, seconds, max_error);
    return 0;
}
