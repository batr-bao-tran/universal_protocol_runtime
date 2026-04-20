#include <benchmark/benchmark.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "universal_protocol_runtime/adapters/frame_channel.hpp"
#include "universal_protocol_runtime/adapters/io_uring_reactor.hpp"
#include "universal_protocol_runtime/adapters/local_shm_ring_transport.hpp"
#include "universal_protocol_runtime/adapters/pluggable_stream_engine.hpp"
#include "universal_protocol_runtime/adapters/tcp_stream_transport.hpp"
#include "universal_protocol_runtime/adapters/unix_socket_transport.hpp"

namespace upr = universal_protocol_runtime;

namespace {

constexpr std::array<int, 5> kPayloadSizes = {64, 256, 1024, 4096, 65536};

std::vector<std::byte> make_payload(size_t size, uint8_t seed) {
  std::vector<std::byte> payload(size);
  uint32_t state = (seed * 2654435761U) + 1U;
  for (std::byte& byte : payload) {
    state = (state * 1664525U) + 1013904223U;
    byte = static_cast<std::byte>(state & 0xFFU);
  }
  return payload;
}

upr::StatusOr<std::pair<upr::PluggableStreamTransport, upr::PluggableStreamTransport>> make_io_uring_unix_pair() {
  if (!upr::IoUringReactor::is_supported()) {
    return upr::not_found(std::string(upr::IoUringReactor::reason()));
  }
  std::array<int, 2> sockets = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()) < 0) {
    return upr::io_error("socketpair(AF_UNIX) failed");
  }
  auto left = upr::IoUringStreamEngine::create(sockets[0], "uring://unix-left", "uring://unix-right");
  if (!left.ok()) {
    ::close(sockets[0]);
    ::close(sockets[1]);
    return left.status();
  }
  auto right = upr::IoUringStreamEngine::create(sockets[1], "uring://unix-right", "uring://unix-left");
  if (!right.ok()) {
    return right.status();
  }
  return std::make_pair(upr::PluggableStreamTransport(std::move(left.value())),
                        upr::PluggableStreamTransport(std::move(right.value())));
}

upr::StatusOr<std::pair<upr::PluggableStreamTransport, upr::PluggableStreamTransport>> make_io_uring_tcp_pair() {
  if (!upr::IoUringReactor::is_supported()) {
    return upr::not_found(std::string(upr::IoUringReactor::reason()));
  }
  auto listener = upr::TcpListener::bind_loopback(0);
  if (!listener.ok()) {
    return listener.status();
  }
  auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", listener.value().port());
  if (!client.ok()) {
    return client.status();
  }
  auto accepted = listener.value().accept();
  if (!accepted.ok()) {
    return accepted.status();
  }
  auto* accepted_tcp = dynamic_cast<upr::TcpStreamTransport*>(accepted.value().release());
  if (accepted_tcp == nullptr) {
    return upr::invalid_argument("Accepted transport was not TCP.");
  }
  const int client_fd = ::dup(client.value().native_handle());
  const int server_fd = ::dup(accepted_tcp->native_handle());
  delete accepted_tcp;
  if (client_fd < 0 || server_fd < 0) {
    if (client_fd >= 0) {
      ::close(client_fd);
    }
    if (server_fd >= 0) {
      ::close(server_fd);
    }
    return upr::io_error("dup() failed for TCP io_uring benchmark setup");
  }
  auto client_engine = upr::IoUringStreamEngine::create(client_fd, "uring://tcp-client", "uring://tcp-server");
  if (!client_engine.ok()) {
    ::close(server_fd);
    return client_engine.status();
  }
  auto server_engine = upr::IoUringStreamEngine::create(server_fd, "uring://tcp-server", "uring://tcp-client");
  if (!server_engine.ok()) {
    return server_engine.status();
  }
  return std::make_pair(upr::PluggableStreamTransport(std::move(client_engine.value())),
                        upr::PluggableStreamTransport(std::move(server_engine.value())));
}

