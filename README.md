# Virtual MMU Page Replace Explorer

Simulador de memoria virtual en **C++**: traduce direcciones lógicas a físicas con **TLB** y **tabla de páginas**, aplica algoritmos de reemplazo de páginas (**FIFO, LRU, Reloj y Óptimo**), demuestra la **Anomalía de Belady**, detecta el **thrashing** (hiperpaginación), lee datos reales del kernel de Linux (`/proc/self/pagemap`) y muestra los resultados en un **dashboard web** que se actualiza en tiempo real.

Proyecto de la asignatura Sistemas Operativos — Tecnología en Gestión de Sistemas Informáticos, UNINTEP.

## Características

- **MMU simulada:** separa página y desplazamiento, consulta el TLB, luego la tabla de páginas y, si la RAM está llena, dispara el algoritmo de reemplazo.
- **4 algoritmos intercambiables:** FIFO, LRU (lista + mapa, O(1)), Reloj (bit de referencia y puntero circular) y Óptimo (métrica de control).
- **Métricas:** page faults, hits y misses del TLB.
- **Anomalía de Belady:** tabla de fallos con 1 a 7 marcos.
- **Thrashing:** detector basado en el *conjunto de trabajo* (WSS) con estados NORMAL / ALERTA / THRASHING, utilización de CPU estimada y curva CPU vs. marcos (ver sección *Thrashing*).
- **Interacción con el kernel:** lectura de `/proc/self/pagemap` con `open`, `pread` y `mmap`, con manejo de errores y sin fugas de memoria.
- **Concurrencia:** modelo productor-consumidor con `std::thread`, `std::mutex` y variables de condición.
- **Dashboard:** mapa de marcos, panel del TLB y gráfica de fallos contra marcos.

## Requisitos

| Requisito | Detalle |
|---|---|
| Sistema operativo | **Linux** (probado en Ubuntu). `/proc/PID/pagemap` no existe en Windows ni macOS |
| Compilador | `g++` con C++17 (`build-essential`) |
| Python 3 | Solo para `servidor.py` (servidor local del dashboard) |
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
| `./simulador pruebas/thrashing.txt 3 thrashing` | Tabla de CPU útil y pasos en thrashing con 1 a 7 marcos; genera `dashboard/thrashing.json` |
| `./simulador pruebas/thrashing.txt 3 lru 4 8` | Traza detallada con TLB de 4 entradas y ventana de thrashing Δ = 8 (5.º argumento) |
| `./simulador pruebas/secuencia.txt 3 pagemap` | Lectura real de `/proc/self/pagemap` (repetir con `sudo` para ver los marcos físicos) |
| `./simulador pruebas/secuencia.txt 3 vivo lru 700 6` | Simulación en vivo con pausa de 700 ms por paso y ventana Δ = 6 (opcional); publica `dashboard/estado.json` |

### Resultados esperados con `secuencia.txt`

| Marcos | FIFO | LRU | Reloj | Óptimo |
|---|---|---|---|---|
| 3 | 9 | 10 | 9 | 7 |
| 4 | 10 | 8 | 10 | 6 |

Con FIFO, pasar de 3 a 4 marcos **aumenta** los fallos de 9 a 10: es la Anomalía de Belady.

## Dashboard

Basta **una terminal**:

1. En la raíz del proyecto (ya compilado): `python3 servidor.py` (dejarla abierta)
2. Abrir en el navegador `http://localhost:8000`
3. Elegir secuencia, marcos, algoritmo y pausa, y pulsar **▶ Ejecutar**

El selector de algoritmo permite correr uno solo (FIFO, LRU, Reloj, Óptimo) o **Todos (comparar los 4)**, que ejecuta los cuatro a la vez y los muestra juntos en el mapa de marcos, la tabla del TLB y las gráficas.

Para apagar el servidor: `Ctrl + C` (también detiene las simulaciones en curso). Los datos de cada corrida se guardan en `.vivo/`, que está en `.gitignore`.

## Thrashing

**Qué es:** el proceso necesita más páginas *a la vez* (su **conjunto de trabajo**) de las que caben en la RAM. Casi cada acceso es un page fault, cada fallo expulsa una página que se necesitará enseguida, y el sistema pasa más tiempo haciendo swap con el disco que ejecutando instrucciones: la **CPU útil se desploma**.

