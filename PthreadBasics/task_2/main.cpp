#include <pthread.h>

#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr int kThreadCount = 4;

struct ThreadData {
    int thread_id;
    long long start;
    long long end;
    long double partial_sum;
};

void* harmonic_worker(void* arg) {
    ThreadData* data = static_cast<ThreadData*>(arg);
    data->partial_sum = 0.0L;

    for (long long i = data->start; i <= data->end; ++i) {
        data->partial_sum += 1.0L / static_cast<long double>(i);
    }

    return nullptr;
}

bool parse_positive_long_long(const char* text, long long& value) {
    try {
        std::size_t pos = 0;
        std::string arg = text;
        value = std::stoll(arg, &pos);

        if (pos != arg.size()) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }

    return value > 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " N" << std::endl;
        return EXIT_FAILURE;
    }

    long long n = 0;
    if (!parse_positive_long_long(argv[1], n)) {
        std::cerr << "N must be a positive integer" << std::endl;
        return EXIT_FAILURE;
    }

    pthread_t threads[kThreadCount];
    ThreadData thread_data[kThreadCount];

    for (int i = 0; i < kThreadCount; ++i) {
        const long long start = (static_cast<long long>(i) * n) / kThreadCount + 1;
        const long long end = (static_cast<long long>(i + 1) * n) / kThreadCount;

        thread_data[i] = ThreadData{i, start, end, 0.0L};

        const int rc = pthread_create(&threads[i], nullptr, harmonic_worker, &thread_data[i]);
        if (rc != 0) {
            std::cerr << "pthread_create failed for thread " << i
                      << ", error code = " << rc << std::endl;
            return EXIT_FAILURE;
        }
    }

    long double total_sum = 0.0L;
    for (int i = 0; i < kThreadCount; ++i) {
        const int rc = pthread_join(threads[i], nullptr);
        if (rc != 0) {
            std::cerr << "pthread_join failed for thread " << i
                      << ", error code = " << rc << std::endl;
            return EXIT_FAILURE;
        }

        total_sum += thread_data[i].partial_sum;
    }

    std::cout << "Using " << kThreadCount << " threads" << '\n';
    std::cout << "N = " << n << '\n';
    std::cout << std::setprecision(20);
    std::cout << "Sum = " << total_sum << std::endl;

    return EXIT_SUCCESS;
}
