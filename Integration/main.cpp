#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <vector>

namespace {

constexpr std::size_t kInitialStackCapacity = 1024;

constexpr unsigned kMaxRecursionDepth = 40;

constexpr double kIntervalStart = 0.0;
constexpr double kIntervalEnd = 4.999;

struct Task {
    double a = 0.0;
    double b = 0.0;
    double fa = 0.0;
    double fm = 0.0;
    double fb = 0.0;
    double simpson = 0.0;
    double tolerance = 0.0;
    unsigned depth = 0;
};

struct RunSummary {
    double result = 0.0;
    double elapsed_seconds = 0.0;
    unsigned long processed_tasks = 0;
    unsigned long accepted_intervals = 0;
    unsigned long splits = 0;
    unsigned long function_evals = 0;
    unsigned long depth_limit_hits = 0;
    double min_step = DBL_MAX;
    double max_step = 0.0;
};

struct ThreadSummary {
    std::size_t thread_index = 0;
    double partial_sum = 0.0;
    unsigned long processed_tasks = 0;
    unsigned long accepted_intervals = 0;
    unsigned long splits = 0;
    unsigned long function_evals = 0;
    unsigned long depth_limit_hits = 0;
    double min_step = DBL_MAX;
    double max_step = 0.0;
};

struct TaskStack {
    Task* items = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
};

struct Scheduler {
    TaskStack stack;
    pthread_mutex_t mutex;
    pthread_cond_t has_work;
    std::size_t active_workers = 0;
    bool done = false;
    bool error = false;
};

struct WorkerContext {
    Scheduler* scheduler = nullptr;
    ThreadSummary* summary = nullptr;
};

using Clock = std::chrono::steady_clock;

double now_seconds() {
    const auto now = Clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

double midpoint(double left, double right) {
    return left + (right - left) * 0.5;
}

double simpson_estimate(double left,
                        double right,
                        double f_left,
                        double f_mid,
                        double f_right) {
    return (right - left) * (f_left + 4.0 * f_mid + f_right) / 6.0;
}

double integrand(double x) {
    return std::sin(1.0 / ((x - 5.0) * (x - 5.0)));
}

void init_run_summary(RunSummary& summary) {
    summary = RunSummary{};
}

void init_thread_summary(ThreadSummary& summary, std::size_t thread_index) {
    summary = ThreadSummary{};
    summary.thread_index = thread_index;
}

void update_step_stats(double step, double& min_step, double& max_step) {
    if (step < min_step) {
        min_step = step;
    }
    if (step > max_step) {
        max_step = step;
    }
}

void merge_thread_summary(RunSummary& total, const ThreadSummary& thread) {
    total.result += thread.partial_sum;
    total.processed_tasks += thread.processed_tasks;
    total.accepted_intervals += thread.accepted_intervals;
    total.splits += thread.splits;
    total.function_evals += thread.function_evals;
    total.depth_limit_hits += thread.depth_limit_hits;

    if (thread.accepted_intervals > 0) {
        if (thread.min_step < total.min_step) {
            total.min_step = thread.min_step;
        }
        if (thread.max_step > total.max_step) {
            total.max_step = thread.max_step;
        }
    }
}

bool task_stack_init(TaskStack& stack, std::size_t initial_capacity) {
    stack.items = static_cast<Task*>(std::malloc(initial_capacity * sizeof(Task)));
    if (stack.items == nullptr) {
        return false;
    }

    stack.size = 0;
    stack.capacity = initial_capacity;
    return true;
}

void task_stack_destroy(TaskStack& stack) {
    std::free(stack.items);
    stack.items = nullptr;
    stack.size = 0;
    stack.capacity = 0;
}

bool task_stack_push(TaskStack& stack, const Task& task) {
    if (stack.size == stack.capacity) {
        const std::size_t new_capacity = stack.capacity * 2;
        Task* new_items = static_cast<Task*>(
            std::realloc(stack.items, new_capacity * sizeof(Task)));
        if (new_items == nullptr) {
            return false;
        }

        stack.items = new_items;
        stack.capacity = new_capacity;
    }

    stack.items[stack.size++] = task;
    return true;
}

Task task_stack_pop(TaskStack& stack) {
    return stack.items[--stack.size];
}

bool scheduler_init(Scheduler& scheduler) {
    if (!task_stack_init(scheduler.stack, kInitialStackCapacity)) {
        return false;
    }

    if (pthread_mutex_init(&scheduler.mutex, nullptr) != 0) {
        task_stack_destroy(scheduler.stack);
        return false;
    }

    if (pthread_cond_init(&scheduler.has_work, nullptr) != 0) {
        pthread_mutex_destroy(&scheduler.mutex);
        task_stack_destroy(scheduler.stack);
        return false;
    }

    scheduler.active_workers = 0;
    scheduler.done = false;
    scheduler.error = false;
    
    return true;
}

void scheduler_destroy(Scheduler& scheduler) {
    pthread_cond_destroy(&scheduler.has_work);
    pthread_mutex_destroy(&scheduler.mutex);
    task_stack_destroy(scheduler.stack);
}

void scheduler_finish_task_locked(Scheduler& scheduler) {
    if (scheduler.active_workers > 0) {
        --scheduler.active_workers;
    }

    if (scheduler.error) {
        scheduler.done = true;
    } else if (scheduler.stack.size == 0 && scheduler.active_workers == 0) {
        scheduler.done = true;
    }

    pthread_cond_broadcast(&scheduler.has_work);
}

Task make_root_task(double tolerance, unsigned long& function_evals) {
    Task root;
    const double middle = midpoint(kIntervalStart, kIntervalEnd);

    root.a = kIntervalStart;
    root.b = kIntervalEnd;
    root.fa = integrand(root.a);
    root.fm = integrand(middle);
    root.fb = integrand(root.b);
    root.simpson = simpson_estimate(root.a, root.b, root.fa, root.fm, root.fb);
    root.tolerance = tolerance;
    root.depth = 0;

    function_evals += 3;

    return root;
}

double integrate_sequential_task(const Task& task, RunSummary& summary) {
    const double middle = midpoint(task.a, task.b);
    const double left_middle = midpoint(task.a, middle);
    const double right_middle = midpoint(middle, task.b);

    const double f_left_middle = integrand(left_middle);
    const double f_right_middle = integrand(right_middle);

    const double left_estimate =
        simpson_estimate(task.a, middle, task.fa, f_left_middle, task.fm);
    const double right_estimate =
        simpson_estimate(middle, task.b, task.fm, f_right_middle, task.fb);

    const double refined = left_estimate + right_estimate;
    const double delta = refined - task.simpson;
    const double step = task.b - task.a;
    const bool reached_depth_limit = task.depth >= kMaxRecursionDepth;

    ++summary.processed_tasks;
    summary.function_evals += 2;

    if (reached_depth_limit || std::fabs(delta) <= 15.0 * task.tolerance) {
        ++summary.accepted_intervals;

        if (reached_depth_limit && std::fabs(delta) > 15.0 * task.tolerance) {
            ++summary.depth_limit_hits;
        }

        update_step_stats(step, summary.min_step, summary.max_step);

        return refined + delta / 15.0;
    }

    ++summary.splits;

    const Task left_task{
        task.a,
        middle,
        task.fa,
        f_left_middle,
        task.fm,
        left_estimate,
        task.tolerance * 0.5,
        task.depth + 1
    };
    const Task right_task{
        middle,
        task.b,
        task.fm,
        f_right_middle,
        task.fb,
        right_estimate,
        task.tolerance * 0.5,
        task.depth + 1
    };

    return integrate_sequential_task(left_task, summary)
           + integrate_sequential_task(right_task, summary);
}

bool run_sequential(double tolerance, RunSummary& summary) {
    init_run_summary(summary);
    const double start_time = now_seconds();
    const Task root = make_root_task(tolerance, summary.function_evals);
    summary.result = integrate_sequential_task(root, summary);
    summary.elapsed_seconds = now_seconds() - start_time;
    return true;
}

void* worker_main(void* arg) {
    auto* context = static_cast<WorkerContext*>(arg);
    Scheduler* scheduler = context->scheduler;
    ThreadSummary* summary = context->summary;
    std::vector<Task> local_stack;
    local_stack.reserve(256);

    for (;;) {
        if (local_stack.empty()) {
            pthread_mutex_lock(&scheduler->mutex);

            while (scheduler->stack.size == 0 && !scheduler->done && !scheduler->error) {
                if (scheduler->active_workers == 0) {
                    scheduler->done = true;
                    pthread_cond_broadcast(&scheduler->has_work);
                    break;
                }
                pthread_cond_wait(&scheduler->has_work, &scheduler->mutex);
            }

            if (scheduler->done || scheduler->error) {
                pthread_mutex_unlock(&scheduler->mutex);
                break;
            }

            local_stack.push_back(task_stack_pop(scheduler->stack));
            ++scheduler->active_workers;
            pthread_mutex_unlock(&scheduler->mutex);
        }

        while (!local_stack.empty()) {
            const Task task = local_stack.back();
            local_stack.pop_back();

            ++summary->processed_tasks;

            const double middle = midpoint(task.a, task.b);
            const double left_middle = midpoint(task.a, middle);
            const double right_middle = midpoint(middle, task.b);
            const double f_left_middle = integrand(left_middle);
            const double f_right_middle = integrand(right_middle);
            const double left_estimate =
                simpson_estimate(task.a, middle, task.fa, f_left_middle, task.fm);
            const double right_estimate =
                simpson_estimate(middle, task.b, task.fm, f_right_middle, task.fb);

            const double refined = left_estimate + right_estimate;
            const double delta = refined - task.simpson;
            const double step = task.b - task.a;
            const bool reached_depth_limit = task.depth >= kMaxRecursionDepth;

            summary->function_evals += 2;

            if (reached_depth_limit || std::fabs(delta) <= 15.0 * task.tolerance) {
                ++summary->accepted_intervals;
                if (reached_depth_limit && std::fabs(delta) > 15.0 * task.tolerance) {
                    ++summary->depth_limit_hits;
                }

                update_step_stats(step, summary->min_step, summary->max_step);
                summary->partial_sum += refined + delta / 15.0;
                continue;
            }

            ++summary->splits;

            const Task left_task{
                task.a,
                middle,
                task.fa,
                f_left_middle,
                task.fm,
                left_estimate,
                task.tolerance * 0.5,
                task.depth + 1
            };
            const Task right_task{
                middle,
                task.b,
                task.fm,
                f_right_middle,
                task.fb,
                right_estimate,
                task.tolerance * 0.5,
                task.depth + 1
            };

            local_stack.push_back(left_task);

            bool push_failed = false;
            pthread_mutex_lock(&scheduler->mutex);
            if (!task_stack_push(scheduler->stack, right_task)) {
                scheduler->error = true;
                scheduler->done = true;
                push_failed = true;
                pthread_cond_broadcast(&scheduler->has_work);
            } else {
                pthread_cond_signal(&scheduler->has_work);
            }
            pthread_mutex_unlock(&scheduler->mutex);

            if (push_failed) {
                break;
            }
        }

        pthread_mutex_lock(&scheduler->mutex);
        scheduler_finish_task_locked(*scheduler);
        const bool stop = scheduler->done || scheduler->error;
        pthread_mutex_unlock(&scheduler->mutex);

        if (stop) {
            break;
        }
    }

    return nullptr;
}

bool run_parallel(std::size_t thread_count,
                  double tolerance,
                  RunSummary& summary,
                  std::vector<ThreadSummary>& thread_summaries) {
    init_run_summary(summary);

    Scheduler scheduler{};
    if (!scheduler_init(scheduler)) {
        std::cerr << "Failed to initialize scheduler.\n";
        return false;
    }

    std::vector<pthread_t> threads(thread_count);
    thread_summaries.assign(thread_count, ThreadSummary{});
    std::vector<WorkerContext> contexts(thread_count);
    std::size_t created_threads = 0;
    bool ok = false;

    const Task root = make_root_task(tolerance, summary.function_evals);
    if (!task_stack_push(scheduler.stack, root)) {
        std::cerr << "Failed to push the root task.\n";
        scheduler_destroy(scheduler);
        return false;
    }

    const double start_time = now_seconds();

    for (std::size_t i = 0; i < thread_count; ++i) {
        init_thread_summary(thread_summaries[i], i);
        contexts[i].scheduler = &scheduler;
        contexts[i].summary = &thread_summaries[i];

        if (pthread_create(&threads[i], nullptr, worker_main, &contexts[i]) != 0) {
            std::cerr << "pthread_create failed for worker " << i << ".\n";
            scheduler.error = true;
            scheduler.done = true;
            pthread_cond_broadcast(&scheduler.has_work);
            break;
        }

        ++created_threads;
    }

    for (std::size_t i = 0; i < created_threads; ++i) {
        pthread_join(threads[i], nullptr);
    }

    summary.elapsed_seconds = now_seconds() - start_time;

    if (!scheduler.error) {
        for (const ThreadSummary& thread : thread_summaries) {
            merge_thread_summary(summary, thread);
        }
        ok = true;
    } else {
        std::cerr << "Parallel integration failed because of scheduler error.\n";
    }

    scheduler_destroy(scheduler);
    return ok;
}

bool parse_positive_size(const char* text, std::size_t& value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || parsed <= 0) {
        return false;
    }

    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parse_positive_double(const char* text, double& value) {
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text, &end);

    if (errno != 0 || end == text || *end != '\0'
        || !std::isfinite(parsed) || parsed <= 0.0) {
        return false;
    }

    value = parsed;
    return true;
}

void print_usage(const char* program_name) {
    std::cerr << "Usage: " << program_name << " <thread_count> <tolerance>\n"
              << "Example: " << program_name << " 4 1e-8\n";
}

void print_run_summary(const char* label, const RunSummary& summary) {
    const double average_step = summary.accepted_intervals == 0
        ? 0.0
        : (kIntervalEnd - kIntervalStart)
              / static_cast<double>(summary.accepted_intervals);
    const double min_step = summary.accepted_intervals == 0 ? 0.0 : summary.min_step;

    std::cout << label << ":\n";
    std::cout << "  integral               = " << std::fixed << std::setprecision(12)
              << summary.result << '\n';
    std::cout << "  elapsed                = " << std::fixed << std::setprecision(6)
              << summary.elapsed_seconds << " sec\n";
    std::cout << "  processed tasks        = " << summary.processed_tasks << '\n';
    std::cout << "  accepted intervals     = " << summary.accepted_intervals << '\n';
    std::cout << "  performed splits       = " << summary.splits << '\n';
    std::cout << "  function evaluations   = " << summary.function_evals << '\n';
    std::cout << "  min adaptive step      = " << std::scientific << std::setprecision(12)
              << min_step << '\n';
    std::cout << "  max adaptive step      = " << summary.max_step << '\n';
    std::cout << "  average step           = " << average_step << '\n';

    if (summary.depth_limit_hits > 0) {
        std::cout << "  warnings               = depth limit reached "
                  << summary.depth_limit_hits << " time(s)\n";
    }
}

void print_thread_balance(const std::vector<ThreadSummary>& threads) {
    if (threads.empty()) {
        return;
    }

    unsigned long min_tasks = threads.front().processed_tasks;
    unsigned long max_tasks = threads.front().processed_tasks;
    unsigned long total_tasks = 0;

    std::cout << "\nThread balance:\n";
    std::cout << "  id | tasks | accepted | partial sum\n";

    for (const ThreadSummary& thread : threads) {
        const unsigned long tasks = thread.processed_tasks;

        if (tasks < min_tasks) {
            min_tasks = tasks;
        }
        if (tasks > max_tasks) {
            max_tasks = tasks;
        }

        total_tasks += tasks;

        std::cout << "  " << std::setw(2) << thread.thread_index
                  << " | " << std::setw(5) << thread.processed_tasks
                  << " | " << std::setw(8) << thread.accepted_intervals
                  << " | " << std::fixed << std::setprecision(12)
                  << thread.partial_sum << '\n';
    }

    const double average_tasks =
        static_cast<double>(total_tasks) / static_cast<double>(threads.size());
    const double imbalance =
        average_tasks == 0.0 ? 0.0 : static_cast<double>(max_tasks) / average_tasks;

    std::cout << "  avg tasks/thread       = " << std::fixed << std::setprecision(2)
              << average_tasks << '\n';
    std::cout << "  min tasks/thread       = " << min_tasks << '\n';
    std::cout << "  max tasks/thread       = " << max_tasks << '\n';
    std::cout << "  imbalance coefficient  = " << std::fixed << std::setprecision(4)
              << imbalance << '\n';
}

}

