#pragma once

// Minimal blocking HTTP/1.1 and WebSocket clients for the server integration tests.

#include <map>
#include <string>
#include <vector>

#include <asio.hpp>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/time.h>

namespace pf::server::test {

struct HttpResponse {
  int status = 0;
  std::map<std::string, std::string> headers;  // lower-case names
  std::string body;

  [[nodiscard]] nlohmann::json json() const { return nlohmann::json::parse(body); }
};

inline void setReceiveTimeout(asio::ip::tcp::socket& socket, int seconds) {
  timeval tv{};
  tv.tv_sec = seconds;
  setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

inline HttpResponse request(std::uint16_t port, const std::string& method, const std::string& path,
                            const std::string& body = "",
                            const std::map<std::string, std::string>& extra_headers = {}) {
  asio::io_context io;
  asio::ip::tcp::socket socket(io);
  socket.connect({asio::ip::make_address("127.0.0.1"), port});
  setReceiveTimeout(socket, 60);
  std::string req = method + " " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n";
  if (!body.empty()) {
    req +=
        "Content-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) + "\r\n";
  }
  for (const auto& [k, v] : extra_headers) req += k + ": " + v + "\r\n";
  req += "\r\n" + body;
  asio::write(socket, asio::buffer(req));

  std::string raw;
  std::array<char, 8192> buf{};
  asio::error_code ec;
  while (true) {
    const std::size_t n = socket.read_some(asio::buffer(buf), ec);
    raw.append(buf.data(), n);
    if (ec) break;
  }
  HttpResponse res;
  const auto header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) return res;
  std::istringstream head(raw.substr(0, header_end));
  std::string line;
  std::getline(head, line);
  res.status = std::stoi(line.substr(9, 3));
  while (std::getline(head, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon);
    for (char& c : name) c = static_cast<char>(std::tolower(c));
    res.headers[name] = line.substr(colon + 2);
  }
  res.body = raw.substr(header_end + 4);
  return res;
}

inline HttpResponse get(std::uint16_t port, const std::string& path) {
  return request(port, "GET", path);
}
inline HttpResponse post(std::uint16_t port, const std::string& path,
                         const nlohmann::json& body = nlohmann::json::object()) {
  return request(port, "POST", path, body.dump());
}

/// Connects to a WebSocket and returns every text message until one satisfies `stop`
/// (or the connection closes / `timeout_s` passes).
template <typename Stop>
std::vector<nlohmann::json> readWebSocket(std::uint16_t port, const std::string& path, Stop&& stop,
                                          int timeout_s = 30) {
  asio::io_context io;
  asio::ip::tcp::socket socket(io);
  socket.connect({asio::ip::make_address("127.0.0.1"), port});
  setReceiveTimeout(socket, timeout_s);
  const std::string req = "GET " + path +
                          " HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                          "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                          "Sec-WebSocket-Version: 13\r\n\r\n";
  asio::write(socket, asio::buffer(req));
  asio::streambuf sb;
  asio::read_until(socket, sb, "\r\n\r\n");
  std::string head(asio::buffers_begin(sb.data()), asio::buffers_end(sb.data()));
  const auto end = head.find("\r\n\r\n");
  std::string pending = head.substr(end + 4);
  if (head.find(" 101 ") == std::string::npos) return {};

  auto read_exact = [&](std::size_t n) {
    while (pending.size() < n) {
      std::array<char, 4096> buf{};
      asio::error_code ec;
      const std::size_t got = socket.read_some(asio::buffer(buf), ec);
      if (ec) throw std::runtime_error("websocket read failed: " + ec.message());
      pending.append(buf.data(), got);
    }
    std::string out = pending.substr(0, n);
    pending.erase(0, n);
    return out;
  };

  std::vector<nlohmann::json> messages;
  try {
    while (true) {
      const std::string h = read_exact(2);
      const auto opcode = static_cast<unsigned char>(h[0]) & 0x0FU;
      std::uint64_t len = static_cast<unsigned char>(h[1]) & 0x7FU;
      if (len == 126) {
        const std::string e = read_exact(2);
        len = (static_cast<std::uint64_t>(static_cast<unsigned char>(e[0])) << 8U) |
              static_cast<unsigned char>(e[1]);
      } else if (len == 127) {
        const std::string e = read_exact(8);
        len = 0;
        for (const char c : e) len = (len << 8U) | static_cast<unsigned char>(c);
      }
      const std::string payload = read_exact(len);
      if (opcode == 0x8) break;  // close
      if (opcode != 0x1) continue;
      messages.push_back(nlohmann::json::parse(payload));
      if (stop(messages.back())) break;
    }
  } catch (const std::exception&) {
    // Timeout or closed connection: the caller checks what arrived.
    return messages;
  }
  return messages;
}

}  // namespace pf::server::test
