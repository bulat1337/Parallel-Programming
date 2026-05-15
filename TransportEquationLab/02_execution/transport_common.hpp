#ifndef TRANSPORT_COMMON_HPP
#define TRANSPORT_COMMON_HPP

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace transport {

struct Options {
    int M = 200;          // Число шагов по координате x, узлы имеют номера 0..M.
    int K = 400;          // Число шагов по времени t, слои имеют номера 0..K.
    double a = 1.0;       // Скорость переноса. Для левого уголка используем a > 0.
    double T = 1.0;       // Правая граница временного интервала.
    double X = 1.0;       // Правая граница пространственного интервала.
    bool csv = false;     // Короткий вывод одной CSV-строкой для скриптов.
    bool help = false;    // Печать справки без запуска расчета.
    std::string output;   // CSV-файл с итоговым профилем u(T, x).
};

inline int parse_positive_int(const std::string& value, const std::string& name) {
    std::size_t pos = 0;
    const long long parsed = std::stoll(value, &pos);
    if (pos != value.size() || parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        throw std::invalid_argument(name + " must be a positive integer");
    }
    return static_cast<int>(parsed);
}

inline double parse_positive_double(const std::string& value, const std::string& name) {
    std::size_t pos = 0;
    const double parsed = std::stod(value, &pos);
    if (pos != value.size() || parsed <= 0.0 || !std::isfinite(parsed)) {
        throw std::invalid_argument(name + " must be a positive finite number");
    }
    return parsed;
}

inline std::string require_value(int& i, int argc, char** argv, const std::string& name) {
    if (i + 1 >= argc) {
        throw std::invalid_argument("missing value after " + name);
    }
    ++i;
    return argv[i];
}

inline Options parse_options(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--M") {
            options.M = parse_positive_int(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--K") {
            options.K = parse_positive_int(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--a") {
            options.a = parse_positive_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--T") {
            options.T = parse_positive_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--X") {
            options.X = parse_positive_double(require_value(i, argc, argv, arg), arg);
        } else if (arg == "--output") {
            options.output = require_value(i, argc, argv, arg);
        } else if (arg == "--csv") {
            options.csv = true;
        } else {
            throw std::invalid_argument("unknown argument: " + arg);
        }
    }

    return options;
}

inline double tau(const Options& options) {
    return options.T / static_cast<double>(options.K);
}

inline double h(const Options& options) {
    return options.X / static_cast<double>(options.M);
}

inline double courant(const Options& options) {
    return options.a * tau(options) / h(options);
}

inline void validate_options(const Options& options) {
    if (options.a <= 0.0) {
        throw std::invalid_argument("left-corner scheme in this program requires a > 0");
    }

    const double c = courant(options);
    if (c > 1.0 + 1e-12) {
        std::ostringstream message;
        message << "explicit left-corner scheme is stable for c = a*tau/h <= 1, got c = "
                << c;
        throw std::invalid_argument(message.str());
    }
}

inline double exact_solution(double t, double x) {
    // Тестовая точная функция. По ней строятся начальное, граничное условия и f(t, x).
    return std::exp(-t) * (1.0 + std::sin(x));
}

inline double initial_phi(double x) {
    // u(0, x) = phi(x).
    return exact_solution(0.0, x);
}

inline double boundary_psi(double t) {
    // u(t, 0) = psi(t). Совместимость выполнена: phi(0) = psi(0) = 1.
    return exact_solution(t, 0.0);
}

inline double source_f(double t, double x, double a) {
    // f(t, x) = u_t + a*u_x для выбранной точной функции.
    const double e = std::exp(-t);
    return e * (a * std::cos(x) - 1.0 - std::sin(x));
}

inline void print_usage(std::ostream& out, const std::string& program_name) {
    out << "Usage: " << program_name << " [options]\n"
        << "Options:\n"
        << "  --M N           number of spatial steps, default 200\n"
        << "  --K N           number of time steps, default 400\n"
        << "  --a VALUE       transfer speed, default 1.0\n"
        << "  --T VALUE       final time, default 1.0\n"
        << "  --X VALUE       right x boundary, default 1.0\n"
        << "  --output FILE   write final layer u(T,x) to CSV\n"
        << "  --csv           print one machine-readable CSV row\n"
        << "  --help          show this message\n";
}

inline void print_summary(
    const std::string& mode,
    int processes,
    const Options& options,
    double seconds,
    double max_error
) {
    const double dt = tau(options);
    const double dx = h(options);
    const double c = courant(options);

    if (options.csv) {
        std::cout << mode << ','
                  << processes << ','
                  << options.M << ','
                  << options.K << ','
                  << std::setprecision(17) << options.a << ','
                  << options.T << ','
                  << options.X << ','
                  << dt << ','
                  << dx << ','
                  << c << ','
                  << seconds << ','
                  << max_error << '\n';
        return;
    }

    std::cout << std::setprecision(10);
    std::cout << "mode: " << mode << '\n';
    std::cout << "processes: " << processes << '\n';
    std::cout << "M: " << options.M << '\n';
    std::cout << "K: " << options.K << '\n';
    std::cout << "a: " << options.a << '\n';
    std::cout << "T: " << options.T << '\n';
    std::cout << "X: " << options.X << '\n';
    std::cout << "tau: " << dt << '\n';
    std::cout << "h: " << dx << '\n';
    std::cout << "c = a*tau/h: " << c << '\n';
    std::cout << "time_seconds: " << seconds << '\n';
    std::cout << "max_error: " << max_error << '\n';
}

inline void write_profile_csv(const std::string& path, const Options& options, const std::vector<double>& values) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("cannot open output file: " + path);
    }

    out << "m,x,numeric,exact,error\n";
    out << std::setprecision(17);

    for (int m = 0; m <= options.M; ++m) {
        const double x = static_cast<double>(m) * h(options);
        const double exact = exact_solution(options.T, x);
        out << m << ','
            << x << ','
            << values[m] << ','
            << exact << ','
            << std::fabs(values[m] - exact) << '\n';
    }
}

}  // namespace transport

#endif  // TRANSPORT_COMMON_HPP
