#pragma once
#include <vector>
#include <cstddef>

namespace tcp {

// Lee exactamente n bytes del descriptor fd; lanza std::runtime_error si la conexión se cierra
std::vector<char> receive_exact(int fd, std::size_t n);

// Envía todo el buffer (data) por el descriptor fd
void send_all(int fd, const std::vector<char>& data);

std::vector<char> make_header(std::size_t bytes,char type, char layer); 
std::vector<char> make_header(std::size_t bytes, char type);

}  // namespace tcp
