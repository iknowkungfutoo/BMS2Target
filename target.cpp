#include <iostream>
#include <ws2tcpip.h>

#include "target.h"

bool Target::create_connection( PCSTR port )
{
    int iResult;

    WSADATA wsaData;
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        std::cerr << "WSAStartup failed with error: " << iResult << std::endl;
        return false;
    }

    char hostname[256];
    iResult = gethostname(hostname, sizeof(hostname));
    if (iResult != 0)
    {
        std::cerr << "gethostname failed with error: " << iResult << std::endl;
        WSACleanup();
        return false;
    }

    struct addrinfo  hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    struct addrinfo* result = NULL;
    iResult = getaddrinfo(hostname, port, &hints, &result);
    if (iResult != 0)
    {
        std::cerr << "getaddrinfo failed with error: " << iResult << std::endl;
        WSACleanup();
        return false;
    }

    // Attempt to connect to an address until one succeeds
    struct addrinfo* ptr = NULL;
    for (ptr = result; ptr != NULL; ptr = ptr->ai_next)
    {
        // Create a SOCKET for connecting to server
        m_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (m_socket == INVALID_SOCKET)
        {
            std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return false;
        }

        // Connect to server.
        iResult = connect(m_socket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR)
        {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (m_socket == INVALID_SOCKET)
    {
        std::cerr << "Unable to connect to server!" << std::endl;
        WSACleanup();
        return false;
    }

    return true;
}

bool Target::break_connection()
{
    // shutdown the connection since no more data will be sent
    int iResult = shutdown(m_socket, SD_SEND);
    if (iResult == SOCKET_ERROR)
    {
        std::cerr << "shutdown failed with error: " << WSAGetLastError() << std::endl;
        closesocket(m_socket);
        WSACleanup();
        return false;
    }

    closesocket(m_socket);
    WSACleanup();

    return true;
}

bool Target::send_message( const std::string& message )
{
    const int HEADER_SIZE      = 2;

    int packet_size = HEADER_SIZE + message.length(); // packet_size is length of message plus 2 bytes of header
    if (packet_size > MAX_PAYLOAD_SIZE)
    {
        std::cerr << "message is too long!" << std::endl;
        return false;
    }

    std::string payload;

    payload += packet_size & 0xFF;
    payload += (packet_size >> 8) & 0xFF;
    payload += message;

    int iResult = send(m_socket, payload.data(), packet_size, 0);
    if (iResult == SOCKET_ERROR)
    {
        std::cerr << "send failed with error: " << WSAGetLastError() << std::endl;
        closesocket(m_socket);
        WSACleanup();
        return false;
    }

    return true;
}
