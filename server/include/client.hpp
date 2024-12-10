#ifndef EPOLL_HPP
#define EPOLL_HPP

#include <arpa/inet.h>
#include <psp2/libdbg.h>
#include <psp2/net/net.h>
#include <psp2/rtc.h>

#include "heartbeat.hpp"

#include <netprotocol_generated.h>

constexpr unsigned int MIN_POLLING_INTERVAL_MICROS = (1 * 1000 / 144) * 1000;

class TimeHelper {
public:
  TimeHelper() { update(); }
  void update() { last_time_micros_ = get_current_time_micros(); }
  uint64_t elapsed_time_secs() const {
    return (get_current_time_micros() - last_time_micros_) / 1'000'000;
  }
  uint64_t elapsed_time_micros() const { return get_current_time_micros() - last_time_micros_; }

private:
  uint64_t get_current_time_micros() const {
    SceKernelSysClock sys_clock;
    sceKernelGetProcessTime(&sys_clock);
    return sys_clock;
  }
  uint64_t last_time_micros_;
};

class EpollSocket {
public:
  EpollSocket(int sock_fd, SceUID epoll) : fd_(sock_fd), epoll_(epoll) {}
  ~EpollSocket() {
    SCE_DBG_LOG_TRACE("Closing socket %d", fd_);
    sceNetEpollControl(epoll_, SCE_NET_EPOLL_CTL_DEL, fd_, nullptr);
    sceNetSocketClose(fd_);
  }
  int fd() const { return fd_; }

private:
  int fd_;
  SceUID epoll_;
};

class ClientException : public std::exception {
public:
  ClientException(std::string const &msg) : msg_(msg) {}
  char const *what() const noexcept override { return msg_.c_str(); }

private:
  std::string msg_;
};

class Client {
public:
  enum class State { WaitingForHandshake, WaitingForServerConfirm, Connected };

  static constexpr size_t MAX_BUFFER_ACCEPTABLE_SIZE = 1 * 1024 * 1024;

  Client(int fd, SceUID epoll) : sock_(fd, epoll) {
    SceNetSockaddrIn clientaddr;
    unsigned int addrlen = sizeof(clientaddr);
    sceNetGetpeername(fd, reinterpret_cast<SceNetSockaddr *>(&clientaddr), &addrlen);
    sceNetInetNtop(SCE_NET_AF_INET, &(clientaddr.sin_addr), ip_, INET_ADDRSTRLEN);
  }

  int ctrl_fd() const { return sock_.fd(); }
  const char *ip() const { return ip_; }

  State state() const { return state_; }
  void set_state(State state) { state_ = state; }

  /**
   * @brief Returns time in seconds since last heartbeat
   */
  uint64_t time_since_last_heartbeat() const { return heartbeat_time_helper_.elapsed_time_secs(); }
  void update_heartbeat_time() { heartbeat_time_helper_.update(); }

  /**
   * @brief Returns time in microseconds since last sent data
   */
  uint64_t time_since_last_sent_data() const {
    return sent_data_time_helper_.elapsed_time_micros();
  }
  void update_sent_data_time() { sent_data_time_helper_.update(); }
  bool is_polling_time_elapsed() const { return time_since_last_sent_data() > polling_time_; }
  uint64_t remaining_polling_time() const {
    return std::clamp(polling_time_ - time_since_last_sent_data(), static_cast<uint64_t>(0),
                      polling_time_);
  }

  void add_to_buffer(uint8_t const *data, size_t size) {
    if (buffer_.size() + size > MAX_BUFFER_ACCEPTABLE_SIZE) {
      SCE_DBG_LOG_ERROR("Buffer overflow, clearing buffer for client: %s", ip_);
      buffer_.clear();
      throw ClientException("Buffer size exceeded");
    } else {
      buffer_.insert(buffer_.end(), data, data + size);
    }
  }

