#include "luniris_lbridge_server.h"
#include "grpc_client.h"
#include "luniris_rpc_ids.h"
#include "luniris_messages.pb.h"
#include "server_log.h"

extern "C" {
#include "lbridge.h"
#include <pb_encode.h>
#include <pb_decode.h>
}

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

// Static instance for callback routing
LunirisLBridgeServer* LunirisLBridgeServer::s_instance = nullptr;

// Time callback for client timeout
static uint64_t get_time_ms(lbridge_context_t)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000 + static_cast<uint64_t>(ts.tv_nsec) / 1000000;
}

// Nonce generation for encryption
#ifdef LBRIDGE_ENABLE_SECURE
static bool generate_nonce(lbridge_context_t, uint8_t out_nonce[12])
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    ssize_t ret = read(fd, out_nonce, 12);
    close(fd);
    return ret == 12;
}
#endif

LunirisLBridgeServer::LunirisLBridgeServer(const Config& config)
    : config_(config)
    , grpc_client_(nullptr)
    , lbridge_ctx_(nullptr)
    , lbridge_srv_(nullptr)
    , running_(false)
{
    s_instance = this;
}

LunirisLBridgeServer::~LunirisLBridgeServer()
{
    stop();

    if (grpc_client_)
    {
        grpc_client_->stop();
    }

    if (lbridge_srv_)
    {
        lbridge_server_destroy(lbridge_srv_);
    }

    if (lbridge_ctx_)
    {
        lbridge_context_destroy(lbridge_ctx_);
    }

    s_instance = nullptr;
}

bool LunirisLBridgeServer::init()
{
    // Create gRPC client and start streaming threads
    grpc_client_ = std::make_unique<GrpcClient>(config_.grpc_address);
    grpc_client_->start();

    // Create LBridge context
    lbridge_context_params params = {};
#ifdef LBRIDGE_ENABLE_SECURE
    params.fp_generate_nonce = generate_nonce;
#endif
    params.fp_malloc = malloc;
    params.fp_free = free;
    params.fp_get_time_ms = get_time_ms;
#ifdef LBRIDGE_ENABLE_LOG
    params.fp_log = [](lbridge_context_t, enum lbridge_log_level level, const char* message) {
        static const char* level_names[] = { "ERROR", "INFO", "TRACE" };
        const char* level_name = (level <= LBRIDGE_LOG_LEVEL_TRACE) ? level_names[level] : "?";
        server_log(level == LBRIDGE_LOG_LEVEL_ERROR, "[LBridge/%s] %s\n", level_name, message);
    };
#endif

    lbridge_ctx_ = lbridge_context_create(&params);
    if (!lbridge_ctx_)
    {
        return false;
    }

    // Create LBridge server
    lbridge_srv_ = lbridge_server_create(lbridge_ctx_, 1024, 65536, on_rpc_call);
    if (!lbridge_srv_)
    {
        return false;
    }

    // Configure client timeout
    if (config_.client_timeout_ms > 0)
    {
        lbridge_server_set_client_timeout(lbridge_srv_, config_.client_timeout_ms);
    }

    // Retrieve LBridge port from parameters service
    std::string port_str;
    if (!grpc_client_->get_parameter_value("lbridge_port", port_str))
    {
        server_log(true, "Failed to get 'port' parameter via gRPC: %s\n", grpc_client_->last_error().c_str());
        return false;
    }

    int port = std::atoi(port_str.c_str());
    if (port <= 0 || port > 65535)
    {
        server_log(true, "Invalid port value from parameters: '%s'\n", port_str.c_str());
        return false;
    }

    server_log(false, "LBridge port from parameters: %d\n", port);

    // Retrieve optional encryption key from parameters service
#ifdef LBRIDGE_ENABLE_SECURE
    std::string secure_key_str;
    if (!grpc_client_->get_parameter_value("lbridge_secure_key", secure_key_str))
    {
        server_log(true, "Failed to get 'lbridge_secure_key' parameter via gRPC: %s\n", grpc_client_->last_error().c_str());
        return false;
    }

    if (!secure_key_str.empty())
    {
        std::memset(encryption_key_, 0, sizeof(encryption_key_));
        size_t copy_len = std::min(secure_key_str.size(), static_cast<size_t>(32));
        std::memcpy(encryption_key_, secure_key_str.data(), copy_len);
        lbridge_activate_encryption(lbridge_srv_, encryption_key_);
        server_log(false, "Encryption enabled (key truncated to %zu bytes)\n", copy_len);
    }
    else
    {
        server_log(false, "Encryption disabled (no secure key)\n");
    }
#endif

    // Start listening
    if (!lbridge_server_listen_tcp(lbridge_srv_, config_.lbridge_address, static_cast<uint16_t>(port), config_.max_clients))
    {
        return false;
    }

    return true;
}

void LunirisLBridgeServer::run()
{
    running_ = true;

    while (running_)
    {
        if (!lbridge_server_update(lbridge_srv_))
        {
			server_log(true, "Error updating LBridge server\n");
            return;
        }

        // Small sleep to avoid busy-waiting
        usleep(1000); // 1ms
    }
}

