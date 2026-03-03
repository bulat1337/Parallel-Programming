#include <mpi.h>      // Библиотека MPI: инициализация, rank/size, завершение.
#include <iostream>   // std::cout для вывода в консоль.

int main(int argc, char** argv) {
    // 1) Запускаем MPI-среду. До MPI_Init нельзя вызывать MPI-функции.
    MPI_Init(&argc, &argv);

    int rank = -1;  // Номер (идентификатор) текущего процесса в коммуникаторе.
    int size = 0;   // Общее число процессов в коммуникаторе.

    // 2) Узнаем rank текущего процесса в глобальном коммуникаторе MPI_COMM_WORLD.
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // 3) Узнаем общее количество процессов в MPI_COMM_WORLD.
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // 4) Каждый процесс печатает свое сообщение.
    // Порядок строк может быть разным при каждом запуске, это нормально для MPI.
    std::cout << "Hello World from rank " << rank
              << " of " << size << std::endl;

    // 5) Корректно завершаем MPI-среду.
    MPI_Finalize();

    return 0;
}