**Cómo lo detecta el simulador** (`src/thrashing.h`, modelo del conjunto de trabajo de Denning). Después de cada acceso se analizan los últimos **Δ** accesos (por defecto 6):

| Medida | Significado |
|---|---|
| **WSS** | páginas *distintas* usadas en la ventana (conjunto de trabajo) |
| **Tasa de fallos** | page faults en la ventana / accesos en la ventana |
| **CPU útil** | trabajo útil / (trabajo útil + tiempo de swap). Modelo didáctico: un acceso cuesta 1 y cada page fault suma 10 |

| Estado | Condición |
|---|---|
| **THRASHING** | `WSS > marcos` **y** tasa de fallos ≥ 50 % |
| **ALERTA** | `WSS > marcos`, pero todavía fallan pocas páginas |
| **NORMAL** | cualquier otro caso (la carga inicial de páginas no cuenta como thrashing mientras el WSS quepa) |

También se cuentan los **pasos en thrashing** y los **episodios** (cada vez que se entra en thrashing).

**Secuencia de prueba `pruebas/thrashing.txt`** (50 accesos, usar 3 marcos): tiene tres fases para ver cómo se entra y se sale.

1. Pasos 1-15: localidad de 3 páginas (`1 2 3`) → NORMAL.
2. Pasos 16-42: bucle de 6 páginas (`1 2 3 4 5 6`) que no cabe en 3 marcos → ALERTA y luego **THRASHING**.
3. Pasos 43-50: vuelve la localidad (`7 8`) → el detector **sale** de thrashing.

```bash
./simulador pruebas/thrashing.txt 3 lru      # traza con líneas "Thrashing:" y avisos >>> ENTRA / <<< SALE
./simulador pruebas/thrashing.txt 3          # resumen: pasos en thrashing, episodios y CPU % por algoritmo
./simulador pruebas/thrashing.txt 3 thrashing  # CPU % según marcos 1 a 7
```

Resultado esperado con `thrashing.txt` y 3 marcos: FIFO, LRU y Reloj dan 32 page faults, 29 de 50 pasos en THRASHING, 1 episodio y 13.5 % de CPU útil (Óptimo: 22 fallos, 18.5 %). Con **6 marcos o más** el thrashing desaparece (8 fallos, 38.5 % de CPU). Ningún algoritmo de reemplazo lo arregla con pocos marcos: la solución es dar más marcos o bajar la cantidad de procesos.

**En el dashboard:** banner rojo "THRASHING" cuando se detecta, panel *Detector de thrashing* por algoritmo (WSS contra marcos, barra de tasa de fallos con su umbral, barra de CPU útil, contadores), marcos que vibran en rojo, franjas rojas en la gráfica de avance y la gráfica *Utilización de CPU vs. marcos asignados* con círculos rojos donde hay thrashing.

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
│   ├── thrashing.h         # detector de thrashing (WSS, tasa de fallos, CPU útil)
│   └── vivo.h              # modo en vivo con dos hilos
├── pruebas/
│   ├── secuencia.txt
│   └── thrashing.txt       # secuencia que provoca thrashing
├── dashboard/
│   ├── index.html
│   ├── belady.json
│   └── thrashing.json      # CPU útil vs. marcos (lo genera el modo thrashing)
├── servidor.py             # servidor local del dashboard
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
| Dashboard en "Esperando estado.json..." | Pulsar **▶ Ejecutar** y verificar que `python3 servidor.py` corre desde la raíz |
| Gráfica vacía | Pulsar **▶ Ejecutar** (genera `belady.json`) |
| Falta la opción «Todos» / dos botones | `index.html` antiguo: reemplazarlo y `Ctrl + Shift + R` |
| `Address already in use` | Otro servidor usa el puerto 8000: `python3 servidor.py 8001` |
| Cambios en la página no se ven | Recargar con `Ctrl + Shift + R` |

## Limitaciones

- Modela un solo proceso con una única tabla de páginas: el thrashing aparece por falta de marcos, no por exceso de procesos (grado de multiprogramación).
- La CPU útil es un modelo didáctico (costo de fallo configurable en `ParamThrashing`), no una medición real.
- El desplazamiento de las direcciones lógicas es sintético cuando la entrada es un archivo de páginas.
- El Swap es conceptual: no se simulan bits de modificación ni escrituras a disco.
- La lectura de `pagemap` es demostrativa y no alimenta la simulación.

## Autoría

Daniela Blandon Garcia .
