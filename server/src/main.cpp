#include "luniris_lbridge_server.h"
#include "crash_handler.h"

#include <csignal>
#include <cstring>
#include <cstdlib>
#include "server_log.h"


static LunirisLBridgeServer* g_server = nullptr;

static void signal_handler(int sig)
{
    (void)sig;
    if (g_server)
    {
        g_server->stop();
    }
}

int main()
{
    // Enable gRPC HTTP/2 tracing (must be before any gRPC initialization)
    //setenv("GRPC_VERBOSITY", "DEBUG", 1);
    //setenv("GRPC_TRACE", "http", 1);

    // Setup crash handlers (must be before anything else)
    install_crash_handlers();

    // Setup graceful shutdown handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Fixed configuration
    LunirisLBridgeServer::Config config = {};
    config.grpc_address = "unix:///tmp/features.socket";
    config.lbridge_address = "::";
    config.max_clients = 100;
    config.client_timeout_ms = 60000*5; // 5min

    // Create and initialize server
    LunirisLBridgeServer server(config);
    g_server = &server;

    server_log(false, "Luniris LBridge API Server starting\n");
    server_log(false, "  LBridge address: %s\n", config.lbridge_address);
    server_log(false, "  Luniris gRPC:    %s\n", config.grpc_address);

    if (!server.init())
    {
        server_log(true, "Failed to initialize server\n");
        return 1;
    }

    server_log(false, "Server started.\n");

    server.run();

    server_log(false, "Server stopped.\n");

    g_server = nullptr;
    return 0;
}
