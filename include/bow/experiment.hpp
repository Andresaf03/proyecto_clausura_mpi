// experiment.hpp: Define estructuras compartidas para configuración y resultados.
#pragma once

#include <string>
#include <vector>

namespace bow {

// Configuración inmutable para cada experimento del proyecto.
struct ExperimentConfig {
  int num_processes = 1;          // Número de procesos solicitados para MPI.
  std::string list_path;          // Ruta del archivo con nombres de libros.
  int num_experiments = 1;        // Corridas a promediar.
  std::vector<std::string> document_paths;  // Rutas completas a documentos por experimento.
};

// Resultado agregado que permitirá calcular métricas y speed-up.
struct ExperimentResult {
  double total_time_ms = 0.0;     // Tiempo acumulado de todas las corridas.
  double average_time_ms = 0.0;   // Tiempo promedio calculado externamente.
};

}  // namespace bow
