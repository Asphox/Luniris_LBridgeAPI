#ifndef GRPC_CLIENT_H
#define GRPC_CLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

// Forward declaration for PIMPL (hides gRPC types from this header)
struct GrpcClientImpl;

/**
 * @brief Plain data structure for action info, exposed in the public API
 * instead of the gRPC-generated Action type (which would conflict with nanopb).
 */
struct ActionInfo
{
    std::string key;
    std::string name;
    bool is_available;
    int source;
};

/**
 * @brief gRPC client to communicate with the local Luniris daemon.
 *
 * Streaming RPCs are handled by background threads that cache the latest values.
 * Polling functions return the cached values.
 *
 * Uses PIMPL to hide gRPC-generated types from the header, avoiding name
 * conflicts with nanopb types that share the same names (EyeCoordinates, etc.).
 */
class GrpcClient
{
public:
    explicit GrpcClient(const std::string& address);
    ~GrpcClient();

    // Non-copyable
    GrpcClient(const GrpcClient&) = delete;
    GrpcClient& operator=(const GrpcClient&) = delete;

    /**
     * @brief Starts all streaming threads. Call after construction.
     */
    void start();

    /**
     * @brief Stops all streaming threads.
     */
    void stop();

    // Eye Position API (cached from streams)
    bool get_eye_coordinates(float& out_x, float& out_y);
    bool get_eyelid_state(float& out_closure);

    // Eye Position API (send)
    bool set_eye_coordinates(float x, float y);
    bool set_eyelid_state(float closure);

    // Display API (unary)
    bool get_brightness(int32_t& out_level);
    bool set_brightness(int32_t level);

    // IMU API (cached from streams)
    bool get_gyroscope(float& out_x, float& out_y, float& out_z);
    bool get_accelerometer(float& out_x, float& out_y, float& out_z);
    bool get_temperature(float& out_celsius);

    // Actions API
    bool get_registered_actions(std::vector<ActionInfo>& out_actions);
    bool send_action(const std::string& key, const std::string& argument);
    bool get_last_action(std::string& out_key, std::string& out_argument);

    // LED API
    bool send_led_settings(int32_t left_r, int32_t left_g, int32_t left_b,
                           int32_t right_r, int32_t right_g, int32_t right_b,
                           int32_t priority, bool has_is_active, bool is_active);

    // Parameters API
    bool get_parameter_value(const std::string& key, std::string& out_value);

    // Last error description (set on failure)
    const std::string& last_error() const { return last_error_; }

private:
    // Streaming thread functions
    void stream_eye_coordinates();
    void stream_eyelid_state();
    void stream_gyroscope();
    void stream_accelerometer();
    void stream_temperature();
    void stream_actions();

    // PIMPL: gRPC channel and stubs
    std::unique_ptr<GrpcClientImpl> impl_;

    // Running flag
    std::atomic<bool> running_;

    // Streaming threads
    std::thread eye_coords_thread_;
    std::thread eyelid_thread_;
    std::thread gyro_thread_;
    std::thread accel_thread_;
    std::thread temp_thread_;
    std::thread actions_thread_;

    // Cached values with mutex protection
    std::mutex eye_coords_mutex_;
    float cached_eye_x_ = 0.0f;
    float cached_eye_y_ = 0.0f;
    bool eye_coords_valid_ = false;

    std::mutex eyelid_mutex_;
    float cached_eyelid_closure_ = 0.0f;
    bool eyelid_valid_ = false;

    std::mutex gyro_mutex_;
    float cached_gyro_x_ = 0.0f;
    float cached_gyro_y_ = 0.0f;
    float cached_gyro_z_ = 0.0f;
    bool gyro_valid_ = false;

    std::mutex accel_mutex_;
    float cached_accel_x_ = 0.0f;
    float cached_accel_y_ = 0.0f;
    float cached_accel_z_ = 0.0f;
    bool accel_valid_ = false;

    std::mutex temp_mutex_;
    float cached_temperature_ = 0.0f;
    bool temp_valid_ = false;

    std::mutex action_mutex_;
    std::string cached_action_key_;
    std::string cached_action_argument_;
    bool action_valid_ = false;

    // LED stream
    std::mutex led_mutex_;
    bool led_stream_open_ = false;

    bool ensure_led_stream();
    void close_led_stream();

    // Last error
    std::string last_error_;
};

#endif // GRPC_CLIENT_H