template <typename Factory>
void register_one_way_benchmark(const std::string& name, Factory&& factory, size_t payload_size) {
  benchmark::RegisterBenchmark(name, [factory, payload_size](benchmark::State& state) {
    auto transports = factory();
    if (!transports.ok()) {
      state.SkipWithError(std::string(transports.status().message()));
      return;
    }
    upr::FrameChannel sender(transports.value().first);
    upr::FrameChannel receiver(transports.value().second);
    const std::vector<std::byte> payload = make_payload(payload_size, 7U);
    std::atomic<bool> running = true;

    std::thread consumer([&]() {
      while (running.load(std::memory_order_acquire)) {
        const upr::StatusOr<upr::TransportBufferLease> leased = receiver.try_acquire_frame();
        if (!leased.ok()) {
          if (leased.status().code() == upr::StatusCode::kNotFound) {
            auto readable = transports.value().second.wait_until_readable(1);
            if (!readable.ok()) {
              break;
            }
            continue;
          }
          break;
        }
        benchmark::DoNotOptimize(leased.value().bytes.data());
        (void)receiver.release_frame(leased.value());
      }
    });

    for (auto _ : state) {
      const upr::Status status = sender.send_frame(upr::ByteSpan(payload.data(), payload.size()));
      if (!status.ok()) {
        state.SkipWithError(std::string(status.message()));
        break;
      }
      benchmark::DoNotOptimize(payload.data());
    }
    running.store(false, std::memory_order_release);
    (void)transports.value().first.shutdown_write();
    consumer.join();
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(payload_size));
    state.SetItemsProcessed(state.iterations());
  });
}

template <typename Factory>
void register_request_response_benchmark(const std::string& name, Factory&& factory, size_t payload_size) {
  benchmark::RegisterBenchmark(name, [factory, payload_size](benchmark::State& state) {
    auto transports = factory();
    if (!transports.ok()) {
      state.SkipWithError(std::string(transports.status().message()));
      return;
    }
    upr::FrameChannel initiator(transports.value().first);
    upr::FrameChannel responder(transports.value().second);
    const std::vector<std::byte> request = make_payload(payload_size, 11U);
    const std::vector<std::byte> response = make_payload(payload_size, 19U);
    std::atomic<bool> running = true;

    std::thread responder_thread([&]() {
      while (running.load(std::memory_order_acquire)) {
        const upr::StatusOr<upr::TransportBufferLease> lease = responder.try_acquire_frame();
        if (lease.ok()) {
          (void)responder.release_frame(lease.value());
          (void)responder.send_frame(upr::ByteSpan(response.data(), response.size()));
          continue;
        }
        if (lease.status().code() == upr::StatusCode::kNotFound) {
          auto readable = transports.value().second.wait_until_readable(1);
          if (!readable.ok()) {
            break;
          }
          continue;
        }
        break;
      }
    });

    for (auto _ : state) {
      const upr::Status send_status = initiator.send_frame(upr::ByteSpan(request.data(), request.size()));
      if (!send_status.ok()) {
        state.SkipWithError(std::string(send_status.message()));
        break;
      }
      while (true) {
        const upr::StatusOr<upr::TransportBufferLease> lease = initiator.try_acquire_frame();
        if (lease.ok()) {
          benchmark::DoNotOptimize(lease.value().bytes.data());
          (void)initiator.release_frame(lease.value());
          break;
        }
        if (lease.status().code() == upr::StatusCode::kNotFound) {
          auto readable = transports.value().first.wait_until_readable(1);
          if (!readable.ok()) {
            state.SkipWithError(std::string(readable.status().message()));
            break;
          }
          continue;
        }
        state.SkipWithError("response receive failed");
        break;
      }
    }

    running.store(false, std::memory_order_release);
    // Unblock responder reads when the benchmark loop finishes.
    (void)transports.value().first.shutdown_write();
    responder_thread.join();
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(payload_size * 2U));
    state.SetItemsProcessed(state.iterations());
  });
}

