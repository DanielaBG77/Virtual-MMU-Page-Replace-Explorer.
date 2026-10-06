#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

// Buffer acotado (productor-consumidor): mismo patrón del material de clase,
// con std::mutex (exclusión mutua) y dos variables de condición (no_lleno / no_vacio).
template <typename T>
class ColaAcotada {
    std::queue<T> cola;
    size_t capacidad;
    bool cerrada = false;
    std::mutex mtx;
    std::condition_variable noLlena, noVacia;

public:
    explicit ColaAcotada(size_t cap) : capacidad(cap) {}

    // Productor: espera si la cola está llena.
    void push(T valor) {
        std::unique_lock<std::mutex> lk(mtx);
        noLlena.wait(lk, [&] { return cola.size() < capacidad; });
        cola.push(std::move(valor));
        noVacia.notify_one();
    }

    // Consumidor: espera si está vacía. Devuelve false cuando ya no habrá más datos.
    bool pop(T& salida) {
        std::unique_lock<std::mutex> lk(mtx);
        noVacia.wait(lk, [&] { return !cola.empty() || cerrada; });
        if (cola.empty()) return false;  // cerrada y vacía: fin
        salida = std::move(cola.front());
        cola.pop();
        noLlena.notify_one();
        return true;
    }

    // El productor avisa que terminó (para que el consumidor no espere para siempre).
    void cerrar() {
        { std::lock_guard<std::mutex> lk(mtx); cerrada = true; }
        noVacia.notify_all();
    }
};