void LunirisLBridgeServer::stop()
{
    running_ = false;
}

bool LunirisLBridgeServer::on_rpc_call(lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size)
{
    if (!s_instance)
    {
        return false;
    }

    uint16_t rpc_id = lbridge_rpc_context_get_rpc_id(ctx);

    switch (rpc_id)
    {
    case LUNIRIS_RPC_SEND_EYE_COORDINATES:
        return s_instance->handle_send_eye_coordinates(ctx, data, size);
    case LUNIRIS_RPC_SEND_EYELID_STATE:
        return s_instance->handle_send_eyelid_state(ctx, data, size);
    case LUNIRIS_RPC_GET_EYE_COORDINATES:
        return s_instance->handle_get_eye_coordinates(ctx);
    case LUNIRIS_RPC_GET_EYELID_STATE:
        return s_instance->handle_get_eyelid_state(ctx);
    case LUNIRIS_RPC_GET_BRIGHTNESS_LEVEL:
        return s_instance->handle_get_brightness(ctx);
    case LUNIRIS_RPC_SEND_BRIGHTNESS_LEVEL:
        return s_instance->handle_send_brightness(ctx, data, size);
    case LUNIRIS_RPC_GET_GYROSCOPE_VALUES:
        return s_instance->handle_get_gyroscope(ctx);
    case LUNIRIS_RPC_GET_ACCELEROMETER_VALUES:
        return s_instance->handle_get_accelerometer(ctx);
    case LUNIRIS_RPC_GET_TEMPERATURE_VALUE:
        return s_instance->handle_get_temperature(ctx);
    case LUNIRIS_RPC_GET_REGISTERED_ACTIONS:
        return s_instance->handle_get_registered_actions(ctx);
    case LUNIRIS_RPC_GET_ACTION:
        return s_instance->handle_get_action(ctx);
    case LUNIRIS_RPC_SEND_ACTION:
        return s_instance->handle_send_action(ctx, data, size);
    case LUNIRIS_RPC_SEND_LED_SETTINGS:
        return s_instance->handle_send_led_settings(ctx, data, size);
    default:
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INVALID_RPC_ID);
        return false;
    }
}

// =============================================================================
// RPC Handlers
// =============================================================================

bool LunirisLBridgeServer::handle_send_eye_coordinates(lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size)
{
    (void)ctx; // Fire-and-forget, no response needed

    EyeCoordinates coords = EyeCoordinates_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, size);

    if (!pb_decode(&stream, EyeCoordinates_fields, &coords))
    {
        return false;
    }

    const bool result = grpc_client_->set_eye_coordinates(coords.x, coords.y);
    if (!result)
    {
        server_log(true, "send_eye_coordinates failed: %s\n", grpc_client_->last_error().c_str());
    }
    return result;
}

bool LunirisLBridgeServer::handle_send_eyelid_state(lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size)
{
    (void)ctx;

    EyelidState state = EyelidState_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, size);

    if (!pb_decode(&stream, EyelidState_fields, &state))
    {
        return false;
    }

    const bool result = grpc_client_->set_eyelid_state(state.closure_percentage);
    if (!result)
    {
        server_log(true, "send_eyelid_state failed: %s\n", grpc_client_->last_error().c_str());
    }
    return result;
}

