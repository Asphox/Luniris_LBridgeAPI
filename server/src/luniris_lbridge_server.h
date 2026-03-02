#ifndef LUNIRIS_LBRIDGE_SERVER_H
#define LUNIRIS_LBRIDGE_SERVER_H

#include <memory>
#include <cstdint>
#include <atomic>

// Forward declarations
class GrpcClient;
struct lbridge_context;
struct lbridge_server;

/**
 * @brief LBridge server that bridges external clients to the local Luniris gRPC API.
 */
class LunirisLBridgeServer
{
public:
    struct Config
    {
        const char* grpc_address;      // Address of local Luniris gRPC server
        const char* lbridge_address;   // LBridge listen address (e.g., "0.0.0.0")
        uint32_t max_clients;          // Maximum concurrent LBridge clients
        uint32_t client_timeout_ms;    // Client inactivity timeout (0 = disabled)
    };

    /**
     * @brief Constructs the server with the given configuration.
     */
    explicit LunirisLBridgeServer(const Config& config);
    ~LunirisLBridgeServer();

    // Non-copyable
    LunirisLBridgeServer(const LunirisLBridgeServer&) = delete;
    LunirisLBridgeServer& operator=(const LunirisLBridgeServer&) = delete;

    /**
     * @brief Initializes the server. Must be called before run().
     * @return true on success, false on failure.
     */
    bool init();

    /**
     * @brief Runs the server main loop (blocking).
     * Call stop() from another thread to exit.
     */
    void run();

    /**
     * @brief Signals the server to stop.
     */
    void stop();

    /**
     * @brief Gets the gRPC client for direct access.
     */
    GrpcClient* get_grpc_client() const { return grpc_client_.get(); }

private:
    static bool on_rpc_call(struct lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size);

    Config config_;
    std::unique_ptr<GrpcClient> grpc_client_;
    struct lbridge_context* lbridge_ctx_;
    struct lbridge_server* lbridge_srv_;
    std::atomic<bool> running_;

    // Static instance pointer for callback routing
    static LunirisLBridgeServer* s_instance;

    // RPC handlers
    bool handle_send_eye_coordinates(struct lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size);
    bool handle_send_eyelid_state(struct lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size);
    bool handle_get_eye_coordinates(struct lbridge_rpc_context* ctx);
    bool handle_get_eyelid_state(struct lbridge_rpc_context* ctx);
    bool handle_get_brightness(struct lbridge_rpc_context* ctx);
    bool handle_send_brightness(struct lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size);
    bool handle_get_gyroscope(struct lbridge_rpc_context* ctx);
    bool handle_get_accelerometer(struct lbridge_rpc_context* ctx);
    bool handle_get_temperature(struct lbridge_rpc_context* ctx);
    bool handle_get_registered_actions(struct lbridge_rpc_context* ctx);
    bool handle_get_action(struct lbridge_rpc_context* ctx);
    bool handle_send_action(struct lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size);
    bool handle_send_led_settings(struct lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size);
};

#endif // LUNIRIS_LBRIDGE_SERVER_H
