#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

MPI_RUN="${MPI_RUN:-mpirun}"
M="${M:-2000}"
K_LIST="${K_LIST:-2000 4000 8000}"
P_LIST="${P_LIST:-1 2 4}"
RESULTS="${RESULTS:-${SCRIPT_DIR}/results.csv}"

make -C "${ROOT_DIR}" all

echo "mode,processes,M,K,a,T,X,tau,h,c,time_seconds,max_error,speedup,efficiency" > "${RESULTS}"

for K in ${K_LIST}; do
    seq_row="$("${ROOT_DIR}/02_execution/transport_seq" --M "${M}" --K "${K}" --csv)"
    seq_time="$(awk -F, '{print $11}' <<< "${seq_row}")"
    echo "${seq_row},1,1" >> "${RESULTS}"

    for P in ${P_LIST}; do
        mpi_row="$("${MPI_RUN}" -np "${P}" "${ROOT_DIR}/02_execution/transport_mpi" --M "${M}" --K "${K}" --csv)"
        mpi_time="$(awk -F, '{print $11}' <<< "${mpi_row}")"
        speedup="$(awk -v seq="${seq_time}" -v par="${mpi_time}" 'BEGIN { print seq / par }')"
        efficiency="$(awk -v s="${speedup}" -v p="${P}" 'BEGIN { print s / p }')"
        echo "${mpi_row},${speedup},${efficiency}" >> "${RESULTS}"
    done
done

echo "Results written to ${RESULTS}"
