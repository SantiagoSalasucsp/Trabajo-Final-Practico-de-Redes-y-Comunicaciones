// tcp.cpp  ─ utilidades de E/S y cabecera ASCII
#include "tcp.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tcp {

/* ─────────────────────────────────────────────────────────────
 *  make_header
 *      bytes : tamaño SOLO del payload
 *      type  : 'e','i','f','s','x'  (→  cabecera de 11 bytes)
 *            : 'M','m'              (→  cabecera de 12 bytes, se añade layer)
 *      layer : '\0' cuando no se necesita;  '0','1','2' para M/m
 *  Devuelve un vector<char> con la cabecera ASCII.
 * ──────────────────────────────────────────────────────────── */
std::vector<char> make_header(std::size_t bytes, char type, char layer)
{
    std::ostringstream oss;
    oss << std::setw(10) << std::setfill('0') << bytes << type;
    if (layer != '\0') oss << layer;                // solo para M / m
    std::string h = oss.str();                      // 11 u 12 chars
    return {h.begin(), h.end()};
}

/* 11 B: size + type (e,i,f,s,x,I) */
std::vector<char> make_header(std::size_t bytes, char type)
{
    std::ostringstream oss;
    oss << std::setw(10) << std::setfill('0') << bytes << type;
    std::string h = oss.str();
    return {h.begin(), h.end()};
}

/* ─────────────────────────────────────────────────────────────
 * receive_exact
 *      Lee exactamente n bytes del socket fd
 *      Lanza std::runtime_error si la conexión se cierra antes.
 * ──────────────────────────────────────────────────────────── */
std::vector<char> receive_exact(int fd, std::size_t n)
{
    std::vector<char> buf(n);
    std::size_t got = 0;
    while (got < n) {
        ssize_t r = ::recv(fd, buf.data() + got, n - got, 0);
        if (r <= 0) throw std::runtime_error("recv() failed / closed");
        got += static_cast<std::size_t>(r);
    }
    return buf;
}

/* ─────────────────────────────────────────────────────────────
 * send_all
 *      Envía todo el contenido de data por el descriptor fd
 *      Lanza std::runtime_error si send() devuelve ≤0.
 * ──────────────────────────────────────────────────────────── */
void send_all(int fd, const std::vector<char>& data)
{
    const char* p = data.data();
    std::size_t left = data.size();
    while (left) {
        ssize_t s = ::send(fd, p, left, 0);
        if (s <= 0) throw std::runtime_error("send() failed");
        p    += s;
        left -= static_cast<std::size_t>(s);
    }
}

}  // namespace tcp