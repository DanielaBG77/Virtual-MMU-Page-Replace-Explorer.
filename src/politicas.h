#pragma once
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Interfaz común: cada algoritmo de reemplazo es una pieza intercambiable.
class Politica {
public:
    virtual ~Politica() = default;
    virtual std::string nombre() const = 0;
    // Una página acaba de cargarse en 'marco'.
    virtual void alCargar(int marco, int pagina) = 0;
    // Se accedió (hit) a la página que vive en 'marco'.
    virtual void alAcceder(int marco, int pagina) = 0;
    // RAM llena: devuelve el marco víctima. 'paginas[m]' es la página en el marco m,
    // 't' es el índice de la petición actual en la secuencia.
    virtual int elegirVictima(const std::vector<int>& paginas, size_t t) = 0;
};

// FIFO: cola pura de marcos por orden de llegada.
class PoliticaFIFO : public Politica {
    std::list<int> cola;
public:
    std::string nombre() const override { return "FIFO"; }
    void alCargar(int marco, int) override { cola.push_back(marco); }
    void alAcceder(int, int) override {}  // FIFO ignora el uso
    int elegirVictima(const std::vector<int>&, size_t) override {
        int v = cola.front();
        cola.pop_front();
        return v;
    }
};

// LRU: lista (menos reciente al frente) + mapa de iteradores para O(1).
class PoliticaLRU : public Politica {
    std::list<int> orden;
    std::unordered_map<int, std::list<int>::iterator> pos;
public:
    std::string nombre() const override { return "LRU"; }
    void alCargar(int marco, int) override {
        orden.push_back(marco);
        pos[marco] = std::prev(orden.end());
    }
    void alAcceder(int marco, int) override {
        orden.erase(pos[marco]);
        orden.push_back(marco);
        pos[marco] = std::prev(orden.end());
    }
    int elegirVictima(const std::vector<int>&, size_t) override {
        int v = orden.front();
        orden.pop_front();
        pos.erase(v);
        return v;
    }
};

// Reloj (segunda oportunidad): bit de referencia + puntero circular.
class PoliticaReloj : public Politica {
    std::vector<bool> bitRef;
    size_t puntero = 0;
public:
    explicit PoliticaReloj(int numMarcos) : bitRef(numMarcos, false) {}
    std::string nombre() const override { return "Reloj"; }
    void alCargar(int marco, int) override { bitRef[marco] = true; }
    void alAcceder(int marco, int) override { bitRef[marco] = true; }
    int elegirVictima(const std::vector<int>&, size_t) override {
        while (true) {
            if (!bitRef[puntero]) {
                int v = static_cast<int>(puntero);
                puntero = (puntero + 1) % bitRef.size();
                return v;
            }
            bitRef[puntero] = false;  // segunda oportunidad
            puntero = (puntero + 1) % bitRef.size();
        }
    }
};

// Óptimo: mira el futuro de la secuencia (solo sirve como métrica de control).
class PoliticaOptimo : public Politica {
    const std::vector<int>& futuro;
public:
    explicit PoliticaOptimo(const std::vector<int>& secuencia) : futuro(secuencia) {}
    std::string nombre() const override { return "Optimo"; }
    void alCargar(int, int) override {}
    void alAcceder(int, int) override {}
    int elegirVictima(const std::vector<int>& paginas, size_t t) override {
        int victima = 0;
        size_t masLejano = 0;
        for (size_t m = 0; m < paginas.size(); m++) {
            size_t proximo = futuro.size();  // "nunca más se usa"
            for (size_t i = t + 1; i < futuro.size(); i++) {
                if (futuro[i] == paginas[m]) { proximo = i; break; }
            }
            if (proximo == futuro.size()) return static_cast<int>(m);
            if (proximo > masLejano) { masLejano = proximo; victima = static_cast<int>(m); }
        }
        return victima;
    }
};

inline std::unique_ptr<Politica> crearPolitica(const std::string& nombre, int numMarcos,
                                               const std::vector<int>& secuencia) {
    if (nombre == "fifo")   return std::make_unique<PoliticaFIFO>();
    if (nombre == "lru")    return std::make_unique<PoliticaLRU>();
    if (nombre == "reloj")  return std::make_unique<PoliticaReloj>(numMarcos);
    if (nombre == "optimo") return std::make_unique<PoliticaOptimo>(secuencia);
    return nullptr;
}