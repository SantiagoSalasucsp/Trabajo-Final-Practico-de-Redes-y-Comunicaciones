/*  server.cpp  – entrenamiento distribuido 17-64-32-1
 *  Cabecera ASCII:
 *      e,i,f,s,x → 11 B   ·  M,m → 12 B
 */

#include "tcp.hpp"
#include <arpa/inet.h>
#include <iomanip>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include <cstring>
#include <atomic>
#include <unordered_map>

using tcp::receive_exact;
using tcp::send_all;

/* ───── parámetros fijos ───── */
constexpr int PORT = 9000;
constexpr int HDR_BASE = 11; // 10 dígitos + 1 byte tipo
const std::vector<int> ELEM = {64 * 17, 32 * 64, 1 * 32};

/* ───── globals ───── */
int N_CLIENTS, N_EPOCHS;
std::vector<int> sockets;                             // fd por cliente
std::vector<std::vector<std::vector<double>>> buf(3); // buf[layer][mat]
std::mutex mtx;
std::atomic<bool> abort_all{false};

std::unordered_map<int,int> mapa;

/* Imprime mensaje m con valores reales  */
void imprimir_m(const std::vector<char> &header,
                const std::vector<double> &v,
                std::size_t k = 8) // muestra 8 valores por defecto
{
    std::cout << "\n──────── Mensaje m(valores reales) ────────\n";
    std::cout << "Header ASCII   : "
              << std::string(header.begin(), header.end())
              << "  (" << header.size() << " bytes)\n";
    //std::cout<< std::string(header.begin(), heade.end() );
    std::cout << "Preview valores: ";
    k = std::min(k, v.size());
    for (std::size_t i = 0; i < k; ++i)
        std::cout << std::setw(10) << std::setprecision(4)
                  << std::fixed << v[i] << "  ";
    if (k < v.size())
        std::cout << "…";
    std::cout << "\n────────────────────────\n";
}

/* ─── Cabecera ASCII 11 B  (size + type) ─────────────────────────── */
std::vector<char> make_header(std::size_t bytes, char type)
{
    std::ostringstream oss;
    oss << std::setw(10) << std::setfill('0') << bytes << type; // 10+1
    std::string h = oss.str();                                  // "0000000123e"
    return {h.begin(), h.end()};                                // 11 chars
}

/* ─── Cabecera ASCII 12 B  (size + type + layer) ─────────────────── */
std::vector<char> make_header(std::size_t bytes, char type, char layer)
{
    std::ostringstream oss;
    oss << std::setw(10) << std::setfill('0') << bytes << type << layer; // 10+1+1
    std::string h = oss.str();                                           // "0000001024M0"
    return {h.begin(), h.end()};                                         // 12 chars
}
/* send_raw 11 B  */
void send_raw(int fd, char type, const std::vector<char> &payload = {})
{
    auto hdr = make_header(payload.size(), type);
    /* 2) DEBUG: mostrar lo que se mandará */
    std::cout << "[e] \""                             // abre comillas
              << std::string(hdr.begin(), hdr.end()); // cabecera ASCII
    if (!payload.empty())
        std::cout << std::string(payload.begin(), payload.end()); // payload (ASCII)
    std::cout << "\"  (" << hdr.size() + payload.size()
              << " bytes)\n";
    std::vector<char> pkt(hdr);
    pkt.insert(pkt.end(), payload.begin(), payload.end());
    send_all(fd, pkt);
}

/* send_vecd 12 B (M/m) */
void send_vecd(int fd, char type, char layer,
               const std::vector<double> &v)
{
    auto hdr = make_header(v.size() * 8, type, layer);
    std::vector<char> pkt(hdr);
    pkt.insert(pkt.end(),
               reinterpret_cast<const char *>(v.data()),
               reinterpret_cast<const char *>(v.data()) + v.size() * 8);
    send_all(fd, pkt);
}

/* ───── generar particiones balanceadas ───── */
void generate_partitions(const std::string &csv,
                         int clients,
                         const std::string &prefix = "part")
{
    std::ostringstream cmd;
    cmd << "python3 partition.py --csv " << csv
        << " --clients " << clients
        << " --prefix " << prefix;
    std::cout << "Ejecutando: " << cmd.str() << '\n';
    if (std::system(cmd.str().c_str()) != 0)
        throw std::runtime_error("partition.py falló");
}

