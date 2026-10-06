#pragma once
#include <deque>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>
#include "politicas.h"

struct Metricas {
    int accesos = 0, tlbHits = 0, tlbMisses = 0, pageFaults = 0;
};

// Resultado de traducir UNA dirección (sirve para imprimir y luego para el dashboard).
struct Traduccion {
    long long dirLogica, dirFisica;
    int pagina, desplazamiento, marco;
    bool tlbHit, fallo;
    int paginaVictima;  // -1 si no hubo reemplazo
};

class MMU {
    static constexpr int TAM_PAGINA = 4096;
    int numMarcos;
    size_t capacidadTLB;
    std::unique_ptr<Politica> politica;
    std::vector<int> marcos;                    // marcos[m] = página cargada (-1 = libre)
    std::unordered_map<int, int> tablaPaginas;  // página -> marco
    std::deque<std::pair<int, int>> tlb;        // (página, marco), reemplazo FIFO
    Metricas met;

    int buscarEnTLB(int pagina) const {
        for (const auto& e : tlb) if (e.first == pagina) return e.second;
        return -1;
    }
    void insertarEnTLB(int pagina, int marco) {
        if (capacidadTLB == 0) return;
        if (tlb.size() == capacidadTLB) tlb.pop_front();
        tlb.emplace_back(pagina, marco);
    }
    void invalidarEnTLB(int pagina) {
        for (auto it = tlb.begin(); it != tlb.end(); ++it)
            if (it->first == pagina) { tlb.erase(it); return; }
    }

public:
    MMU(int marcosRAM, size_t tamTLB, std::unique_ptr<Politica> p)
        : numMarcos(marcosRAM), capacidadTLB(tamTLB), politica(std::move(p)),
          marcos(marcosRAM, -1) {}

    static int tamPagina() { return TAM_PAGINA; }
    const Metricas& metricas() const { return met; }
    const std::vector<int>& estadoMarcos() const { return marcos; }
    const std::deque<std::pair<int, int>>& estadoTLB() const { return tlb; }

    // 't' = índice de la petición (lo necesita el algoritmo Óptimo).
    Traduccion acceder(long long dirLogica, size_t t) {
        Traduccion r{};
        r.dirLogica = dirLogica;
        r.pagina = static_cast<int>(dirLogica / TAM_PAGINA);
        r.desplazamiento = static_cast<int>(dirLogica % TAM_PAGINA);
        r.paginaVictima = -1;
        met.accesos++;

        int marco = buscarEnTLB(r.pagina);
        if (marco >= 0) {                       // 1) TLB HIT
            r.tlbHit = true;
            met.tlbHits++;
            politica->alAcceder(marco, r.pagina);
        } else {                                // 2) TLB MISS -> tabla de páginas
            met.tlbMisses++;
            auto it = tablaPaginas.find(r.pagina);
            if (it != tablaPaginas.end()) {     // en RAM, solo faltaba la traducción
                marco = it->second;
                politica->alAcceder(marco, r.pagina);
            } else {                            // 3) PAGE FAULT
                r.fallo = true;
                met.pageFaults++;
                marco = -1;
                for (int m = 0; m < numMarcos; m++)
                    if (marcos[m] == -1) { marco = m; break; }
                if (marco == -1) {              // RAM llena -> algoritmo de reemplazo
                    marco = politica->elegirVictima(marcos, t);
                    r.paginaVictima = marcos[marco];
                    tablaPaginas.erase(r.paginaVictima);
                    invalidarEnTLB(r.paginaVictima);  // la traducción vieja ya no vale
                }
                marcos[marco] = r.pagina;
                tablaPaginas[r.pagina] = marco;
                politica->alCargar(marco, r.pagina);
            }
            insertarEnTLB(r.pagina, marco);
        }
        r.marco = marco;
        r.dirFisica = static_cast<long long>(marco) * TAM_PAGINA + r.desplazamiento;
        return r;
    }
};