bool LunirisLBridgeServer::handle_get_eye_coordinates(lbridge_rpc_context* ctx)
{
    float x, y;
    const bool got_coords = grpc_client_->get_eye_coordinates(x, y);
    if (!got_coords)
    {
        server_log(true, "get_eye_coordinates failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    EyeCoordinates coords = EyeCoordinates_init_zero;
    coords.x = x;
    coords.y = y;

    uint8_t buffer[EyeCoordinates_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, EyeCoordinates_fields, &coords))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_get_eyelid_state(lbridge_rpc_context* ctx)
{
    float closure;
    const bool got_state = grpc_client_->get_eyelid_state(closure);
    if (!got_state)
    {
        server_log(true, "get_eyelid_state failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    EyelidState state = EyelidState_init_zero;
    state.closure_percentage = closure;

    uint8_t buffer[EyelidState_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, EyelidState_fields, &state))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_get_brightness(lbridge_rpc_context* ctx)
{
    int32_t level;
    const bool got_brightness = grpc_client_->get_brightness(level);
    if (!got_brightness)
    {
        server_log(true, "get_brightness failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    BrightnessMessage msg = BrightnessMessage_init_zero;
    msg.brightness_level = level;

    uint8_t buffer[BrightnessMessage_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, BrightnessMessage_fields, &msg))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_send_brightness(lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size)
{
    (void)ctx;

    BrightnessMessage msg = BrightnessMessage_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, size);

    if (!pb_decode(&stream, BrightnessMessage_fields, &msg))
    {
        return false;
    }

    const bool result = grpc_client_->set_brightness(msg.brightness_level);
    if (!result)
    {
        server_log(true, "send_brightness failed: %s\n", grpc_client_->last_error().c_str());
    }
    return result;
}

bool LunirisLBridgeServer::handle_get_gyroscope(lbridge_rpc_context* ctx)
{
    float x, y, z;
    const bool got_gyro = grpc_client_->get_gyroscope(x, y, z);
    if (!got_gyro)
    {
        server_log(true, "get_gyroscope failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    InertialMeasurementValues values = InertialMeasurementValues_init_zero;
    values.x = x;
    values.y = y;
    values.z = z;

    uint8_t buffer[InertialMeasurementValues_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, InertialMeasurementValues_fields, &values))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_get_accelerometer(lbridge_rpc_context* ctx)
{
    float x, y, z;
    const bool got_accel = grpc_client_->get_accelerometer(x, y, z);
    if (!got_accel)
    {
        server_log(true, "get_accelerometer failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    InertialMeasurementValues values = InertialMeasurementValues_init_zero;
    values.x = x;
    values.y = y;
    values.z = z;

    uint8_t buffer[InertialMeasurementValues_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, InertialMeasurementValues_fields, &values))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_get_temperature(lbridge_rpc_context* ctx)
{
    float temp_celsius;
    const bool got_temp = grpc_client_->get_temperature(temp_celsius);
    if (!got_temp)
    {
        server_log(true, "get_temperature failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    TemperatureValue value = TemperatureValue_init_zero;
    value.temperature = temp_celsius;

    uint8_t buffer[TemperatureValue_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, TemperatureValue_fields, &value))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_get_registered_actions(lbridge_rpc_context* ctx)
{
    std::vector<ActionInfo> action_list;
    const bool got_actions = grpc_client_->get_registered_actions(action_list);
    if (!got_actions)
    {
        server_log(true, "get_registered_actions failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    Actions actions = Actions_init_zero;
    actions.actions_count = static_cast<pb_size_t>(action_list.size());

    for (size_t i = 0; i < action_list.size() && i < LUNIRIS_ACTIONS_MAX_COUNT; i++)
    {
        const auto& info = action_list[i];
        strncpy(actions.actions[i].key, info.key.c_str(), LUNIRIS_ACTION_KEY_MAX_SIZE);
        strncpy(actions.actions[i].name, info.name.c_str(), LUNIRIS_ACTION_NAME_MAX_SIZE);
        actions.actions[i].is_available = info.is_available;
        actions.actions[i].source = static_cast<int32_t>(info.source);
    }

    uint8_t* buffer = static_cast<uint8_t*>(malloc(Actions_size));
    if (!buffer)
    {
        return false;
    }

    pb_ostream_t stream = pb_ostream_from_buffer(buffer, Actions_size);
    bool result = false;

    if (pb_encode(&stream, Actions_fields, &actions))
    {
        result = lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
    }

    free(buffer);
    return result;
}

bool LunirisLBridgeServer::handle_get_action(lbridge_rpc_context* ctx)
{
    std::string key, argument;
    const bool got_action = grpc_client_->get_last_action(key, argument);
    if (!got_action)
    {
        server_log(true, "get_action failed: %s\n", grpc_client_->last_error().c_str());
        lbridge_rpc_context_send_error(ctx, LBRIDGE_PROTOCOL_ERROR_INTERNAL);
        return false;
    }

    ActionMessage msg = ActionMessage_init_zero;
    strncpy(msg.key, key.c_str(), LUNIRIS_ACTION_KEY_MAX_SIZE);
    strncpy(msg.argument, argument.c_str(), LUNIRIS_ACTION_ARG_MAX_SIZE);

    uint8_t buffer[ActionMessage_size];
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    if (!pb_encode(&stream, ActionMessage_fields, &msg))
    {
        return false;
    }

    return lbridge_rpc_context_send_response(ctx, buffer, static_cast<uint32_t>(stream.bytes_written));
}

bool LunirisLBridgeServer::handle_send_action(lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size)
{
    (void)ctx;

    ActionMessage msg = ActionMessage_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, size);

    if (!pb_decode(&stream, ActionMessage_fields, &msg))
    {
        return false;
    }

    const bool result = grpc_client_->send_action(msg.key, msg.argument);
    if (!result)
    {
        server_log(true, "send_action failed: %s\n", grpc_client_->last_error().c_str());
    }
    return result;
}

bool LunirisLBridgeServer::handle_send_led_settings(lbridge_rpc_context* ctx, const uint8_t* data, uint32_t size)
{
    (void)ctx;

    LedSettings settings = LedSettings_init_zero;
    pb_istream_t stream = pb_istream_from_buffer(data, size);

    if (!pb_decode(&stream, LedSettings_fields, &settings))
    {
        return false;
    }

    Color left = settings.has_left_led ? settings.left_led : (Color){0, 0, 0};
    Color right = settings.has_right_led ? settings.right_led : (Color){0, 0, 0};

    const bool result = grpc_client_->send_led_settings(
        left.r, left.g, left.b,
        right.r, right.g, right.b,
        settings.priority_level,
        settings.has_is_active, settings.is_active);

    if (!result)
    {
        server_log(true, "send_led_settings failed: %s\n", grpc_client_->last_error().c_str());
    }
    return result;
}
