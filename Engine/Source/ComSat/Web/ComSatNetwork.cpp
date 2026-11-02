// wow335modding: browser build of Win64/ComSatNetworkAddress.cpp and Win64/ComSatNetworkSocketPosix.cpp on POSIX
// sockets. ComSat is voice chat over UDP, which a page cannot send, so its sockets stay closed and the voice
// channel never connects.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include "Engine/Source/ComSat/ComSatNetwork.h"

void ComSat::Network::Address::SetPort(int32_t port) {
    this->m_port = htons(static_cast<uint16_t>(port));
}

int32_t ComSat::Network::Address::GetPort() const {
    return ntohs(this->m_port);
}

void ComSat::Network::Address::SetAddress(const char* address) {
    const char* colon = strchr(address, ':');
    if (colon) {
        if (strlen(address) < 128) {
            char host[128];
            strcpy(host, address);
            host[colon - address] = '\0';
            this->m_ip = inet_addr(host);
            this->m_port = htons(static_cast<uint16_t>(strtol(colon + 1, nullptr, 10)));
        }
    } else {
        this->m_ip = inet_addr(address);
    }
}

ComSat::Network::SocketPosix::SocketPosix() {
    this->m_socket = socket(AF_INET, SOCK_DGRAM, 0);
    fcntl(this->m_socket, F_SETFL, O_NONBLOCK);
}

ComSat::Network::SocketPosix::~SocketPosix() {
    close(this->m_socket);
}

bool ComSat::Network::SocketPosix::Bind(int32_t port) {
    ComSat::Network::Address address;
    address.SetPort(port);
    return bind(this->m_socket, reinterpret_cast<const sockaddr*>(&address.m_family), address.m_length) == 0;
}

int32_t ComSat::Network::SocketPosix::SendTo(const ComSat::Network::Address& address, const uint8_t* data, uint32_t size) {
    return sendto(this->m_socket, data, size, 0, reinterpret_cast<const sockaddr*>(&address.m_family), address.m_length);
}

bool ComSat::Network::SocketPosix::RecvFrom(ComSat::Network::Address& address, uint8_t* data, uint32_t& size) {
    memset(data, 0, size);
    socklen_t length = address.m_length;
    ssize_t received = recvfrom(this->m_socket, data, size, 0, reinterpret_cast<sockaddr*>(&address.m_family), &length);
    address.m_length = length;
    if (received <= 0) {
        return false;
    }
    size = received;
    return true;
}

#endif
