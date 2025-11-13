// serial.cpp: La versión secuencial del algoritmo.
#include "bow/serial.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Lee un archivo completo y regresa su contenido como string.
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

// Normaliza y tokeniza el contenido manejando separadores como comas o espacios.
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

// Cuenta la frecuencia de cada token dentro de un documento.
std::map<std::string, int> count_tokens(const std::vector<std::string>& tokens) {
  std::map<std::string, int> word_counts;
  for (const auto& token : tokens) {
    ++word_counts[token];
  }
  return word_counts;
}

// Construye el vocabulario global ordenado (columnas del CSV) usando todos los documentos.
std::vector<std::string> build_vocabulary(
    const std::vector<std::map<std::string, int>>& document_counts) {
  std::map<std::string, int> ordered_unique_tokens;
  for (const auto& document_map : document_counts) {
    for (const auto& entry : document_map) {
      ordered_unique_tokens.emplace(entry.first, 0);  // map mantiene orden y evita duplicados.
    }
  }

  std::vector<std::string> vocabulary;
  vocabulary.reserve(ordered_unique_tokens.size());
  for (const auto& entry : ordered_unique_tokens) {
    vocabulary.push_back(entry.first);
  }
  return vocabulary;
}

// Genera la matriz bolsa de palabras recorriendo documentos y vocabulario.
std::vector<std::vector<int>> build_matrix(
    const std::vector<std::map<std::string, int>>& document_counts,
    const std::vector<std::string>& vocabulary) {
  std::vector<std::vector<int>> matrix;
  matrix.reserve(document_counts.size());

  for (const auto& document_map : document_counts) {
    std::vector<int> row;
    row.reserve(vocabulary.size());
    for (const auto& word : vocabulary) {
      const auto it = document_map.find(word);
      row.push_back(it != document_map.end() ? it->second : 0);  // 0 si la palabra no aparece.
    }
    matrix.push_back(std::move(row));
  }
  return matrix;
}

// Escribe la matriz resultante en un archivo CSV.
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

// Ejecuta la versión secuencial completa (I/O, tokenización, matriz y CSV).
ExperimentResult run_serial(const ExperimentConfig& config) {
  ExperimentResult result;
  if (config.document_paths.empty()) {
    std::cerr << "No hay documentos para procesar." << std::endl;
    return result;
  }

  const auto start_time = std::chrono::steady_clock::now();

  std::vector<std::map<std::string, int>> document_counts;
  std::vector<std::string> processed_names;

  for (const auto& document_path : config.document_paths) {
    const std::string content = read_file(document_path);
    if (content.empty()) {
      continue;
    }

    const std::vector<std::string> tokens = tokenize_document(content);
    document_counts.push_back(count_tokens(tokens));
    processed_names.push_back(std::filesystem::path(document_path).filename().string());
  }

  if (document_counts.empty()) {
    std::cerr << "No se pudo procesar ningún documento válido." << std::endl;
    return result;
  }

  const std::vector<std::string> vocabulary = build_vocabulary(document_counts);
  const std::vector<std::vector<int>> matrix = build_matrix(document_counts, vocabulary);

  const std::filesystem::path output_file = std::filesystem::path("results") / "bow_serial.csv";
  std::filesystem::create_directories(output_file.parent_path());
  write_csv(matrix, vocabulary, processed_names, output_file.string());

  const auto end_time = std::chrono::steady_clock::now();
  const double elapsed_ms =
      std::chrono::duration<double, std::milli>(end_time - start_time).count();
  result.total_time_ms = elapsed_ms;
  result.average_time_ms = elapsed_ms;
  return result;
}

}  // namespace bow
