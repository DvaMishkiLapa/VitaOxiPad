#include <psp2/kernel/processmgr.h>
#include <psp2/motion.h>
#include <psp2/touch.h>

#include <common.h>

#include "client.hpp"
#include "ctrl.hpp"
#include "events.hpp"
#include "net.hpp"

constexpr size_t MAX_EPOLL_EVENTS = 10;
constexpr time_t MAX_HEARTBEAT_INTERVAL = 25;
constexpr time_t SECOND_IN_MICROS = 1000 * 1000;

static int send_all(int fd, const void *buf, unsigned int size) {
  const char *buf_ptr = static_cast<const char *>(buf);
  int bytes_sent = 0;

  while (size > 0) {
    bytes_sent = sceNetSend(fd, buf_ptr, size, 0);
    if (bytes_sent < 0)
      return bytes_sent;

    buf_ptr += bytes_sent;
    size -= bytes_sent;
  }

  return bytes_sent;
}

static void handle_ingoing_data(Client &client) {
  constexpr size_t BUFFER_SIZE = 1024;

  uint8_t buffer[BUFFER_SIZE];
  int received;
  while ((received = sceNetRecv(client.ctrl_fd(), buffer, BUFFER_SIZE, 0)) > 0) {
    client.add_to_buffer(buffer, received);
  }

  SCE_DBG_LOG_DEBUG("Received %i bytes from %s", received, client.ip());

  while (client.handle_heartbeat() || client.handle_data())
    ;

  if (received <= 0) {
    switch ((unsigned)received) {
    case SCE_NET_ERROR_EWOULDBLOCK:
      break;
    default:
      throw net::NetException(received);
    }
  }
}

static void send_handshake_response(Client &client, uint16_t port, uint32_t heartbeat_interval,
                                    flatbuffers::FlatBufferBuilder &builder) {
  builder.Clear();
  auto handshake_confirm = NetProtocol::CreateHandshake(builder, NetProtocol::Endpoint::Server,
                                                        port, heartbeat_interval);
  auto packet = NetProtocol::CreatePacket(builder, NetProtocol::PacketContent::Handshake,
                                          handshake_confirm.Union());
  builder.FinishSizePrefixed(packet);

  int sent = send_all(client.ctrl_fd(), builder.GetBufferPointer(), builder.GetSize());

  if (sent <= 0) {
    throw net::NetException(sent);
  }

  client.set_state(Client::State::Connected);
}

static void disconnect_client(std::optional<Client> &client, SceUID ev_flag) {
  if (client) {
    SCE_DBG_LOG_INFO("Flushing buffer for client %s before disconnection", client->ip());
    client->shrink_buffer();
  }
  sceKernelSetEventFlag(ev_flag, MainEvent::PC_DISCONNECT);
  if (!client)
    return;

  SCE_DBG_LOG_INFO("Client %s disconnected", client->ip());
  client.reset();
}

static void add_client(int server_tcp_fd, SceUID epoll, std::optional<Client> &client,
                       SceUID ev_flag) {
  SceNetSockaddrIn clientaddr;
  unsigned int addrlen = sizeof(clientaddr);
  int client_fd =
      sceNetAccept(server_tcp_fd, reinterpret_cast<SceNetSockaddr *>(&clientaddr), &addrlen);
  if (client_fd >= 0) {
    client.emplace(client_fd, epoll);

    SceNetEpollEvent cl_ev = {};
    cl_ev.events = SCE_NET_EPOLLIN | SCE_NET_EPOLLOUT | SCE_NET_EPOLLHUP | SCE_NET_EPOLLERR;
    cl_ev.data.u32 = static_cast<decltype(cl_ev.data.u32)>(SocketType::CLIENT);
    auto nbio = 1;
    sceNetSetsockopt(client_fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio));

    sceNetEpollControl(epoll, SCE_NET_EPOLL_CTL_ADD, client_fd, &cl_ev);
    sceKernelSetEventFlag(ev_flag, MainEvent::PC_CONNECT);
  }
}

static void refuse_client(int server_tcp_fd) {
  SceNetSockaddrIn clientaddr;
  auto clientaddr_size = sizeof(clientaddr);
  auto client_fd = sceNetAccept(server_tcp_fd, reinterpret_cast<SceNetSockaddr *>(&clientaddr),
                                &clientaddr_size);
  if (client_fd >= 0) {
    sceNetSocketClose(client_fd);
  }
}

struct NetCtlCallbackData {
  SceUID event_flag_netctl;
};

enum NetCtlEvents {
  Connected = 1,
  Disconnected = 2,
};

void *netctl_cb(int state, void *arg) {
  auto data = static_cast<NetCtlCallbackData *>(arg);
  if (!data) {
    SCE_DBG_LOG_ERROR("netctl_cb received null data pointer");
    return nullptr;
  }

  switch (state) {
  case SCE_NETCTL_STATE_DISCONNECTED:
  case SCE_NETCTL_STATE_CONNECTING:
  case SCE_NETCTL_STATE_FINALIZING:
    sceKernelSetEventFlag(data->event_flag_netctl, NetCtlEvents::Disconnected);
    break;
  case SCE_NETCTL_STATE_CONNECTED:
    sceKernelSetEventFlag(data->event_flag_netctl, NetCtlEvents::Connected);
    break;
  default:
    break;
  }

  return nullptr;
}

