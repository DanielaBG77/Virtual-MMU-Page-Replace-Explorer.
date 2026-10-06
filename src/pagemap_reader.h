#pragma once
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

// Envuelve un descriptor de archivo: se cierra solo, aunque haya errores (sin "grifos abiertos").
class ArchivoFd {
    int fd;
public:
    explicit ArchivoFd(const std::string& ruta) : fd(open(ruta.c_str(), O_RDONLY)) {}
    ~ArchivoFd() { if (fd >= 0) close(fd); }
    ArchivoFd(const ArchivoFd&) = delete;
    ArchivoFd& operator=(const ArchivoFd&) = delete;
    bool abierto() const { return fd >= 0; }
    int get() const { return fd; }
};

// Una entrada de /proc/PID/pagemap (8 bytes por página virtual).
struct EntradaPagemap {
    bool presente = false;  // bit 63: la página está en RAM
    bool enSwap = false;    // bit 62: la página está en Swap
    uint64_t marco = 0;     // bits 0-54: número de marco físico (0 si no eres root)
};

// Lee la entrada de la página que contiene 'direccionVirtual'. Devuelve false y llena 'error' si falla.
inline bool leerEntradaPagemap(const std::string& rutaPagemap, uintptr_t direccionVirtual,
                               EntradaPagemap& salida, std::string& error) {
    ArchivoFd archivo(rutaPagemap);
    if (!archivo.abierto()) {
        error = "no se pudo abrir " + rutaPagemap + ": " + std::strerror(errno);
        return false;
    }
    const long tamPagina = sysconf(_SC_PAGESIZE);
    if (tamPagina <= 0) { error = "no se pudo obtener el tamano de pagina"; return false; }

    uint64_t entrada = 0;
    off_t desplazamiento = static_cast<off_t>((direccionVirtual / static_cast<uintptr_t>(tamPagina)) * sizeof(entrada));
    ssize_t leidos = pread(archivo.get(), &entrada, sizeof(entrada), desplazamiento);
    if (leidos != static_cast<ssize_t>(sizeof(entrada))) {
        error = "lectura incompleta de pagemap";
        if (leidos < 0) error += std::string(": ") + std::strerror(errno);
        return false;
    }
    salida.presente = (entrada >> 63) & 1ULL;
    salida.enSwap   = (entrada >> 62) & 1ULL;
    salida.marco    = entrada & ((1ULL << 55) - 1);
    return true;
}

// Demo: pide 4 páginas, lee su estado ANTES y DESPUÉS de tocarlas (paginación por demanda).
inline int demoPagemap() {
    const long tam = sysconf(_SC_PAGESIZE);
    const int N = 4;
    void* region = mmap(nullptr, static_cast<size_t>(N) * tam, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) {
        std::cerr << "Error: mmap fallo: " << std::strerror(errno) << "\n";
        return 1;
    }
    char* base = static_cast<char*>(region);

    auto mostrar = [&](const char* titulo) {
        std::cout << "\n" << titulo << "\n";
        std::cout << std::setw(8) << "Pagina" << std::setw(20) << "Direccion virtual"
                  << std::setw(10) << "En RAM" << std::setw(10) << "En Swap" << "  Marco fisico\n";
        for (int i = 0; i < N; i++) {
            uintptr_t dir = reinterpret_cast<uintptr_t>(base + i * tam);
            EntradaPagemap e;
            std::string error;
            if (!leerEntradaPagemap("/proc/self/pagemap", dir, e, error)) {
                std::cerr << "Error: " << error << "\n";
                munmap(region, static_cast<size_t>(N) * tam);
                return;
            }
            std::cout << std::setw(8) << i << "   0x" << std::hex << std::setw(14) << std::setfill('0') << dir
                      << std::dec << std::setfill(' ')
                      << std::setw(10) << (e.presente ? "SI" : "NO")
                      << std::setw(10) << (e.enSwap ? "SI" : "NO") << "  ";
            if (e.presente && e.marco != 0) std::cout << e.marco;
            else if (e.presente) std::cout << "(oculto: requiere sudo)";
            else std::cout << "-";
            std::cout << "\n";
        }
    };

    std::cout << "Lectura real de /proc/self/pagemap (paginas de " << tam << " bytes)\n";
    mostrar("1) Pedimos 4 paginas pero NO las hemos tocado:");
    base[0 * tam] = 1;      // tocar la página 0 -> el kernel genera un Page Fault real
    base[2 * tam] = 1;      // y la página 2
    mostrar("2) Despues de escribir en las paginas 0 y 2:");
    for (int i = 0; i < N; i++) base[i * tam] = 1;
    mostrar("3) Despues de escribir en las 4 paginas:");

    munmap(region, static_cast<size_t>(N) * tam);  // devolvemos la memoria al kernel
    return 0;
}