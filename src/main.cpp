// main.cpp: Punto de entrada que orquesta corridas seriales y paralelas, y calcula speed-up.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <mpi.h>

#include "bow/paralelo.hpp"
#include "bow/serial.hpp"

namespace {

// Carga la lista de archivos desde un archivo de texto plano (uno por línea).
std::vector<std::string> load_document_names(const std::string& list_path) {
  std::vector<std::string> names;
  std::ifstream input(list_path);
  if (!input.is_open()) {
    std::cerr << "No se pudo abrir la lista de archivos: " << list_path << std::endl;
    return names;
  }

  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty()) {
      names.push_back(line);
    }
  }
  return names;
}

// Genera rutas completas (ej. data/books/*.txt) a partir de los nombres en la lista.
// Si el archivo no está junto a la lista, se intenta en el subdirectorio `books/`.
std::vector<std::string> resolve_document_paths(const std::string& list_path,
                                                const std::vector<std::string>& names) {
  std::vector<std::string> resolved;
  const std::filesystem::path list_dir = std::filesystem::path(list_path).parent_path();
  const std::filesystem::path books_dir = list_dir / "books";

  for (const auto& name : names) {
    std::filesystem::path candidate = list_dir / name;
    if (!std::filesystem::exists(candidate)) {
      candidate = books_dir / name;
    }
    if (!std::filesystem::exists(candidate)) {
      std::cerr << "Advertencia: no se encontró el archivo " << name << std::endl;
      continue;
    }
    resolved.push_back(candidate.string());
  }
  return resolved;
}

}  // namespace

int main(int argc, char** argv) {
  // Inicializamos MPI una única vez para toda la orquestación.
  MPI_Init(nullptr, nullptr);

  int world_rank = 0;
  int world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  if (argc < 4) {
    if (world_rank == 0) {
      std::cerr << "Uso: " << argv[0]
                << " <num_procesos> <ruta_lista_archivos> <num_experimentos>" << std::endl;
    }
    MPI_Finalize();
    return 1;
  }

  const int requested_processes = std::stoi(argv[1]);
  const std::string list_path = argv[2];
  const int num_experiments = std::stoi(argv[3]);

  if (world_rank == 0 && requested_processes != world_size) {
    std::cerr << "Advertencia: se ejecuta con " << world_size
              << " procesos MPI, pero se solicitó " << requested_processes << std::endl;
  }

  const std::vector<std::string> documents = load_document_names(list_path);
  if (world_rank == 0) {
    if (documents.empty()) {
      std::cerr << "La lista de libros está vacía o no se pudo leer." << std::endl;
    } else {
      std::cout << "Documentos detectados (" << documents.size() << "):" << std::endl;
      for (const auto& doc : documents) {
        std::cout << "  - " << doc << std::endl;
      }
    }
  }

  bow::ExperimentConfig base_config;
  base_config.num_processes = requested_processes;
  base_config.list_path = list_path;
  base_config.num_experiments = num_experiments;
  base_config.document_paths = resolve_document_paths(list_path, documents);
  if (base_config.document_paths.empty()) {
    if (world_rank == 0) {
      std::cerr << "No se pudo resolver ninguna ruta válida de documentos." << std::endl;
    }
    MPI_Finalize();
    return 1;
  }

  double serial_total = 0.0;
  double parallel_total = 0.0;

  for (int i = 0; i < num_experiments; ++i) {
    if (world_rank == 0) {
      std::cout << "[Experimento " << (i + 1) << "/" << num_experiments << "]" << std::endl;
      const auto serial_result = bow::run_serial(base_config);
      serial_total += serial_result.average_time_ms;
      std::cout << "  Serial promedio acumulado: " << serial_total / (i + 1) << " ms" << std::endl;
    }

    // Sincronizamos todos los procesos antes de iniciar la corrida paralela.
    MPI_Barrier(MPI_COMM_WORLD);
    const auto parallel_result = bow::run_parallel(base_config);
    if (world_rank == 0) {
      parallel_total += parallel_result.average_time_ms;
      std::cout << "  Paralelo promedio acumulado: " << parallel_total / (i + 1) << " ms"
                << std::endl;
    }
  }

  MPI_Finalize();

  if (world_rank == 0) {
    const double serial_avg = num_experiments > 0 ? serial_total / num_experiments : 0.0;
    const double parallel_avg = num_experiments > 0 ? parallel_total / num_experiments : 0.0;
    const double speedup = (parallel_avg > 0.0) ? (serial_avg / parallel_avg) : 0.0;

    std::cout << "==== Resumen ====" << std::endl;
    std::cout << "Tiempo promedio serial: " << serial_avg << " ms" << std::endl;
    std::cout << "Tiempo promedio paralelo: " << parallel_avg << " ms" << std::endl;
    std::cout << "Speed-up estimado: " << speedup << std::endl;
  }

  return 0;
}
