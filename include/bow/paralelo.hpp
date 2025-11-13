// paralelo.hpp: Interfaz de la futura implementación MPI.
#pragma once

#include "bow/experiment.hpp"

namespace bow {

// Ejecuta la versión paralela con MPI del algoritmo bolsa de palabras.
ExperimentResult run_parallel(const ExperimentConfig& config);

}  // namespace bow
