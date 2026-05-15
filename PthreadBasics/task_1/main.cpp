#include <pthread.h>

#include <cstdlib>
#include <iostream>

namespace {

constexpr int kThreadCount = 4;

struct ThreadInfo {
    int thread_id;
    int total_threads;
};

pthread_mutex_t g_print_mutex = PTHREAD_MUTEX_INITIALIZER;

void* hello_worker(void* arg) {
    ThreadInfo* info = static_cast<ThreadInfo*>(arg);

    pthread_mutex_lock(&g_print_mutex);
    std::cout << "Hello World from thread " << info->thread_id
              << " of " << info->total_threads << std::endl;
    pthread_mutex_unlock(&g_print_mutex);

    return nullptr;
}

}  // namespace

int main() {
    pthread_t threads[kThreadCount];
    ThreadInfo thread_infos[kThreadCount];

    for (int i = 0; i < kThreadCount; ++i) {
        thread_infos[i] = ThreadInfo{i, kThreadCount};

        const int rc = pthread_create(&threads[i], nullptr, hello_worker, &thread_infos[i]);
        if (rc != 0) {
            std::cerr << "pthread_create failed for thread " << i
                      << ", error code = " << rc << std::endl;
            return EXIT_FAILURE;
        }
    }

    for (int i = 0; i < kThreadCount; ++i) {
        const int rc = pthread_join(threads[i], nullptr);
        if (rc != 0) {
            std::cerr << "pthread_join failed for thread " << i
                      << ", error code = " << rc << std::endl;
            return EXIT_FAILURE;
        }
    }

    pthread_mutex_destroy(&g_print_mutex);

    return EXIT_SUCCESS;
}
