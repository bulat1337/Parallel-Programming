CXX ?= g++
MPICXX ?= mpic++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

ADMISSION_BIN = 01_admission/latency_mpi
SEQ_BIN = 02_execution/transport_seq
MPI_BIN = 02_execution/transport_mpi

.PHONY: all admission execution clean

all: admission execution

admission: $(ADMISSION_BIN)

execution: $(SEQ_BIN) $(MPI_BIN)

$(ADMISSION_BIN): 01_admission/latency_mpi.cpp
	$(MPICXX) $(CXXFLAGS) $< -o $@

$(SEQ_BIN): 02_execution/transport_seq.cpp 02_execution/transport_common.hpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(MPI_BIN): 02_execution/transport_mpi.cpp 02_execution/transport_common.hpp
	$(MPICXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(ADMISSION_BIN) $(SEQ_BIN) $(MPI_BIN)
	rm -f 03_submission/results.csv
