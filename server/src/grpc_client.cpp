#include "grpc_client.h"
#include "server_log.h"

#include <grpcpp/grpcpp.h>
#include "luniris_service.grpc.pb.h"

static const char* grpc_state_name(grpc_connectivity_state state)
{
    switch (state)
    {
    case GRPC_CHANNEL_IDLE:             return "IDLE";
    case GRPC_CHANNEL_CONNECTING:       return "CONNECTING";
    case GRPC_CHANNEL_READY:            return "READY";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:return "TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:         return "SHUTDOWN";
    default:                            return "UNKNOWN";
    }
}

static void log_grpc_error(const char* method, const grpc::Status& status)
{
    server_log(true, "%s failed: code=%d (%s), message=%s\n",
        method,
        static_cast<int>(status.error_code()),
        status.error_message().c_str(),
        status.error_details().empty() ? "(no details)" : status.error_details().c_str());
}

// PIMPL: holds gRPC channel and service stubs
struct GrpcClientImpl
{
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<EyePositionService::Stub> eye_stub;
    std::unique_ptr<ImuService::Stub> imu_stub;
    std::unique_ptr<DisplayService::Stub> display_stub;
    std::unique_ptr<ActionsService::Stub> actions_stub;
    std::unique_ptr<LedService::Stub> led_stub;
    std::unique_ptr<ParametersService::Stub> parameters_stub;

    // LED client-streaming state
    std::unique_ptr<grpc::ClientContext> led_context;
    std::unique_ptr<grpc::ClientWriter<LedSettings>> led_writer;
    google::protobuf::Empty led_response;

    static std::shared_ptr<grpc::Channel> create_channel(const std::string& address)
    {
        grpc::ChannelArguments args;
        // Override the :authority header to "localhost" instead of the URL-encoded
        // socket path (tmp%2Ffeatures.socket). The Luniris daemon (ASP.NET Core/Kestrel)
        // expects "localhost" as the host, which is what C#/Node.js gRPC clients send.
        args.SetString(GRPC_ARG_DEFAULT_AUTHORITY, "localhost");
        return grpc::CreateCustomChannel(address, grpc::InsecureChannelCredentials(), args);
    }

    explicit GrpcClientImpl(const std::string& address)
        : channel(create_channel(address))
        , eye_stub(EyePositionService::NewStub(channel))
        , imu_stub(ImuService::NewStub(channel))
        , display_stub(DisplayService::NewStub(channel))
        , actions_stub(ActionsService::NewStub(channel))
        , led_stub(LedService::NewStub(channel))
        , parameters_stub(ParametersService::NewStub(channel))
    {
    }
};

GrpcClient::GrpcClient(const std::string& address)
    : impl_(std::make_unique<GrpcClientImpl>(address))
    , running_(false)
{
}

GrpcClient::~GrpcClient()
{
    stop();
}

void GrpcClient::start()
{
    if (running_)
        return;

    running_ = true;

    auto state = impl_->channel->GetState(true);
    server_log(false, "gRPC channel state: %s\n", grpc_state_name(state));

    eye_coords_thread_ = std::thread(&GrpcClient::stream_eye_coordinates, this);
    eyelid_thread_ = std::thread(&GrpcClient::stream_eyelid_state, this);
    gyro_thread_ = std::thread(&GrpcClient::stream_gyroscope, this);
    accel_thread_ = std::thread(&GrpcClient::stream_accelerometer, this);
    temp_thread_ = std::thread(&GrpcClient::stream_temperature, this);
    actions_thread_ = std::thread(&GrpcClient::stream_actions, this);
}

void GrpcClient::stop()
{
    if (!running_)
        return;

    running_ = false;

    close_led_stream();

    // Threads will exit when stream ends or running_ becomes false
    if (eye_coords_thread_.joinable()) eye_coords_thread_.join();
    if (eyelid_thread_.joinable()) eyelid_thread_.join();
    if (gyro_thread_.joinable()) gyro_thread_.join();
    if (accel_thread_.joinable()) accel_thread_.join();
    if (temp_thread_.joinable()) temp_thread_.join();
    if (actions_thread_.joinable()) actions_thread_.join();
}

// =============================================================================
// Streaming Threads
// =============================================================================