void register_benchmarks() {
  for (const int payload_size : kPayloadSizes) {
    register_one_way_benchmark(
        "transport/unix_socket/one_way/bytes_" + std::to_string(payload_size),
        []() { return upr::UnixSocketTransport::create_socket_pair(); },
        payload_size);
    register_request_response_benchmark(
        "transport/unix_socket/request_response/bytes_" + std::to_string(payload_size),
        []() { return upr::UnixSocketTransport::create_socket_pair(); },
        payload_size);
    register_one_way_benchmark(
        "transport/local_shm/one_way/bytes_" + std::to_string(payload_size),
        [payload_size]() {
          return upr::LocalShmRingTransport::create_pair(
              {.slot_count = 256, .slot_size = static_cast<size_t>(std::max(payload_size, 4096))});
        },
        payload_size);
    register_request_response_benchmark(
        "transport/local_shm/request_response/bytes_" + std::to_string(payload_size),
        [payload_size]() {
          return upr::LocalShmRingTransport::create_pair(
              {.slot_count = 256, .slot_size = static_cast<size_t>(std::max(payload_size, 4096))});
        },
        payload_size);
    register_one_way_benchmark(
        "transport/tcp_loopback/one_way/bytes_" + std::to_string(payload_size),
        []() -> upr::StatusOr<std::pair<upr::TcpStreamTransport, upr::TcpStreamTransport>> {
          auto listener = upr::TcpListener::bind_loopback(0);
          if (!listener.ok()) {
            return listener.status();
          }
          auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", listener.value().port());
          if (!client.ok()) {
            return client.status();
          }
          auto accepted = listener.value().accept();
          if (!accepted.ok()) {
            return accepted.status();
          }
          auto* accepted_tcp = dynamic_cast<upr::TcpStreamTransport*>(accepted.value().release());
          if (accepted_tcp == nullptr) {
            return upr::invalid_argument("Accepted transport was not TCP.");
          }
          upr::TcpStreamTransport server = std::move(*accepted_tcp);
          delete accepted_tcp;
          return std::make_pair(std::move(client.value()), std::move(server));
        },
        payload_size);
    register_request_response_benchmark(
        "transport/tcp_loopback/request_response/bytes_" + std::to_string(payload_size),
        []() -> upr::StatusOr<std::pair<upr::TcpStreamTransport, upr::TcpStreamTransport>> {
          auto listener = upr::TcpListener::bind_loopback(0);
          if (!listener.ok()) {
            return listener.status();
          }
          auto client = upr::TcpStreamTransport::connect_to_host("127.0.0.1", listener.value().port());
          if (!client.ok()) {
            return client.status();
          }
          auto accepted = listener.value().accept();
          if (!accepted.ok()) {
            return accepted.status();
          }
          auto* accepted_tcp = dynamic_cast<upr::TcpStreamTransport*>(accepted.value().release());
          if (accepted_tcp == nullptr) {
            return upr::invalid_argument("Accepted transport was not TCP.");
          }
          upr::TcpStreamTransport server = std::move(*accepted_tcp);
          delete accepted_tcp;
          return std::make_pair(std::move(client.value()), std::move(server));
        },
        payload_size);
    register_one_way_benchmark(
        "transport/unix_socket_io_uring/one_way/bytes_" + std::to_string(payload_size),
        []() { return make_io_uring_unix_pair(); },
        payload_size);
    register_request_response_benchmark(
        "transport/unix_socket_io_uring/request_response/bytes_" + std::to_string(payload_size),
        []() { return make_io_uring_unix_pair(); },
        payload_size);
    register_one_way_benchmark(
        "transport/tcp_loopback_io_uring/one_way/bytes_" + std::to_string(payload_size),
        []() { return make_io_uring_tcp_pair(); },
        payload_size);
    register_request_response_benchmark(
        "transport/tcp_loopback_io_uring/request_response/bytes_" + std::to_string(payload_size),
        []() { return make_io_uring_tcp_pair(); },
        payload_size);
  }
}

}  // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  register_benchmarks();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
