#pragma once

#include <winsock2.h>

#pragma comment (lib, "Ws2_32.lib")

class Target
{
public:
    const int MAX_PAYLOAD_SIZE = 256;

    Target() { m_socket = INVALID_SOCKET; }

    bool create_connection(PCSTR port = "2323");
    bool break_connection();
    bool send_message(const std::string& message);

private:
    SOCKET m_socket;
};