int net_thread(__attribute__((unused)) unsigned int arglen, void *argp) {
  assert(arglen == sizeof(ThreadMessage));

  ThreadMessage *message = static_cast<ThreadMessage *>(argp);
  SharedData *shared_data = message->shared_data;

  // Creating a TCP socket to accept heartbeat packets
  auto server_tcp_fd = sceNetSocket("SERVER_SOCKET", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  SceNetSockaddrIn serveraddr;
  serveraddr.sin_family = SCE_NET_AF_INET;
  serveraddr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
  serveraddr.sin_port = sceNetHtons(NET_PORT);
  sceNetBind(server_tcp_fd, reinterpret_cast<SceNetSockaddr *>(&serveraddr), sizeof(serveraddr));
  auto nbio = 1;
  sceNetSetsockopt(server_tcp_fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &nbio, sizeof(nbio));
  sceNetListen(server_tcp_fd, 1);

  // Creating a UDP socket to send data to client
  auto server_udp_fd = sceNetSocket("SERVER_UDP_SOCKET", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
  sceNetBind(server_udp_fd, reinterpret_cast<SceNetSockaddr *>(&serveraddr), sizeof(serveraddr));

  std::optional<Client> client;

  // Configuring CallBack Event for network status (disconnected, connected)
  int cbid;
  auto timeout = MIN_POLLING_INTERVAL_MICROS;
  auto connect_state = sceKernelCreateEventFlag("ev_netctl", 0, 0, nullptr);
  if (connect_state < 0) {
    SCE_DBG_LOG_ERROR("Failed to create event flag: 0x%08X", connect_state);
    return -1;
  }
  static auto netctl_cb_data = NetCtlCallbackData{connect_state};
  int ret = sceNetCtlInetRegisterCallback(&netctl_cb, &netctl_cb_data, &cbid);
  if (ret < 0) {
    SCE_DBG_LOG_ERROR("Failed to register netctl callback: 0x%08X", ret);
    return -1; // Or handle the error appropriately
  }

  // Creating epoll for events from TCP (heartbeat)
  SceUID epoll = sceNetEpollCreate("SERVER_EPOLL", 0);
  static SceNetEpollEvent ev = {};
  ev.events = SCE_NET_EPOLLIN;
  ev.data.u32 = static_cast<decltype(ev.data.u32)>(SocketType::SERVER);
  sceNetEpollControl(epoll, SCE_NET_EPOLL_CTL_ADD, server_tcp_fd, &ev);

  // Various structures
  int n; // number of events that sceNetEpollWait will return
  static SceNetEpollEvent events[MAX_EPOLL_EVENTS];          // event storage
  static flatbuffers::FlatBufferBuilder pad_data(512);       // keystroke data storage
  static flatbuffers::FlatBufferBuilder handshake_data(128); // response to heartbeat

  SceCtrlData pad;
  SceMotionState motion_data;
  SceTouchData touch_data_front, touch_data_back;

  // Main loop of the network
  // Ends if there was a sceNetEpollWait error or the thread was asked to stop
  // via g_net_thread_running
  while (g_net_thread_running.load()) {
    // Power tick for sleep disabling, update battery
    sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
    get_ctrl(&pad, &motion_data, &touch_data_front, &touch_data_back);
    shared_data->pad_data = pad;

    // Receiving TCP events
    n = sceNetEpollWait(epoll, events, MAX_EPOLL_EVENTS, timeout);
    if (n < 0) {
      SCE_DBG_LOG_ERROR("sceNetEpollWait error: 0x%08X (%s)", n, sce_net_strerror(n));
      break;
    }
    sceNetCtlCheckCallback();

    // Checking network status change events
    unsigned int event;
    if (sceKernelPollEventFlag(connect_state, NetCtlEvents::Connected | NetCtlEvents::Disconnected,
                               SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR, &event) == 0) {
      switch (event) {
      case NetCtlEvents::Connected:
        SCE_DBG_LOG_INFO("Connected to internet");
        sceNetBind(server_tcp_fd, reinterpret_cast<SceNetSockaddr *>(&serveraddr),
                   sizeof(serveraddr));
        sceNetListen(server_tcp_fd, 1);
        sceNetBind(server_udp_fd, reinterpret_cast<SceNetSockaddr *>(&serveraddr),
                   sizeof(serveraddr));
        sceKernelSetEventFlag(message->ev_flag, MainEvent::NET_CONNECT);
        break;
      case NetCtlEvents::Disconnected:
        SCE_DBG_LOG_INFO("Disconnected from internet");
        sceKernelSetEventFlag(message->ev_flag, MainEvent::NET_DISCONNECT);
        client.reset();
        timeout = SECOND_IN_MICROS;
        break;
      }
    }

    // TCP event handling
    for (size_t i = 0; i < (unsigned)n; i++) {
      auto ev_el = events[i];
      SocketType sock_type = static_cast<SocketType>(ev_el.data.u32);

      if (ev_el.events & SCE_NET_EPOLLHUP || ev_el.events & SCE_NET_EPOLLERR) {
        if (sock_type == SocketType::CLIENT) {
          disconnect_client(client, message->ev_flag);
          SCE_DBG_LOG_INFO("Client disconnected: %s", client->ip());
        }
      } else if (ev_el.events & SCE_NET_EPOLLIN) {
        if (sock_type == SocketType::SERVER) {
          if (!client) {
            add_client(server_tcp_fd, epoll, client, message->ev_flag);
          } else {
            refuse_client(server_tcp_fd);
          }

          if (client) {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            snprintf(shared_data->client_ip, INET_ADDRSTRLEN, "%s", client->ip());
            shared_data->events |= MainEvent::PC_CONNECT;
            sceKernelSetEventFlag(message->ev_flag, MainEvent::PC_CONNECT);
            SCE_DBG_LOG_INFO("New client connected: %s", client->ip());
          }

          continue;
        }

        if (!client) {
          SCE_DBG_LOG_ERROR("Client is null and still is in epoll");
          continue;
        }

        try {
          SCE_DBG_LOG_INFO("Handling ingoing data from %s", client->ip());
          handle_ingoing_data(*client);
        } catch (const net::NetException &e) {
          if (e.error_code() == SCE_NET_ECONNRESET || e.error_code() == 0) {
            disconnect_client(client, message->ev_flag);
            SCE_DBG_LOG_INFO("Client disconnected: %s", client->ip());
          }
        } catch (const std::exception &e) {
          disconnect_client(client, message->ev_flag);
          SCE_DBG_LOG_DEBUG("Client disconnected: %s", client->ip());
        }
      } else if (ev_el.events & SCE_NET_EPOLLOUT) {
        if (sock_type == SocketType::SERVER) {
          continue;
        }

        // Processing of client events
        switch (client->state()) {
        case Client::State::WaitingForServerConfirm: {
          try {
            send_handshake_response(*client, NET_PORT, MAX_HEARTBEAT_INTERVAL, handshake_data);
            SCE_DBG_LOG_INFO("Sent handshake response to %s", client->ip());

            SceNetEpollEvent reinit_ev = {};
            reinit_ev.events = SCE_NET_EPOLLIN | SCE_NET_EPOLLHUP | SCE_NET_EPOLLERR;
            reinit_ev.data.u32 = static_cast<decltype(reinit_ev.data.u32)>(SocketType::CLIENT);
            sceNetEpollControl(epoll, SCE_NET_EPOLL_CTL_MOD, client->ctrl_fd(), &reinit_ev);
          } catch (const net::NetException &e) {
            if (e.error_code() == SCE_NET_ECONNRESET) {
              disconnect_client(client, message->ev_flag);
              SCE_DBG_LOG_INFO("Client disconnected: %s", client->ip());
            }
          } catch (const std::exception &e) {
            disconnect_client(client, message->ev_flag);
            SCE_DBG_LOG_INFO("Client disconnected: %s", client->ip());
          }

          break;
        }

        default:
          break;
        }
      }
    }

    if (!client)
      continue;

    if (client->time_since_last_heartbeat() > MAX_HEARTBEAT_INTERVAL) {
      disconnect_client(client, message->ev_flag);
      SCE_DBG_LOG_INFO("Client disconnected: %s", client->ip());
    }

    // Sending push data if the client is connected
    if (client->state() == Client::State::Connected && client->is_polling_time_elapsed() &&
        shared_data->pad_mode) {
      if (server_udp_fd >= 0) {
        ctrl_as_netprotocol(&pad, &motion_data, &touch_data_front, &touch_data_back, pad_data,
                            shared_data->battery_level);
        client->update_sent_data_time();
        auto client_addr = client->data_conn_info();
        SceNetSockaddr *need_client_addr = reinterpret_cast<SceNetSockaddr *>(&client_addr);
        int res = sceNetSendto(server_udp_fd, pad_data.GetBufferPointer(), pad_data.GetSize(), 0,
                               need_client_addr, sizeof(client_addr));
        if (res < 0) {
          SCE_DBG_LOG_ERROR("sceNetSendto error: 0x%08X (%s)", res, sce_net_strerror(res));
          continue;
        }
      } else {
        SCE_DBG_LOG_ERROR("server_udp_fd not valid: %d", server_udp_fd);
        continue;
      }
    }

    if (!client) {
      continue;
    }

    timeout = client->remaining_polling_time();
    sceKernelDelayThread(5 * 1000);
  }

  sceNetCtlInetUnregisterCallback(cbid);
  sceNetEpollDestroy(epoll);

  return 0;
}
