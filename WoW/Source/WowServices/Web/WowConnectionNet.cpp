// wow335modding: browser build of Win64/WowConnectionNet.cpp. Emscripten's sockets are WebSockets behind a POSIX
// interface, and poll() blocks properly from a worker thread, so the WSAEventSelect wait becomes a poll() over every
// connection's socket, rebuilt each round.
// ponytail: nothing wakes the poll when a state changes (new connection, queued write); its 10 ms timeout bounds that
// delay. A pipe in the poll set would make it instant if that ever matters.
#if defined(WOWDECOMP_PLATFORM_WEB)

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include "WoW/Source/WowServices/WowConnectionNet.h"

void WowConnectionNet::PlatformChangeState(WowConnection*, WOW_CONN_STATE) {
}

void WowConnectionNet::PlatformRun() {
    this->m_networkStarted = true;

    static const uint32_t MAX_CONNECTIONS = 64;
    int32_t timeout = 10;

    while (!this->m_stop) {
        WowConnection* connections[MAX_CONNECTIONS];
        pollfd fds[MAX_CONNECTIONS];
        uint32_t count = 0;

        this->m_connectionsLock.Enter();

        for (WowConnection* connection = this->m_connections.Head(); connection && count < MAX_CONNECTIONS; connection = this->m_connections.Next(connection)) {
            if (connection->m_serviceCount) {
                continue;
            }

            short events;

            switch (connection->m_connState) {
                case WOWC_CONNECTING:
                    events = POLLOUT;
                    break;

                case WOWC_LISTENING:
                    events = POLLIN;
                    break;

                case WOWC_CONNECTED:
                    events = POLLIN | (connection->m_sendList.Head() || connection->m_notifyWriteReady ? POLLOUT : 0);
                    break;

                case WOWC_DISCONNECTING:
                    events = 0;
                    break;

                default:
                    continue;
            }

            connection->m_refCount.Increment();
            fds[count].fd = connection->m_sock;
            fds[count].events = events;
            fds[count].revents = 0;
            connections[count] = connection;
            count++;
        }

        this->m_connectionsLock.Leave();

        poll(fds, count, timeout);

        for (uint32_t i = 0; i < count; i++) {
            WowConnection* connection = connections[i];
            short revents = fds[i].revents;
            uint32_t flags = 0;

            if (revents & (POLLIN | POLLERR | POLLHUP)) {
                flags = 2;
            }

            if (revents & (POLLOUT | POLLERR | POLLHUP)) {
                flags |= 1;
            }

            if (connection->m_connState == WOWC_DISCONNECTING) {
                flags |= 8;
            }

            if (WowConnection::s_lagTestDelayMin || WowConnection::s_lagTestDelayMax || connection->m_lagTestRecvList.Head()) {
                flags |= 3;
            }

            if (flags) {
                this->SignalWorker(connection, flags);
            }

            connection->Release();
        }
    }
}

void WowConnectionNet::PlatformInit(bool) {
}

void WowConnectionNet::PlatformDestruct(WowConnection*) {
}

void WowConnectionNet::PlatformWorkerReady() {
}

void WowConnectionNet::PlatformAdd(WowConnection* connection) {
    int noDelay = 1;
    setsockopt(connection->m_sock, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));
}

void WowConnectionNet::PlatformRemove(WowConnection*) {
}

void WowConnectionNet::PlatformStop() {
}

void WowConnectionNet::PlatformDestroy() {
}

#endif
