// serial.hpp: Interfaz de la futura implementación secuencial.
#pragma once

#include "bow/experiment.hpp"

namespace bow {

// Ejecuta la versión serial del algoritmo bolsa de palabras.
ExperimentResult run_serial(const ExperimentConfig& config);

}  // namespace bow
