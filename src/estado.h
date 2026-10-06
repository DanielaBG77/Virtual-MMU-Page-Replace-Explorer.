#pragma once
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "mmu.h"

// Foto del simulador justo después de procesar UNA petición. Es una COPIA:
// el consumidor nunca toca la MMU, solo recibe fotos (así se evitan condiciones de carrera).
struct Instantanea {
    size_t paso = 0, total = 0;
    std::string algoritmo;
    int numMarcos = 0;
    size_t tamTLB = 0;
    Traduccion tr{};
    std::vector<int> marcos;
    std::vector<std::pair<int, int>> tlb;
    Metricas met;
};

inline std::string aJson(const Instantanea& s, const std::deque<Instantanea>& historial) {
    std::ostringstream o;
    o << "{\n"
      << "  \"algoritmo\": \"" << s.algoritmo << "\",\n"
      << "  \"marcos\": " << s.numMarcos << ",\n"
      << "  \"tamTLB\": " << s.tamTLB << ",\n"
      << "  \"paso\": " << s.paso << ",\n"
      << "  \"total\": " << s.total << ",\n"
      << "  \"terminado\": " << (s.paso == s.total ? "true" : "false") << ",\n"
      << "  \"metricas\": {\"accesos\": " << s.met.accesos << ", \"tlbHits\": " << s.met.tlbHits
      << ", \"tlbMisses\": " << s.met.tlbMisses << ", \"pageFaults\": " << s.met.pageFaults << "},\n"
      << "  \"marcosEstado\": [";
    for (size_t i = 0; i < s.marcos.size(); i++) o << (i ? ", " : "") << s.marcos[i];
    o << "],\n  \"tlbEstado\": [";
    for (size_t i = 0; i < s.tlb.size(); i++)
        o << (i ? ", " : "") << "{\"pagina\": " << s.tlb[i].first << ", \"marco\": " << s.tlb[i].second << "}";
    o << "],\n  \"historial\": [";
    for (size_t i = 0; i < historial.size(); i++) {
        const Traduccion& t = historial[i].tr;
        o << (i ? ",\n    " : "\n    ") << "{\"paso\": " << historial[i].paso
          << ", \"pagina\": " << t.pagina << ", \"tlbHit\": " << (t.tlbHit ? "true" : "false")
          << ", \"fallo\": " << (t.fallo ? "true" : "false") << ", \"marco\": " << t.marco
          << ", \"dirLogica\": " << t.dirLogica << ", \"dirFisica\": " << t.dirFisica
          << ", \"victima\": " << t.paginaVictima << "}";
    }
    o << "\n  ]\n}\n";
    return o.str();
}