int main(int argc, char** argv) {
    std::size_t thread_count = 0;
    double tolerance = 0.0;
    RunSummary sequential_summary;
    RunSummary parallel_summary;
    std::vector<ThreadSummary> thread_summaries;

    if (argc != 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!parse_positive_size(argv[1], thread_count)
        || !parse_positive_double(argv[2], tolerance)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!run_sequential(tolerance, sequential_summary)) {
        return EXIT_FAILURE;
    }

    if (!run_parallel(thread_count, tolerance, parallel_summary, thread_summaries)) {
        return EXIT_FAILURE;
    }

    const double speedup =
        sequential_summary.elapsed_seconds / parallel_summary.elapsed_seconds;
    const double efficiency = speedup / static_cast<double>(thread_count);
    const double abs_difference =
        std::fabs(sequential_summary.result - parallel_summary.result);

    std::cout << "Adaptive integration by Simpson's rule\n";
    std::cout << "Function interval        = [" << std::fixed << std::setprecision(3)
              << kIntervalStart << ", " << kIntervalEnd << "]\n";
    std::cout << "Requested tolerance      = " << std::scientific
              << std::setprecision(12) << tolerance << '\n';
    std::cout << "Threads                  = " << std::defaultfloat
              << thread_count << "\n\n";

    print_run_summary("Sequential run", sequential_summary);
    std::cout << '\n';
    print_run_summary("Parallel run", parallel_summary);

    std::cout << "\nQuality and scalability:\n";
    std::cout << "  |I_seq - I_par|        = " << std::scientific
              << std::setprecision(12) << abs_difference << '\n';
    std::cout << "  speedup                = " << std::fixed << std::setprecision(6)
              << speedup << '\n';
    std::cout << "  efficiency             = " << efficiency << '\n';

    print_thread_balance(thread_summaries);

    return EXIT_SUCCESS;
}
