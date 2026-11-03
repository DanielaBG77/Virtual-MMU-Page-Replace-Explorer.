#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "mmu.h"
#include "pagemap_reader.h"
#include "thrashing.h"
#include "vivo.h"

std::vector<int> leerSecuencia(const std::string& ruta) {
    std::vector<int> secuencia;
    std::ifstream archivo(ruta);
    if (!archivo.is_open()) {
        std::cerr << "Error: no se pudo abrir " << ruta << std::endl;
        return secuencia;
    }
    int pagina;
    while (archivo >> pagina) secuencia.push_back(pagina);
    return secuencia;
}

// Convierte el número de página en una dirección lógica (con un desplazamiento de ejemplo).
long long aDireccionLogica(int pagina, size_t i) {
    return static_cast<long long>(pagina) * MMU::tamPagina() + static_cast<long long>((i * 37) % MMU::tamPagina());
}

// Corre una simulación completa. Si 'thrOut' no es nulo, devuelve ahí el resultado final del detector de thrashing.
Metricas simular(const std::string& alg, int marcos, size_t tamTLB,
                 const std::vector<int>& secuencia, bool detalle,
                 const ParamThrashing& pt, InfoThrashing* thrOut = nullptr) {
    MMU mmu(marcos, tamTLB, crearPolitica(alg, marcos, secuencia));
    DetectorThrashing detector(marcos, pt);
    InfoThrashing thr;
    for (size_t i = 0; i < secuencia.size(); i++) {
        Traduccion r = mmu.acceder(aDireccionLogica(secuencia[i], i), i);
        thr = detector.registrar(r.pagina, r.fallo);  // el detector ve cada acceso y si fue fallo
        if (detalle) {
            std::cout << "Dir logica " << std::setw(6) << r.dirLogica
                      << " -> pag " << r.pagina << ", desp " << std::setw(4) << r.desplazamiento
                      << " | TLB " << (r.tlbHit ? "HIT  " : "MISS ")
                      << "| " << (r.fallo ? "PAGE FAULT" : "en RAM    ")
                      << " | marco " << r.marco
                      << " | dir fisica " << std::setw(6) << r.dirFisica;
            if (r.paginaVictima >= 0) std::cout << " | sale pag " << r.paginaVictima;
            std::cout << "\n   Marcos: [";
            const auto& m = mmu.estadoMarcos();
            for (size_t k = 0; k < m.size(); k++) {
                if (m[k] < 0) std::cout << "-"; else std::cout << m[k];
                if (k + 1 < m.size()) std::cout << ", ";
            }
            std::cout << "]\n";
            // Línea del detector: estado, conjunto de trabajo contra marcos, tasa de fallos y CPU útil
            std::cout << "   Thrashing: " << std::setw(9) << nombreEstadoThr(thr.estado)
                      << " | WSS " << thr.wss << (thr.wss > marcos ? " > " : " <= ") << marcos << " marcos"
                      << " | fallos en ventana " << thr.fallosVentana << "/" << thr.accesosVentana
                      << " (" << std::fixed << std::setprecision(0) << thr.tasaVentana * 100 << "%)"
                      << " | CPU util " << thr.cpuVentana * 100 << "%" << std::defaultfloat << "\n";
            if (thr.entra) std::cout << "   >>> ENTRA EN THRASHING: la RAM no alcanza para el conjunto de trabajo\n";
            if (thr.sale)  std::cout << "   <<< SALE DE THRASHING: el conjunto de trabajo vuelve a caber\n";
        }
    }
    if (thrOut) *thrOut = thr;
    return mmu.metricas();
}

