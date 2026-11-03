#pragma once
#include <deque>
#include <unordered_set>
#include <utility>

// ---------------------------------------------------------------------------
// THRASHING (hiperpaginación)
//
// Ocurre cuando el proceso necesita más páginas "a la vez" de las que caben en
// RAM: casi cada acceso provoca un page fault, y el sistema pasa más tiempo
// moviendo páginas entre RAM y disco (swap) que ejecutando instrucciones útiles.
//
// Cómo se detecta aquí (modelo del CONJUNTO DE TRABAJO de Denning):
//   - Se mira una ventana con los últimos Δ accesos.
//   - WSS  = cuántas páginas DISTINTAS se usaron en esa ventana (conjunto de trabajo).
//   - tasa = page faults dentro de la ventana / accesos de la ventana.
//   - THRASHING  si  WSS > marcos  Y  tasa >= umbral   (la demanda supera a la RAM
//                                                       y además se está fallando mucho)
//   - ALERTA     si  WSS > marcos  pero la tasa todavía es baja (va a empezar a fallar)
//   - NORMAL     en cualquier otro caso (incluye la carga inicial: fallos "fríos"
//                con WSS <= marcos no son thrashing)
//
// Modelo de tiempo (didáctico, en unidades arbitrarias):
//   - cada acceso suma 'costoAcceso' de trabajo ÚTIL
//   - cada page fault suma 'costoFallo' de tiempo perdido en swap (disco)
//   - CPU útil = trabajo útil / (trabajo útil + tiempo de swap)
// ---------------------------------------------------------------------------

struct ParamThrashing {
    size_t ventana = 6;    // Δ: cuántos accesos recientes se analizan
    double umbral = 0.5;   // tasa de fallos (0..1) desde la cual hay thrashing
    int costoAcceso = 1;   // tiempo útil por acceso
    int costoFallo = 10;   // tiempo extra por atender un page fault (swap con el disco)
};

enum class EstadoThr { NORMAL, ALERTA, THRASHING };

inline const char* nombreEstadoThr(EstadoThr e) {
    switch (e) {
        case EstadoThr::ALERTA:    return "ALERTA";
        case EstadoThr::THRASHING: return "THRASHING";
        default:                   return "NORMAL";
    }
}

// Foto de las métricas de thrashing después de procesar UNA petición.
struct InfoThrashing {
    EstadoThr estado = EstadoThr::NORMAL;
    size_t ventana = 0;        // Δ configurada
    double umbral = 0.5;       // umbral configurado
    int accesosVentana = 0;    // accesos que hay ahora en la ventana (<= Δ al inicio)
    int fallosVentana = 0;     // page faults dentro de la ventana
    double tasaVentana = 0;    // fallosVentana / accesosVentana
    int wss = 0;               // conjunto de trabajo: páginas distintas en la ventana
    int marcos = 0;            // marcos de RAM disponibles (para comparar con wss)
    double cpuVentana = 1;     // CPU útil dentro de la ventana (0..1)
    double cpuTotal = 1;       // CPU útil acumulada desde el inicio (0..1)
    int pasosThrashing = 0;    // cuántos pasos llevan en estado THRASHING
    int episodios = 0;         // cuántas veces se ENTRÓ a thrashing
    bool entra = false;        // este paso es el que dispara un nuevo episodio
    bool sale = false;         // este paso es el que termina el episodio
};

class DetectorThrashing {
    int numMarcos;
    ParamThrashing p;
    std::deque<std::pair<int, bool>> ventana;  // (página, ¿hubo fallo?)
    long long tiempoUtil = 0, tiempoSwap = 0;
    int pasosThr = 0, episodios = 0;
    EstadoThr anterior = EstadoThr::NORMAL;

public:
    DetectorThrashing(int marcos, ParamThrashing params) : numMarcos(marcos), p(params) {
        if (p.ventana == 0) p.ventana = 1;
    }

    // Se llama UNA vez por cada acceso de la secuencia, justo después de mmu.acceder().
    InfoThrashing registrar(int pagina, bool fallo) {
        ventana.emplace_back(pagina, fallo);
        if (ventana.size() > p.ventana) ventana.pop_front();
        tiempoUtil += p.costoAcceso;
        if (fallo) tiempoSwap += p.costoFallo;

        std::unordered_set<int> distintas;
        int fallos = 0;
        for (const auto& e : ventana) {
            distintas.insert(e.first);
            if (e.second) fallos++;
        }

        InfoThrashing r;
        r.ventana = p.ventana;
        r.umbral = p.umbral;
        r.marcos = numMarcos;
        r.accesosVentana = static_cast<int>(ventana.size());
        r.fallosVentana = fallos;
        r.tasaVentana = static_cast<double>(fallos) / ventana.size();
        r.wss = static_cast<int>(distintas.size());

        const double utilVentana = static_cast<double>(ventana.size()) * p.costoAcceso;
        r.cpuVentana = utilVentana / (utilVentana + static_cast<double>(fallos) * p.costoFallo);
        r.cpuTotal = static_cast<double>(tiempoUtil) / static_cast<double>(tiempoUtil + tiempoSwap);

        const bool demandaSuperaRAM = r.wss > numMarcos;
        if (demandaSuperaRAM && r.tasaVentana >= p.umbral) r.estado = EstadoThr::THRASHING;
        else if (demandaSuperaRAM)                         r.estado = EstadoThr::ALERTA;
        else                                               r.estado = EstadoThr::NORMAL;

        r.entra = (r.estado == EstadoThr::THRASHING && anterior != EstadoThr::THRASHING);
        r.sale = (r.estado != EstadoThr::THRASHING && anterior == EstadoThr::THRASHING);
        if (r.entra) episodios++;
        if (r.estado == EstadoThr::THRASHING) pasosThr++;
        r.pasosThrashing = pasosThr;
        r.episodios = episodios;
        anterior = r.estado;
        return r;
    }
};
