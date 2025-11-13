// paralelo.cpp: Implementación de la variante MPI del algoritmo.
#include "bow/paralelo.hpp"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// Abre un archivo completo y regresa su contenido como string.
std::string read_file(const std::string& path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    std::cerr << "No se pudo abrir el archivo: " << path << std::endl;
    return {};
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Normaliza a minúsculas y separa tokens con cualquier carácter no alfanumérico.
std::vector<std::string> tokenize_document(const std::string& content) {
  std::vector<std::string> tokens;
  std::string current_token;

  for (unsigned char raw : content) {
    const char lower = static_cast<char>(std::tolower(raw));
    if (std::isalnum(lower) || lower == '_') {
      current_token.push_back(lower);
    } else {
      if (!current_token.empty()) {
        tokens.push_back(current_token);
        current_token.clear();
      }
    }
  }

  if (!current_token.empty()) {
    tokens.push_back(current_token);
  }
  return tokens;
}

// Cuenta cuántas veces aparece cada token dentro de un documento.
std::map<std::string, int> count_tokens(const std::vector<std::string>& tokens) {
  std::map<std::string, int> word_counts;
  for (const auto& token : tokens) {
    ++word_counts[token];
  }
  return word_counts;
}

// Serializa un vocabulario (ordenado) separando cada palabra con '\n'.
std::string join_words_with_newline(const std::set<std::string>& words) {
  std::string serialized;
  for (const auto& word : words) {
    serialized += word;
    serialized.push_back('\n');
  }
  return serialized;
}

// Operación inversa: divide un string por saltos de línea y descarta entradas vacías.
std::vector<std::string> split_by_newline(const std::string& data) {
  std::vector<std::string> parts;
  std::string current;
  for (char ch : data) {
    if (ch == '\n') {
      if (!current.empty()) {
        parts.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    parts.push_back(current);
  }
  return parts;
}

// Escribe la matriz final (ordenada por documento) en formato CSV.
void write_csv(const std::vector<std::vector<int>>& matrix,
               const std::vector<std::string>& vocabulary,
               const std::vector<std::string>& doc_names,
               const std::string& output_path) {
  std::ofstream output(output_path);
  if (!output.is_open()) {
    std::cerr << "No se pudo abrir el CSV de salida: " << output_path << std::endl;
    return;
  }

  output << "document";
  for (const auto& word : vocabulary) {
    output << ',' << word;
  }
  output << '\n';

  for (std::size_t i = 0; i < matrix.size(); ++i) {
    output << doc_names[i];
    for (int value : matrix[i]) {
      output << ',' << value;
    }
    output << '\n';
  }
}

}  // namespace

namespace bow {

// Ejecuta la versión MPI distribuyendo documentos y reuniendo resultados en rank 0.
ExperimentResult run_parallel(const ExperimentConfig& config) {
  ExperimentResult result;
  if (config.document_paths.empty()) {
    return result;
  }

  int world_rank = 0;
  int world_size = 1;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  const auto start_time = std::chrono::steady_clock::now();

  std::vector<std::map<std::string, int>> local_counts;
  std::vector<int> local_doc_indices;
  local_counts.reserve((config.document_paths.size() + world_size - 1) / world_size);

  for (std::size_t idx = world_rank; idx < config.document_paths.size(); idx += world_size) {
    const std::string& path = config.document_paths[idx];
    const std::string content = read_file(path);
    if (content.empty()) {
      continue;
    }

    const std::vector<std::string> tokens = tokenize_document(content);
    local_counts.push_back(count_tokens(tokens));
    local_doc_indices.push_back(static_cast<int>(idx));
  }

  std::set<std::string> local_vocab;
  for (const auto& doc_map : local_counts) {
    for (const auto& entry : doc_map) {
      local_vocab.insert(entry.first);
    }
  }

  const std::string local_vocab_serialized = join_words_with_newline(local_vocab);
  const int local_vocab_bytes = static_cast<int>(local_vocab_serialized.size());

  std::vector<int> vocab_byte_counts;
  if (world_rank == 0) {
    vocab_byte_counts.resize(world_size);
  }
  MPI_Gather(&local_vocab_bytes, 1, MPI_INT,
             world_rank == 0 ? vocab_byte_counts.data() : nullptr, 1, MPI_INT, 0, MPI_COMM_WORLD);

  std::vector<int> vocab_displs;
  std::vector<char> global_vocab_buffer;
  if (world_rank == 0) {
    vocab_displs.resize(world_size);
    int total = 0;
    for (int i = 0; i < world_size; ++i) {
      vocab_displs[i] = total;  // Offset dentro del buffer concatenado.
      total += vocab_byte_counts[i];  // Sumamos los bytes aportados por cada rank.
    }
    global_vocab_buffer.resize(total);
  }

  MPI_Gatherv(local_vocab_serialized.data(), local_vocab_bytes, MPI_CHAR,
              world_rank == 0 ? global_vocab_buffer.data() : nullptr,
              world_rank == 0 ? vocab_byte_counts.data() : nullptr,
              world_rank == 0 ? vocab_displs.data() : nullptr, MPI_CHAR, 0, MPI_COMM_WORLD);

  std::string broadcast_vocab;
  if (world_rank == 0) {
    std::set<std::string> global_vocab_set;
    for (int i = 0; i < world_size; ++i) {
      const int offset = vocab_displs[i];
      const int length = vocab_byte_counts[i];
      if (length == 0) {
        continue;
      }
      std::string chunk(global_vocab_buffer.begin() + offset,
                        global_vocab_buffer.begin() + offset + length);
      const auto words = split_by_newline(chunk);
      global_vocab_set.insert(words.begin(), words.end());
    }
    for (const auto& word : global_vocab_set) {
      broadcast_vocab += word;
      broadcast_vocab.push_back('\n');
    }
  }

  int vocab_bytes = static_cast<int>(broadcast_vocab.size());
  MPI_Bcast(&vocab_bytes, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (world_rank != 0) {
    broadcast_vocab.resize(vocab_bytes);
  }
  MPI_Bcast(!broadcast_vocab.empty() ? broadcast_vocab.data() : nullptr, vocab_bytes, MPI_CHAR, 0,
            MPI_COMM_WORLD);
  const std::vector<std::string> global_vocabulary = split_by_newline(broadcast_vocab);

  const int vocab_size = static_cast<int>(global_vocabulary.size());
  std::unordered_map<std::string, int> vocab_index;
  vocab_index.reserve(global_vocabulary.size());
  for (int i = 0; i < vocab_size; ++i) {
    vocab_index.emplace(global_vocabulary[i], i);
  }

  const int local_row_count = static_cast<int>(local_counts.size());
  std::vector<int> row_counts;
  if (world_rank == 0) {
    row_counts.resize(world_size);
  }
  MPI_Gather(&local_row_count, 1, MPI_INT, world_rank == 0 ? row_counts.data() : nullptr, 1,
             MPI_INT, 0, MPI_COMM_WORLD);

  std::vector<int> local_rows_flat;
  local_rows_flat.reserve(static_cast<std::size_t>(local_row_count) * vocab_size);
  for (const auto& document_map : local_counts) {
    std::vector<int> row(vocab_size, 0);
    for (const auto& entry : document_map) {
      const auto lookup = vocab_index.find(entry.first);
      if (lookup != vocab_index.end()) {
        row[lookup->second] = entry.second;
      }
    }
    local_rows_flat.insert(local_rows_flat.end(), row.begin(), row.end());
  }

  std::vector<int> doc_index_displs;
  std::vector<int> gathered_doc_indices;
  if (world_rank == 0) {
    doc_index_displs.resize(world_size);
    int running = 0;
    for (int i = 0; i < world_size; ++i) {
      doc_index_displs[i] = running;  // Posición donde inicia el bloque del proceso i.
      running += row_counts[i];
    }
    gathered_doc_indices.resize(running);
  }

  MPI_Gatherv(local_doc_indices.data(), local_row_count, MPI_INT,
              world_rank == 0 ? gathered_doc_indices.data() : nullptr,
              world_rank == 0 ? row_counts.data() : nullptr,
              world_rank == 0 ? doc_index_displs.data() : nullptr, MPI_INT, 0, MPI_COMM_WORLD);

  const int local_value_count = vocab_size * local_row_count;
  std::vector<int> value_counts;
  std::vector<int> value_displs;
  std::vector<int> gathered_values;
  if (world_rank == 0) {
    value_counts.resize(world_size);
    value_displs.resize(world_size);
    int running = 0;
    for (int i = 0; i < world_size; ++i) {
      value_counts[i] = row_counts[i] * vocab_size;  // Número de enteros que envía cada proceso.
      value_displs[i] = running;                     // Offset dentro del buffer plano.
      running += value_counts[i];
    }
    gathered_values.resize(running);
  }

  MPI_Gatherv(local_rows_flat.data(), local_value_count, MPI_INT,
              world_rank == 0 ? gathered_values.data() : nullptr,
              world_rank == 0 ? value_counts.data() : nullptr,
              world_rank == 0 ? value_displs.data() : nullptr, MPI_INT, 0, MPI_COMM_WORLD);

  if (world_rank == 0) {
    const int total_rows =
        std::accumulate(row_counts.begin(), row_counts.end(), 0,
                        std::plus<int>());  // Total de documentos recibidos.
    std::vector<std::pair<int, std::vector<int>>> ordered_rows;
    ordered_rows.reserve(total_rows);

    std::size_t offset = 0;
    for (int i = 0; i < total_rows; ++i) {
      std::vector<int> row(vocab_size, 0);
      if (vocab_size > 0) {
        std::copy(gathered_values.begin() + offset, gathered_values.begin() + offset + vocab_size,
                  row.begin());
      }
      ordered_rows.push_back({gathered_doc_indices[i], std::move(row)});
      offset += vocab_size;
    }
    std::sort(ordered_rows.begin(), ordered_rows.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    std::vector<std::string> doc_names;
    std::vector<std::vector<int>> matrix;
    doc_names.reserve(ordered_rows.size());
    matrix.reserve(ordered_rows.size());

    for (const auto& row : ordered_rows) {
      doc_names.push_back(std::filesystem::path(config.document_paths[row.first]).filename().string());
      matrix.push_back(row.second);
    }

    if (!doc_names.empty()) {
      const std::filesystem::path output_file = std::filesystem::path("results") / "bow_mpi.csv";
      std::filesystem::create_directories(output_file.parent_path());
      write_csv(matrix, global_vocabulary, doc_names, output_file.string());
    } else {
      std::cerr << "MPI: No se generaron filas, revisar entradas." << std::endl;
    }
  }

  // Aseguramos que todos escribieron/envíaron antes de tomar el tiempo final.
  MPI_Barrier(MPI_COMM_WORLD);
  const auto end_time = std::chrono::steady_clock::now();
  const double local_elapsed =
      std::chrono::duration<double, std::milli>(end_time - start_time).count();

  double max_elapsed = 0.0;
  // El tiempo paralelo total es el del proceso que terminó más tarde.
  MPI_Reduce(&local_elapsed, &max_elapsed, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  if (world_rank == 0) {
    result.total_time_ms = max_elapsed;
    result.average_time_ms = max_elapsed;
  }

  return result;
}

}  // namespace bow
