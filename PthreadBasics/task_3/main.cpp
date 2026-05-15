#include <pthread.h>

#include <cstdlib>
#include <iostream>

namespace {

constexpr int kThreadCount = 5;

int g_shared_value = 0;
int g_next_thread_id = 0;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_turn_changed = PTHREAD_COND_INITIALIZER;

struct ThreadInfo {
    int thread_id;
};

void* ordered_worker(void* arg) {
    ThreadInfo* info = static_cast<ThreadInfo*>(arg);

    pthread_mutex_lock(&g_mutex);

    while (info->thread_id != g_next_thread_id) {
        pthread_cond_wait(&g_turn_changed, &g_mutex);
    }

    ++g_shared_value;

    std::cout << "Thread " << info->thread_id
              << " updated shared_value = " << g_shared_value << std::endl;

    ++g_next_thread_id;

    pthread_cond_broadcast(&g_turn_changed);

    pthread_mutex_unlock(&g_mutex);

    return nullptr;
}

}  // namespace

int main() {
    pthread_t threads[kThreadCount];
    ThreadInfo thread_infos[kThreadCount];

    for (int i = 0; i < kThreadCount; ++i) {
        thread_infos[i] = ThreadInfo{i};

        const int rc = pthread_create(&threads[i], nullptr, ordered_worker, &thread_infos[i]);
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

    std::cout << "Final shared_value = " << g_shared_value << std::endl;

    pthread_cond_destroy(&g_turn_changed);
    pthread_mutex_destroy(&g_mutex);

    return EXIT_SUCCESS;
}
