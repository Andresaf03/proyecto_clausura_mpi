# Bolsa de Palabras con MPI

## Descripción

Este es el proyecto de clausura para la materia de Cómputo Paralelo y en la Nube que consiste en implementar una Bolsa de Palabras.

Se implementa una versión serial y una paralela, utilizando MPI, para comparar el tiempo de ejecución y obtener un speed up de al menos 1.2x. Adicionalmente se cumple con el uso responsable y ético de la IA Generativa de acuerdo a los lineamientos de IEEE y Elsevier.

## Contenido

- Canal completo para construir una matriz bolsa de palabras a partir de libros en texto plano.
- Dos archivps (`serial` y `paralelo`) que derivan del mismo `main`, lo que facilita comparar tiempos bajo los mismos argumentos.
- Scripts de build (Makefile y tarea de VS Code), datasets de ejemplo en `data/books/` y `results/` para almacenar salidas sin ensuciar el historial.
- Los CSV generados (`bow_serial.csv`, `bow_mpi.csv`) escriben en la carpeta `results/`.

## Integrantes

- Andrés Álvarez Flores (208450)
- Nicolas Álvarez Ortega (206379)

## Estrategia de implementación

*Nota:* Aprovechamos que `map` ya ordena lexicográficamente su contenido.

### Serial

1. Leer la lista de rutas (una por línea) y cargar cada archivo completo en memoria.
2. Tokenizar cada documento: convertir a minúsculas, filtrar cualquier delimitador no alfanumérico y construir un `std::map<string,int>` con los conteos por documento.
3. Unir todos los mapas para generar un vocabulario global ordenado (las columnas de la matriz).
4. Recorrer el vocabulario para cada documento y producir filas densas (vector de enteros) que representan la bolsa de palabras.
5. Escribir `results/bow_serial.csv` con encabezado (`document, palabra1, ...`) y las filas en el mismo orden que la lista de entrada.

### Paralela

1. `rank 0` reparte las rutas entre procesos MPI en esquema round-robin (cada proceso recibe un subconjunto, si el número de procesos es igual al número de documentos cada proceso recibe un documento).
2. Cada proceso ejecuta localmente las mismas funciones del serial (lectura, tokenización, conteo) sobre sus documentos.
3. Los vocabularios locales se envían a `rank 0`, que construye un vocabulario global ordenado y lo difunde vía `MPI_Bcast` para garantizar el mismo orden de columnas en todos los procesos.
4. Cada proceso convierte sus mapas en filas densas usando el vocabulario global y las devuelve con `MPI_Gatherv`, junto con el índice original del documento.
5. `rank 0` ordena las filas según el índice, escribe `results/bow_mpi.csv` y calcula el tiempo total usando el máximo de los tiempos locales (`MPI_Reduce` con `MPI_MAX`), reflejando cuánto duró realmente la etapa paralela completa.

## Hallazgos principales

- La tokenización basada en `std::isalnum` permitió soportar archivos con comas, saltos de línea y puntuación mixta sin reglas adicionales.
- El flujo MPI distribuye carga en round-robin y sincroniza vocabulario/filas con `MPI_Gatherv` + `MPI_Bcast` para garantizar el mismo orden de columnas.
- En mediciones locales con los seis libros de ejemplo se obtuvo un speed-up promedio de **~1.8×**, superando la meta de 1.2×.

## Requisitos y Compilación

- **Compilador:** `mpicxx` (OpenMPI o MPICH). También se puede usar `g++`, pero es necesario que tenga acceso a los encabezados de MPI (`mpi.h`), por lo que se recomienda mantener `mpicxx` como predeterminado.
- **Estándar:** C++17.
- **Build por defecto:** el repositorio incluye un `Makefile` que compila un único ejecutable (`build/bow_app`) enlazando `src/main.cpp`, `src/serial.cpp` y `src/paralelo.cpp`, además de exponer los encabezados del directorio `include/bow` para que funcionen los `#include "bow/..."`. El ejecutable del `Makefile` se guarda en `/build`
- **Build rápido desde VS Code:** puedes crear una tarea local de VS Code que invoque `mpicxx` y genere un binario auxiliar en `src/main`; al no versionar `.vscode/`, cada desarrollador mantiene su propia configuración local.

Pasos:

```bash
make # Compila con mpicxx (variable MPI_CXX) y añade -Iinclude automáticamente
./build/bow_app <num_procs> data/libros.txt <num_experimentos>
```

Si necesitas forzar otro compilador (por ejemplo `g++`), puedes hacerlo así:

```bash
make MPI_CXX=g++
```

solo asegúrate de que `g++` conozca la instalación de MPI o añade manualmente las rutas necesarias.

### Alternativa: Ejecutar desde VS Code

1. Abre el proyecto en VS Code.
2. Crea `.vscode/c_cpp_properties.json` con algo como:

   ```json
   {
     "configurations": [
       {
         "name": "BOW",
         "includePath": ["${workspaceFolder}/include", "${workspaceFolder}/**"],
         "compilerPath": "mpicxx",
         "cppStandard": "c++17"
       }
     ],
     "version": 4
   }
   ```

   Esto asegura que los `#include "bow/..."` se resuelvan (los headers viven en `include/bow`, alineados con el namespace `bow`).
3. Crea `.vscode/tasks.json` para mapear la tarea de build local, por ejemplo:

   ```json
   {
     "version": "2.0.0",
     "tasks": [
       {
         "label": "Build src/main (mpicxx)",
         "type": "shell",
         "command": "mpicxx",
         "args": ["-O2", "-std=c++17", "-Wall", "-Wextra", "-pedantic", "-I", "include",
                   "src/main.cpp", "src/serial.cpp", "src/paralelo.cpp", "-o", "src/main"],
         "group": {"kind": "build", "isDefault": true},
         "problemMatcher": ["$gcc"]
       }
     ]
   }
   ```

4. Presiona `Cmd+Shift+B` (o selecciona **Run Build Task...**) para invocar esa tarea.
5. Se generará `src/main`, al que puedes pasar los mismos parámetros que a `build/bow_app`:

```bash
src/main <num_procs> data/libros.txt <num_experimentos>
```

Ambos binarios comparten el mismo código fuente; elige el flujo que te sea más cómodo.

## Uso

- Ejecutar todo en un solo proceso (útil para depuración rápida):

  ```bash
  ./src/main <num_procs> data/libros.txt <num_experimentos>
  ```

- Ejecutar la versión paralela con MPI (recomendado para medir speed-up):

  ```bash
  mpirun -np <num_procs> ./src/main <list_length> <list_directory> <num_experimentos>
  ```

  Procura que el argumento `<num_procs>` coincida con el `-np` real. Cada corrida actualiza `results/bow_serial.csv` y/o `results/bow_mpi.csv`, reutilizando la misma lista de entrada.

- Ejemplo típico con los seis libros provistos, seis procesos y diez experimentos promediados:

  ```bash
  mpirun -np 6 ./src/main 6 data/libros.txt 10
  ```

*Nota:* también se puede utilizar el ejecutable generado por el `Makefile`, basta con sustituir `<./src/main>` por `<./build/bow_app>`.

## Visualización y validación

- Crea un entorno para el notebook:

  ```bash
  python -m venv .venv
  source .venv/bin/activate  # Windows: .venv\\Scripts\\activate
  pip install -r src/requirements.txt
  ```

- Abre `src/bow_visualization.ipynb` en VS Code o Jupyter.
- Ejecuta todas las celdas, las primeras celdas comprueban que ambos csv generados sean iguales y muestan los resultados. La última celda carga una vista interactiva para:
  - Elegir el CSV (`results/bow_mpi.csv` o `results/bow_serial.csv`).
  - Buscar una palabra clave y mostrar solo las columnas que la contienen (deja vacío para ver las primeras columnas).
- Opcional: si tienes la extensión Data Wrangler en VS Code, abre cualquiera de los CSV y selecciona **Open in Data Wrangler** para filtrarlos con una interfaz de tabla.

## Estructura del repositorio

```txt
.
├── build/
│   └── .gitkeep
├── data/
│   ├── books/
│   │   ├── dickens_a_christmas_carol.txt
│   │   └── …
│   └── libros.txt
├── include/
│   └── bow/
│       ├── experiment.hpp
│       ├── paralelo.hpp
│       └── serial.hpp
├── results/
│   └── .gitkeep
├── src/
│   ├── main.cpp
│   ├── paralelo.cpp
│   └── serial.cpp
├── Makefile
├── README.md
├── .gitignore
└── Proyecto Clausura.pdf
```

## Uso responsable y ético de la IA Generativa

Únicamente se utilizo IA Generativa para comentar los fragmentos de código relevantes, planear la estructura del proyecto, diseñar los experimentos y la escritura y lectura de archivos, revisar las lógicas de implementación y la vista interactiva del *notebook*. **No** se utilizó IA Generativa para ejecutar código ni para generar datos de ningún tipo, todos los experimentos fueron ejecutados localmente.