/* ───── Habilitar time out───── */
void enable_timeout(int fd, bool on)
{
    timeval tv{on ? 10 : 0, 0}; //Tiempo para timeout
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* ───── Mensaje z (error) para timeout ──── */
void broadcast_abort()
{
    for (int fd : sockets)
    {
        send_raw(fd, 'z');
        close(fd);
    }
    std::cerr << "Timeout: entrenamiento cancelado\n";
    std::_Exit(EXIT_FAILURE); // termina todo el proceso
}


void print_M(int fd, int layer,
                     std::size_t sz_bytes,
                     const std::vector<double>& vec,
                     std::size_t k = 8)          
{
    std::cout << "\n──────── mensaje M recivido ────────\n";
    std::cout << "id cliente   : " << mapa[fd] << '\n';
    std::cout << "Capa          : " << layer << '\n';
    std::cout << "Cabecera      : "
              << std::setw(10) << std::setfill('0') << sz_bytes
              << 'M' << layer << '\n';
    std::cout << "Tamaño (bytes): " << sz_bytes << '\n';
    std::cout << "Preview valores (float64): ";
    k = std::min(k, vec.size());
    for (std::size_t i = 0; i < k; ++i)
        std::cout << std::fixed << std::setprecision(15)
                  << vec[i] << ' ';
    if (k < vec.size()) std::cout << "…";
    std::cout << "\n──────────────────────────\n";
}



/* ───── hilo de cliente ───── */
void client_thread(int fd)
{
    try
    {
        for (int epoch = 0; epoch < N_EPOCHS; ++epoch){      
            for (int lid = 0; lid < 3; ++lid)
            {
                enable_timeout(fd, true);
                /* cabecera 11 B */
                auto h = receive_exact(fd, HDR_BASE);
                enable_timeout(fd, false);

                int sz = std::stoi(std::string(h.data(), 10));
                char typ = h[10];
                if (typ != 'M')
                    throw std::runtime_error("se esperaba 'M'");

                /* byte layer (capa) */
                char layer_char = receive_exact(fd, 1)[0];
                if (layer_char != '0' + lid)
                    throw std::runtime_error("layer fuera de orden");

                /* payload */
                auto data = receive_exact(fd, sz);
                std::vector<double> mat(ELEM[lid]);
                std::memcpy(mat.data(), data.data(), sz);

                print_M(fd,lid,sz,mat);

                /* sincronización */
                bool listo = false;
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    buf[lid].push_back(std::move(mat));
                    //solo un hilo pondra listo = true
                    listo = buf[lid].size() == sockets.size();
                }
                /* Calculo del promedio */
                // solo un hilo realizara el promedio
                if (listo)
                {
                    std::vector<double> avg(ELEM[lid], 0.0);
                    for (auto &m : buf[lid])
                        for (int i = 0; i < ELEM[lid]; ++i)
                            avg[i] += m[i];
                    for (double &v : avg)
                        v /= sockets.size();

                    /* ───── Imprimir matriz m───── */
                    auto hdr = make_header(avg.size() * 8, 'm', '0' + lid);
                    imprimir_m(hdr, avg);
                    /* ───── Enviar matriz m───── */
                    for (int cfd : sockets)
                        send_vecd(cfd, 'm', '0' + lid, avg);

                    std::lock_guard<std::mutex> lk(mtx);
                    buf[lid].clear();
                }
            }
        }
    }
    catch (const std::exception &e)
    {
        // std::cerr << "Hilo fd " << fd << " -> " << e.what() << "\n";
        abort_all = true;
    }
}





/* ───── main ───── */
int main()
{
    std::cout << "Número de clientes : ";
    std::cin >> N_CLIENTS;
    std::cout << "Número de épocas   : ";
    std::cin >> N_EPOCHS;

    /* 1)  generar CSVs balanceados antes de abrir sockets */
    generate_partitions("diabetes_data.csv", N_CLIENTS);

    /* 2)  servidor TCP */
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    a.sin_addr.s_addr = INADDR_ANY;
    bind(srv, (sockaddr *)&a, sizeof(a));
    listen(srv, N_CLIENTS);
    std::cout << "Escuchando en " << PORT << '\n';

    /* 3)  aceptar conexiones */
    for (int i = 0; i < N_CLIENTS; ++i)
    {
        int fd = accept(srv, nullptr, nullptr);

        /* ecibir cabecera 11 B */
        auto h = receive_exact(fd, HDR_BASE); // 11 bytes

        /* convertir a string y mostrar */
        std::string hdr_str(h.begin(), h.end());
        std::cout << "[r] \"" << hdr_str // imprime tal cual
                  << "\"  (" << hdr_str.size() << " bytes)\n";

        /* validar que sea mensaje I */
        int sz = std::stoi(hdr_str.substr(0, 10)); // 0000000000
        char typ = hdr_str[10];                    // I
        if (typ != 'I' || sz != 0)
            throw std::runtime_error("se esperaba \"0000000000I\"");

        std::cout << "Cliente nuevo solicita ID, ID a enviar: " << i << '\n';
        /* responder con el id */
        std::string s_id = std::to_string(i); // "0", "1", …
        send_raw(fd, 'i', std::vector<char>(s_id.begin(), s_id.end()));
        
        mapa[fd]=i;
        sockets.push_back(fd);
    }

    /* 4) SETUP -------------------------------------------------- */
    for (int idx = 0; idx < N_CLIENTS; ++idx)
    {
        int fd = sockets[idx];

        /* epocas (e) */
        std::string s_epochs = std::to_string(N_EPOCHS);
        send_raw(fd, 'e', std::vector<char>(s_epochs.begin(), s_epochs.end()));

        /* filename (f) */
        std::string fname = "part" + std::to_string(idx) + ".csv";
        send_raw(fd, 'f', std::vector<char>(fname.begin(), fname.end()));

        /* start (s) */
        send_raw(fd, 's'); 
    }

    /* 5) hilos del cliente */
    std::vector<std::thread> th;
    for (int fd : sockets)
        th.emplace_back(client_thread, fd);
    for (auto &t : th)
        t.join();
    if (abort_all) //time out
        broadcast_abort();

    /* 6)  fin */
    for (int fd : sockets)
    {
        send_raw(fd, 'x');
        close(fd);
    }
    std::cout << "Fin del entrenamiento\n";
    return 0;
}