int main(int argc, char* argv[]) {
    std::string ruta = (argc > 1) ? argv[1] : "pruebas/secuencia.txt";
    int marcos = (argc > 2) ? std::stoi(argv[2]) : 3;
    std::string modo = (argc > 3) ? argv[3] : "todos";
    // En modo "vivo" el 4.º argumento es el algoritmo, no el tamaño del TLB
    size_t tamTLB = (argc > 4 && modo != "vivo") ? static_cast<size_t>(std::stoi(argv[4])) : 4;

    if (modo == "pagemap") return demoPagemap();  // no necesita archivo de secuencia

    // Ventana Δ del detector de thrashing: 5.º argumento (en modo "vivo" es el 6.º, tras algoritmo y pausa)
    ParamThrashing pt;
    const int posVentana = (modo == "vivo") ? 6 : 5;
    if (argc > posVentana) {
        int v = std::stoi(argv[posVentana]);
        if (v > 0) pt.ventana = static_cast<size_t>(v);
    }

    if (marcos <= 0) { std::cerr << "Error: marcos debe ser > 0\n"; return 1; }
    std::vector<int> secuencia = leerSecuencia(ruta);
    if (secuencia.empty()) { std::cerr << "Error: la secuencia esta vacia\n"; return 1; }

    const std::vector<std::string> algoritmos = {"fifo", "lru", "reloj", "optimo"};

    if (modo == "vivo") {  // simulacion con 2 hilos que publica dashboard/estado.json
        std::string alg = (argc > 4) ? argv[4] : "lru";
        int retrasoMs = (argc > 5) ? std::stoi(argv[5]) : 500;
        if (!crearPolitica(alg, marcos, secuencia)) {
            std::cerr << "Error: algoritmo invalido (fifo, lru, reloj, optimo)\n";
            return 1;
        }
        return ejecutarVivo(secuencia, alg, marcos, 4, retrasoMs, "dashboard/estado.json", pt);
    }

    if (modo == "belady") {  // Page Faults vs. cantidad de marcos (datos de la gráfica)
        std::cout << "Page Faults segun marcos asignados\n";
        std::cout << std::setw(8) << "Marcos";
        for (const auto& a : algoritmos) std::cout << std::setw(9) << a;
        std::cout << "\n";
        std::vector<std::vector<int>> datos(algoritmos.size());
        for (int n = 1; n <= 7; n++) {
            std::cout << std::setw(8) << n;
            for (size_t a = 0; a < algoritmos.size(); a++) {
                int fallos = simular(algoritmos[a], n, tamTLB, secuencia, false, pt).pageFaults;
                datos[a].push_back(fallos);
                std::cout << std::setw(9) << fallos;
            }
            std::cout << "\n";
        }
        // Exporta los datos para la gráfica del dashboard
        std::ostringstream js;
        js << "{\"marcos\": [1, 2, 3, 4, 5, 6, 7], \"series\": {";
        for (size_t a = 0; a < algoritmos.size(); a++) {
            js << (a ? ", " : "") << "\"" << algoritmos[a] << "\": [";
            for (size_t k = 0; k < datos[a].size(); k++) js << (k ? ", " : "") << datos[a][k];
            js << "]";
        }
        js << "}}\n";
        std::error_code ec;
        std::filesystem::create_directories("dashboard", ec);
        if (!escribirAtomico("dashboard/belady.json", js.str()))
            std::cerr << "Aviso: no se pudo escribir dashboard/belady.json\n";
        return 0;
    }

    if (modo == "thrashing") {  // CPU util y % de pasos en thrashing segun marcos (datos de la grafica)
        std::cout << "Thrashing segun marcos asignados (ventana " << pt.ventana << ", umbral "
                  << static_cast<int>(pt.umbral * 100) << "%, costo de fallo " << pt.costoFallo << ")\n";
        struct Fila { int fallos, pasosThr, episodios; double cpu; };
        std::vector<std::vector<Fila>> datos(algoritmos.size());
        for (size_t a = 0; a < algoritmos.size(); a++) {
            std::cout << "\n" << algoritmos[a] << "\n" << std::setw(8) << "Marcos" << std::setw(9) << "Fallos"
                      << std::setw(9) << "CPU %" << std::setw(16) << "Pasos thrashing" << std::setw(11) << "Episodios\n";
            for (int n = 1; n <= 7; n++) {
                InfoThrashing t;
                Metricas m = simular(algoritmos[a], n, tamTLB, secuencia, false, pt, &t);
                datos[a].push_back({m.pageFaults, t.pasosThrashing, t.episodios, t.cpuTotal * 100});
                std::cout << std::setw(8) << n << std::setw(9) << m.pageFaults << std::setw(9) << std::fixed
                          << std::setprecision(1) << t.cpuTotal * 100 << std::defaultfloat
                          << std::setw(10) << t.pasosThrashing << "/" << std::left << std::setw(5) << secuencia.size()
                          << std::right << std::setw(6) << t.episodios << "\n";
            }
        }
        std::ostringstream js;
        js << std::fixed << std::setprecision(1);
        js << "{\"marcos\": [1, 2, 3, 4, 5, 6, 7], \"ventana\": " << pt.ventana << ", \"umbral\": " << pt.umbral
           << ", \"costoFallo\": " << pt.costoFallo << ", \"pasos\": " << secuencia.size() << ", \"series\": {";
        for (size_t a = 0; a < algoritmos.size(); a++) {
            js << (a ? ", " : "") << "\"" << algoritmos[a] << "\": {\"fallos\": [";
            for (size_t k = 0; k < datos[a].size(); k++) js << (k ? ", " : "") << datos[a][k].fallos;
            js << "], \"cpu\": [";
            for (size_t k = 0; k < datos[a].size(); k++) js << (k ? ", " : "") << datos[a][k].cpu;
            js << "], \"pasosThr\": [";
            for (size_t k = 0; k < datos[a].size(); k++) js << (k ? ", " : "") << datos[a][k].pasosThr;
            js << "], \"episodios\": [";
            for (size_t k = 0; k < datos[a].size(); k++) js << (k ? ", " : "") << datos[a][k].episodios;
            js << "]}";
        }
        js << "}}\n";
        std::error_code ec;
        std::filesystem::create_directories("dashboard", ec);
        if (!escribirAtomico("dashboard/thrashing.json", js.str()))
            std::cerr << "Aviso: no se pudo escribir dashboard/thrashing.json\n";
        return 0;
    }

    if (modo == "todos") {
        std::cout << "Resumen con " << marcos << " marcos y TLB de " << tamTLB << " entradas (ventana de thrashing " << pt.ventana << ")\n";
        std::cout << std::setw(9) << "Algoritmo" << std::setw(12) << "PageFaults"
                  << std::setw(10) << "TLB hits" << std::setw(11) << "TLB miss"
                  << std::setw(11) << "Thrashing" << std::setw(11) << "Episodios" << std::setw(8) << "CPU %" << "\n";
        for (const auto& a : algoritmos) {
            InfoThrashing t;
            Metricas m = simular(a, marcos, tamTLB, secuencia, false, pt, &t);
            std::cout << std::setw(9) << a << std::setw(12) << m.pageFaults
                      << std::setw(10) << m.tlbHits << std::setw(11) << m.tlbMisses
                      << std::setw(8) << t.pasosThrashing << "/" << std::left << std::setw(2) << secuencia.size()
                      << std::right << std::setw(9) << t.episodios
                      << std::setw(8) << std::fixed << std::setprecision(1) << t.cpuTotal * 100 << std::defaultfloat << "\n";
        }
        return 0;
    }

    if (!crearPolitica(modo, marcos, secuencia)) {
        std::cerr << "Error: use fifo, lru, reloj, optimo, todos, belady, thrashing, pagemap o vivo\n";
        return 1;
    }
    std::cout << "=== " << modo << " con " << marcos << " marcos, TLB de " << tamTLB << " ===\n";
    InfoThrashing t;
    Metricas m = simular(modo, marcos, tamTLB, secuencia, true, pt, &t);
    std::cout << "\nAccesos: " << m.accesos << " | Page Faults: " << m.pageFaults
              << " | TLB hits: " << m.tlbHits << " | TLB misses: " << m.tlbMisses << "\n";
    std::cout << "Thrashing: " << t.pasosThrashing << " de " << m.accesos << " pasos en THRASHING"
              << " | episodios: " << t.episodios
              << " | CPU util total: " << std::fixed << std::setprecision(1) << t.cpuTotal * 100 << "%"
              << std::defaultfloat << "\n";
    return 0;
}
