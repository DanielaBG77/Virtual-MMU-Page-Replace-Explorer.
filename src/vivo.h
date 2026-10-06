#pragma once
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "cola.h"
#include "estado.h"

// Cierra la cola al salir del hilo productor, incluso si ocurre una excepción
// (si no, el consumidor se quedaría esperando para siempre).
struct CierraAlSalir {
    ColaAcotada<Instantanea>& cola;
    ~CierraAlSalir() { cola.cerrar(); }
};

// Escribe el archivo en forma atómica: primero un temporal y luego renombrar,
// para que el dashboard nunca lea un JSON a medias.
inline bool escribirAtomico(const std::string& ruta, const std::string& contenido) {
    const std::string tmp = ruta + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f.is_open()) return false;
        f << contenido;
        if (!f.good()) return false;
    }
    return std::rename(tmp.c_str(), ruta.c_str()) == 0;
}

inline int ejecutarVivo(const std::vector<int>& secuencia, const std::string& algoritmo,
                        int marcos, size_t tamTLB, int retrasoMs, const std::string& rutaJson) {
    std::error_code ec;
    std::filesystem::path carpeta = std::filesystem::path(rutaJson).parent_path();
    if (!carpeta.empty()) std::filesystem::create_directories(carpeta, ec);

    ColaAcotada<Instantanea> cola(8);  // buffer de 8 fotos
    int resultado = 0;

    // PRODUCTOR: único dueño de la MMU. Procesa peticiones y publica fotos.
    std::thread productor([&] {
        CierraAlSalir cierre{cola};
        MMU mmu(marcos, tamTLB, crearPolitica(algoritmo, marcos, secuencia));
        for (size_t i = 0; i < secuencia.size(); i++) {
            long long dir = static_cast<long long>(secuencia[i]) * MMU::tamPagina() +
                            static_cast<long long>((i * 37) % MMU::tamPagina());
            Instantanea s;
            s.tr = mmu.acceder(dir, i);
            s.paso = i + 1;
            s.total = secuencia.size();
            s.algoritmo = algoritmo;
            s.numMarcos = marcos;
            s.tamTLB = tamTLB;
            s.marcos = mmu.estadoMarcos();
            s.tlb.assign(mmu.estadoTLB().begin(), mmu.estadoTLB().end());
            s.met = mmu.metricas();
            cola.push(std::move(s));
            if (retrasoMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(retrasoMs));
        }
    });

    // CONSUMIDOR: recibe fotos y publica estado.json (lo que leerá el dashboard).
    std::thread consumidor([&] {
        std::deque<Instantanea> historial;
        Instantanea s;
        bool avisoError = false;
        while (cola.pop(s)) {
            historial.push_back(s);
            if (historial.size() > 12) historial.pop_front();
            if (!escribirAtomico(rutaJson, aJson(s, historial)) && !avisoError) {
                std::cerr << "Aviso: no se pudo escribir " << rutaJson << "\n";
                avisoError = true;
                resultado = 1;
            }
            std::cout << "Paso " << s.paso << "/" << s.total << " | pagina " << s.tr.pagina
                      << " | TLB " << (s.tr.tlbHit ? "HIT " : "MISS") << " | "
                      << (s.tr.fallo ? "PAGE FAULT" : "en RAM") << " | fallos acumulados: "
                      << s.met.pageFaults << std::endl;
        }
    });

    productor.join();
    consumidor.join();
    std::cout << "\nListo. Estado final en " << rutaJson << std::endl;
    return resultado;
}