#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "mmu.h"
#include "pagemap_reader.h"
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

Metricas simular(const std::string& alg, int marcos, size_t tamTLB,
                 const std::vector<int>& secuencia, bool detalle) {
    MMU mmu(marcos, tamTLB, crearPolitica(alg, marcos, secuencia));
    for (size_t i = 0; i < secuencia.size(); i++) {
        Traduccion r = mmu.acceder(aDireccionLogica(secuencia[i], i), i);
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
        }
    }
    return mmu.metricas();
}

int main(int argc, char* argv[]) {
    std::string ruta = (argc > 1) ? argv[1] : "pruebas/secuencia.txt";
    int marcos = (argc > 2) ? std::stoi(argv[2]) : 3;
    std::string modo = (argc > 3) ? argv[3] : "todos";
    // En modo "vivo" el 4.º argumento es el algoritmo, no el tamaño del TLB
    size_t tamTLB = (argc > 4 && modo != "vivo") ? static_cast<size_t>(std::stoi(argv[4])) : 4;

    if (modo == "pagemap") return demoPagemap();  // no necesita archivo de secuencia

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
        return ejecutarVivo(secuencia, alg, marcos, 4, retrasoMs, "dashboard/estado.json");
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
                int fallos = simular(algoritmos[a], n, tamTLB, secuencia, false).pageFaults;
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

    if (modo == "todos") {
        std::cout << "Resumen con " << marcos << " marcos y TLB de " << tamTLB << " entradas\n";
        std::cout << std::setw(9) << "Algoritmo" << std::setw(12) << "PageFaults"
                  << std::setw(10) << "TLB hits" << std::setw(11) << "TLB miss" << "\n";
        for (const auto& a : algoritmos) {
            Metricas m = simular(a, marcos, tamTLB, secuencia, false);
            std::cout << std::setw(9) << a << std::setw(12) << m.pageFaults
                      << std::setw(10) << m.tlbHits << std::setw(11) << m.tlbMisses << "\n";
        }
        return 0;
    }

    if (!crearPolitica(modo, marcos, secuencia)) {
        std::cerr << "Error: use fifo, lru, reloj, optimo, todos, belady, pagemap o vivo\n";
        return 1;
    }
    std::cout << "=== " << modo << " con " << marcos << " marcos, TLB de " << tamTLB << " ===\n";
    Metricas m = simular(modo, marcos, tamTLB, secuencia, true);
    std::cout << "\nAccesos: " << m.accesos << " | Page Faults: " << m.pageFaults
              << " | TLB hits: " << m.tlbHits << " | TLB misses: " << m.tlbMisses << "\n";
    return 0;
}