  bool handle_data() {
    typedef void (Client::*BufferHandler)(const void *);

    auto data = NetProtocol::GetSizePrefixedPacket(buffer_.data());
    SCE_DBG_LOG_TRACE("Received flatbuffer packet from %s", ip());

    const std::unordered_map<NetProtocol::PacketContent, BufferHandler> handlers = {
        {NetProtocol::PacketContent::Handshake, &Client::handle_handshake},
        {NetProtocol::PacketContent::Config, &Client::handle_config},
    };

    auto handler_entry = handlers.find(data->content_type());
    if (handler_entry == handlers.end())
      return false;
    flatbuffers::Verifier verifier(buffer_.data(), buffer_.size());
    if (!NetProtocol::VerifySizePrefixedPacketBuffer(verifier)) {
      SCE_DBG_LOG_ERROR("Invalid Flatbuffer packet from %s", ip_);
      buffer_.clear();
      return false;
    }
    auto [_, handler] = *handler_entry;

    SCE_DBG_LOG_TRACE("Calling %s handler for %s",
                      NetProtocol::EnumNamePacketContent(data->content_type()), ip());
    std::invoke(handler, this, data->content());

    auto size = verifier.GetComputedSize();
    SCE_DBG_LOG_TRACE("Removing %lu bytes from buffer after invoking handler "
                      "for %s (size: %lu, client: %s)",
                      size, NetProtocol::EnumNamePacketContent(data->content_type()),
                      buffer_.size(), ip());
    buffer_.erase(buffer_.begin(), buffer_.begin() + size);
    return true;
  }

  void handle_handshake(const void *buffer) {
    auto handshake = static_cast<NetProtocol::Handshake const *>(buffer);
    SCE_DBG_LOG_TRACE("Received handshake from %s", ip());

    SceNetSockaddrIn clientaddr;
    unsigned int addrlen = sizeof(clientaddr);
    sceNetGetpeername(ctrl_fd(), reinterpret_cast<SceNetSockaddr *>(&clientaddr), &addrlen);
    clientaddr.sin_port = sceNetHtons(handshake->port());
    SCE_DBG_LOG_TRACE("Setting data connection info to: %s:%d", ip(), handshake->port());

    auto addr = reinterpret_cast<SceNetSockaddr *>(&clientaddr);
    set_data_conn_info(*addr);
    set_state(Client::State::WaitingForServerConfirm);
    SCE_DBG_LOG_TRACE("Setting state to WaitingForServerConfirm for %s", ip());
  }

  void handle_config(const void *buffer) {
    auto config = static_cast<NetProtocol::Config const *>(buffer);
    SCE_DBG_LOG_TRACE("Received config from %s", ip());

    if (config->polling_interval() > MIN_POLLING_INTERVAL_MICROS) {
      polling_time_ = config->polling_interval();
    }
  }

  bool handle_heartbeat() {
    if (buffer_.size() < heartbeat_magic.size() ||
        !std::equal(heartbeat_magic.begin(), heartbeat_magic.end(), buffer_.begin()))
      return false;

    SCE_DBG_LOG_TRACE("Received heartbeat from %s", ip());
    update_heartbeat_time();

    constexpr auto size = heartbeat_magic.size();
    SCE_DBG_LOG_TRACE("Removing %lu bytes from heartbeat for buffer (size: %lu, client: %s)", size,
                      buffer_.size(), ip());
    buffer_.erase(buffer_.begin(), buffer_.begin() + size);
    return true;
  }

  void shrink_buffer() { buffer_.shrink_to_fit(); }

  SceNetSockaddrIn data_conn_info() const { return data_conn_info_; }
  void set_data_conn_info(SceNetSockaddr info) {
    data_conn_info_ = reinterpret_cast<SceNetSockaddrIn &>(info);
  }

private:
  EpollSocket sock_;
  TimeHelper heartbeat_time_helper_;
  TimeHelper sent_data_time_helper_;
  /**
   * @brief Time in microseconds between polling for data
   */
  uint64_t polling_time_ = MIN_POLLING_INTERVAL_MICROS;

  State state_ = State::WaitingForHandshake;
  std::vector<uint8_t> buffer_;
  SceNetSockaddrIn data_conn_info_;
  char ip_[INET_ADDRSTRLEN];
};

enum class SocketType {
  SERVER = 1,
  CLIENT = 2,
};

#endif // EPOLL_HPP