void GrpcClient::stream_eye_coordinates()
{
    while (running_)
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        EyeCoordinates response;

        auto reader = impl_->eye_stub->StreamEyeCoordinatesToClient(&context, request);

        while (running_ && reader->Read(&response))
        {
            std::lock_guard<std::mutex> lock(eye_coords_mutex_);
            cached_eye_x_ = response.x();
            cached_eye_y_ = response.y();
            eye_coords_valid_ = true;
        }

        // Stream ended, retry after a short delay
        if (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void GrpcClient::stream_eyelid_state()
{
    while (running_)
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        EyelidState response;

        auto reader = impl_->eye_stub->StreamEyelidStateToClient(&context, request);

        while (running_ && reader->Read(&response))
        {
            std::lock_guard<std::mutex> lock(eyelid_mutex_);
            cached_eyelid_closure_ = response.closure_percentage();
            eyelid_valid_ = true;
        }

        if (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void GrpcClient::stream_gyroscope()
{
    while (running_)
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        InertialMeasurementValues response;

        auto reader = impl_->imu_stub->StreamGyroscopeValues(&context, request);

        while (running_ && reader->Read(&response))
        {
            std::lock_guard<std::mutex> lock(gyro_mutex_);
            cached_gyro_x_ = response.x();
            cached_gyro_y_ = response.y();
            cached_gyro_z_ = response.z();
            gyro_valid_ = true;
        }

        if (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void GrpcClient::stream_accelerometer()
{
    while (running_)
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        InertialMeasurementValues response;

        auto reader = impl_->imu_stub->StreamAccelerometerValues(&context, request);

        while (running_ && reader->Read(&response))
        {
            std::lock_guard<std::mutex> lock(accel_mutex_);
            cached_accel_x_ = response.x();
            cached_accel_y_ = response.y();
            cached_accel_z_ = response.z();
            accel_valid_ = true;
        }

        if (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void GrpcClient::stream_temperature()
{
    while (running_)
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        TemperatureValue response;

        auto reader = impl_->imu_stub->StreamTemperatureValues(&context, request);

        while (running_ && reader->Read(&response))
        {
            std::lock_guard<std::mutex> lock(temp_mutex_);
            cached_temperature_ = response.temperature();
            temp_valid_ = true;
        }

        if (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void GrpcClient::stream_actions()
{
    while (running_)
    {
        grpc::ClientContext context;
        google::protobuf::Empty request;
        ActionMessage response;

        auto reader = impl_->actions_stub->StreamActions(&context, request);

        while (running_ && reader->Read(&response))
        {
            std::lock_guard<std::mutex> lock(action_mutex_);
            cached_action_key_ = response.key();
            cached_action_argument_ = response.argument();
            action_valid_ = true;
        }

        if (running_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// =============================================================================
// Eye Position API
// =============================================================================

bool GrpcClient::get_eye_coordinates(float& out_x, float& out_y)
{
    std::lock_guard<std::mutex> lock(eye_coords_mutex_);
    if (!eye_coords_valid_)
    {
        last_error_ = "stream data not available";
        return false;
    }
    out_x = cached_eye_x_;
    out_y = cached_eye_y_;
    return true;
}

bool GrpcClient::get_eyelid_state(float& out_closure)
{
    std::lock_guard<std::mutex> lock(eyelid_mutex_);
    if (!eyelid_valid_)
    {
        last_error_ = "stream data not available";
        return false;
    }
    out_closure = cached_eyelid_closure_;
    return true;
}

bool GrpcClient::set_eye_coordinates(float x, float y)
{
    EyeCoordinates request;
    request.set_x(x);
    request.set_y(y);

    google::protobuf::Empty response;
    grpc::ClientContext context;

    grpc::Status status = impl_->eye_stub->SendEyeCoordinates(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("SendEyeCoordinates", status);
        last_error_ = status.error_message();
        return false;
    }
    return true;
}

bool GrpcClient::set_eyelid_state(float closure)
{
    EyelidState request;
    request.set_closure_percentage(closure);

    google::protobuf::Empty response;
    grpc::ClientContext context;

    grpc::Status status = impl_->eye_stub->SendEyelidState(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("SendEyelidState", status);
        last_error_ = status.error_message();
        return false;
    }
    return true;
}

// =============================================================================
// Display API
// =============================================================================

bool GrpcClient::get_brightness(int32_t& out_level)
{
    google::protobuf::Empty request;
    BrightnessMessage response;
    grpc::ClientContext context;

    grpc::Status status = impl_->display_stub->GetBrightnessLevel(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("GetBrightnessLevel", status);
        last_error_ = status.error_message();
        return false;
    }
    out_level = response.brightness_level();
    return true;
}

bool GrpcClient::set_brightness(int32_t level)
{
    BrightnessMessage request;
    request.set_brightness_level(level);

    google::protobuf::Empty response;
    grpc::ClientContext context;

    grpc::Status status = impl_->display_stub->SendBrightnessLevel(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("SendBrightnessLevel", status);
        last_error_ = status.error_message();
        return false;
    }
    return true;
}

// =============================================================================
// IMU API
// =============================================================================

bool GrpcClient::get_gyroscope(float& out_x, float& out_y, float& out_z)
{
    std::lock_guard<std::mutex> lock(gyro_mutex_);
    if (!gyro_valid_)
    {
        last_error_ = "stream data not available";
        return false;
    }
    out_x = cached_gyro_x_;
    out_y = cached_gyro_y_;
    out_z = cached_gyro_z_;
    return true;
}

bool GrpcClient::get_accelerometer(float& out_x, float& out_y, float& out_z)
{
    std::lock_guard<std::mutex> lock(accel_mutex_);
    if (!accel_valid_)
    {
        last_error_ = "stream data not available";
        return false;
    }
    out_x = cached_accel_x_;
    out_y = cached_accel_y_;
    out_z = cached_accel_z_;
    return true;
}

bool GrpcClient::get_temperature(float& out_celsius)
{
    std::lock_guard<std::mutex> lock(temp_mutex_);
    if (!temp_valid_)
    {
        last_error_ = "stream data not available";
        return false;
    }
    out_celsius = cached_temperature_;
    return true;
}

// =============================================================================
// Actions API
// =============================================================================

bool GrpcClient::get_registered_actions(std::vector<ActionInfo>& out_actions)
{
    google::protobuf::Empty request;
    grpc::ClientContext context;
    Actions response;

    grpc::Status status = impl_->actions_stub->GetRegisteredActions(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("GetRegisteredActions", status);
        last_error_ = status.error_message();
        return false;
    }

    out_actions.clear();
    out_actions.reserve(response.actions_size());

    for (int i = 0; i < response.actions_size(); i++)
    {
        const auto& action = response.actions(i);
        ActionInfo info;
        info.key = action.key();
        info.name = action.name();
        info.is_available = action.is_available();
        info.source = static_cast<int>(action.source());
        out_actions.push_back(std::move(info));
    }

    return true;
}

bool GrpcClient::send_action(const std::string& key, const std::string& argument)
{
    ActionMessage request;
    request.set_key(key);
    request.set_argument(argument);

    google::protobuf::Empty response;
    grpc::ClientContext context;

    grpc::Status status = impl_->actions_stub->SendAction(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("SendAction", status);
        last_error_ = status.error_message();
        return false;
    }
    return true;
}

bool GrpcClient::get_last_action(std::string& out_key, std::string& out_argument)
{
    std::lock_guard<std::mutex> lock(action_mutex_);
    if (!action_valid_)
    {
        last_error_ = "stream data not available";
        return false;
    }
    out_key = cached_action_key_;
    out_argument = cached_action_argument_;
    return true;
}

// =============================================================================
// LED API
// =============================================================================

bool GrpcClient::ensure_led_stream()
{
    if (led_stream_open_)
        return true;

    impl_->led_context = std::make_unique<grpc::ClientContext>();
    impl_->led_writer = impl_->led_stub->StreamLedSettings(
        impl_->led_context.get(), &impl_->led_response);

    if (!impl_->led_writer)
    {
        last_error_ = "Failed to open LED stream";
        impl_->led_context.reset();
        return false;
    }

    led_stream_open_ = true;
    server_log(false, "LED stream opened\n");
    return true;
}

void GrpcClient::close_led_stream()
{
    std::lock_guard<std::mutex> lock(led_mutex_);
    if (!led_stream_open_)
        return;

    if (impl_->led_writer)
    {
        impl_->led_writer->WritesDone();
        impl_->led_writer->Finish();
    }
    impl_->led_writer.reset();
    impl_->led_context.reset();
    led_stream_open_ = false;
    server_log(false, "LED stream closed\n");
}

bool GrpcClient::send_led_settings(int32_t left_r, int32_t left_g, int32_t left_b,
                                   int32_t right_r, int32_t right_g, int32_t right_b,
                                   int32_t priority, bool has_is_active, bool is_active)
{
    std::lock_guard<std::mutex> lock(led_mutex_);

    if (!ensure_led_stream())
        return false;

    LedSettings request;
    Color* left = request.mutable_left_led();
    left->set_r(left_r);
    left->set_g(left_g);
    left->set_b(left_b);

    Color* right = request.mutable_right_led();
    right->set_r(right_r);
    right->set_g(right_g);
    right->set_b(right_b);

    request.set_priority_level(priority);
    if (has_is_active)
        request.set_is_active(is_active);

    if (!impl_->led_writer->Write(request))
    {
        server_log(true, "LED stream write failed, reopening...\n");
        impl_->led_writer.reset();
        impl_->led_context.reset();
        led_stream_open_ = false;

        if (!ensure_led_stream())
            return false;

        if (!impl_->led_writer->Write(request))
        {
            last_error_ = "LED stream write failed after reconnect";
            return false;
        }
    }

    return true;
}

// =============================================================================
// Parameters API
// =============================================================================

bool GrpcClient::get_parameter_value(const std::string& key, std::string& out_value)
{
    Parameter request;
    request.set_key(key);

    Parameter response;
    grpc::ClientContext context;

    const char* token = std::getenv("TOKEN");
    if (!token)
    {
        last_error_ = "TOKEN env variable not set";
        server_log(true, "GetParameterValue: TOKEN env variable not set!\n");
        return false;
    }
    context.AddMetadata("token", token);

    grpc::Status status = impl_->parameters_stub->GetParameterValue(&context, request, &response);
    if (!status.ok())
    {
        log_grpc_error("GetParameterValue", status);
        last_error_ = status.error_message();
        return false;
    }
    out_value = response.value();
    return true;
}
