# Virtual MMU Page Replace Explorer

Simulador de memoria virtual en **C++**: traduce direcciones lógicas a físicas con **TLB** y **tabla de páginas**, aplica algoritmos de reemplazo de páginas (**FIFO, LRU, Reloj y Óptimo**), demuestra la **Anomalía de Belady**, lee datos reales del kernel de Linux (`/proc/self/pagemap`) y muestra los resultados en un **dashboard web** que se actualiza en tiempo real.

Proyecto de la asignatura Sistemas Operativos — Tecnología en Gestión de Sistemas Informáticos, UNINTEP.

## Características

- **MMU simulada:** separa página y desplazamiento, consulta el TLB, luego la tabla de páginas y, si la RAM está llena, dispara el algoritmo de reemplazo.
- **4 algoritmos intercambiables:** FIFO, LRU (lista + mapa, O(1)), Reloj (bit de referencia y puntero circular) y Óptimo (métrica de control).
- **Métricas:** page faults, hits y misses del TLB.
- **Anomalía de Belady:** tabla de fallos con 1 a 7 marcos.
- **Interacción con el kernel:** lectura de `/proc/self/pagemap` con `open`, `pread` y `mmap`, con manejo de errores y sin fugas de memoria.
- **Concurrencia:** modelo productor-consumidor con `std::thread`, `std::mutex` y variables de condición.
- **Dashboard:** mapa de marcos, panel del TLB y gráfica de fallos contra marcos.

## Requisitos

| Requisito | Detalle |
|---|---|
| Sistema operativo | **Linux** (probado en Ubuntu). `/proc/PID/pagemap` no existe en Windows ni macOS |
| Compilador | `g++` con C++17 (`build-essential`) |
| Python 3 | Solo para el servidor local del dashboard |
| Navegador | Firefox, Chrome o similar |
| Git | Para clonar el proyecto |
| Valgrind | Opcional, para repetir las pruebas de memoria |

> **¿Usas Windows?** Instala una máquina virtual (VMware o VirtualBox) con Ubuntu, con 4 GB de RAM y 40 GB de disco, o usa WSL2 con Ubuntu.

## Instalación

```bash
sudo apt update
sudo apt install build-essential git python3 valgrind

git clone <URL-del-repositorio>
cd virtual-mmu-page-replace-explorer
```

## Compilar

```bash
g++ -std=c++17 -Wall -Wextra -pthread -o simulador src/main.cpp
```

La opción `-pthread` es obligatoria porque el proyecto usa hilos.

## Uso

```bash
./simulador <archivo> <marcos> <modo> [opciones]
```

| Comando | Qué hace |
|---|---|
| `./simulador pruebas/secuencia.txt 3` | Compara los 4 algoritmos con 3 marcos |
| `./simulador pruebas/secuencia.txt 3 lru` | Traza detallada de un algoritmo (`fifo`, `lru`, `reloj`, `optimo`) |
| `./simulador pruebas/secuencia.txt 3 lru 8` | Igual, con un TLB de 8 entradas (por defecto 4) |
| `./simulador pruebas/secuencia.txt 3 belady` | Tabla de fallos con 1 a 7 marcos y genera `dashboard/belady.json` |
| `./simulador pruebas/secuencia.txt 3 pagemap` | Lectura real de `/proc/self/pagemap` (repetir con `sudo` para ver los marcos físicos) |
| `./simulador pruebas/secuencia.txt 3 vivo lru 700` | Simulación en vivo con pausa de 700 ms por paso; publica `dashboard/estado.json` |

### Resultados esperados con `secuencia.txt`

| Marcos | FIFO | LRU | Reloj | Óptimo |
|---|---|---|---|---|
| 3 | 9 | 10 | 9 | 7 |
| 4 | 10 | 8 | 10 | 6 |

Con FIFO, pasar de 3 a 4 marcos **aumenta** los fallos de 9 a 10: es la Anomalía de Belady.

## Dashboard

Se necesitan **dos terminales**.

1. **Terminal 1:** `./simulador pruebas/secuencia.txt 3 belady`
2. **Terminal 2:** `cd dashboard && python3 -m http.server 8000` (dejarla abierta)
3. **Navegador:** abrir `http://localhost:8000`
4. **Terminal 1:** `./simulador pruebas/secuencia.txt 3 vivo lru 700`

Para apagar el servidor: `Ctrl + C` en la Terminal 2.

## Secuencia propia

El archivo de entrada es un `.txt` con números de página separados por espacios:

```
1 2 3 4 1 2 5 1 2 3 4 5
```

## Estructura del proyecto

```
virtual-mmu-page-replace-explorer/
├── src/
│   ├── main.cpp            # modos y argumentos
│   ├── mmu.h               # MMU: TLB, tabla de páginas, marcos, métricas
│   ├── politicas.h         # FIFO, LRU, Reloj, Óptimo
│   ├── pagemap_reader.h    # lectura de /proc/self/pagemap
│   ├── cola.h              # buffer productor-consumidor
│   ├── estado.h            # fotos del estado y JSON
│   └── vivo.h              # modo en vivo con dos hilos
├── pruebas/
│   └── secuencia.txt
├── dashboard/
│   ├── index.html
│   └── belady.json
└── README.md
```

## Pruebas de memoria

```bash
g++ -std=c++17 -Wall -Wextra -g -pthread -o simulador src/main.cpp
valgrind --leak-check=full ./simulador pruebas/secuencia.txt 3 vivo lru 0
valgrind --leak-check=full ./simulador pruebas/secuencia.txt 3 pagemap
```

Resultado esperado: `All heap blocks were freed -- no leaks are possible` y `ERROR SUMMARY: 0 errors`.

## Problemas frecuentes

| Síntoma | Solución |
|---|---|
| `fatal error: cola.h: No such file or directory` | Falta un archivo en `src`; verificar con `ls src` |
| `undefined reference to pthread_create` | Compilar con `-pthread` |
| `Error: la secuencia esta vacia` | El `.txt` está vacío o sin guardar |
| `Error: no se pudo abrir ...` | Ejecutar desde la raíz del proyecto |
| Marco físico `(oculto: requiere sudo)` | Normal sin permisos; ejecutar con `sudo` |
| Dashboard en "Esperando estado.json..." | Ejecutar el modo `vivo` y servir desde la carpeta `dashboard` |
| Gráfica vacía | Ejecutar primero el modo `belady` |
| Cambios en la página no se ven | Recargar con `Ctrl + Shift + R` |

## Limitaciones

- Modela un solo proceso con una única tabla de páginas.
- El desplazamiento de las direcciones lógicas es sintético cuando la entrada es un archivo de páginas.
- El Swap es conceptual: no se simulan bits de modificación ni escrituras a disco.
- La lectura de `pagemap` es demostrativa y no alimenta la simulación.

## Autoría

Daniela Blandon Garcia
