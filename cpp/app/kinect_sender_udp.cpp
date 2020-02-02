#include <chrono>
#include "kh_core.h"
#include "kh_sender.h"
#include "k4a/k4a.hpp"
#include "kh_vp8.h"
#include "kh_trvl.h"
#include "kh_sender_udp.h"

namespace kh
{
int pow_of_two(int exp) {
    assert(exp >= 0);

    int res = 1;
    for (int i = 0; i < exp; ++i) {
        res *= 2;
    }
    return res;
}

class KinectDevice
{
private:
    KinectDevice(k4a::device&& device, std::chrono::milliseconds timeout)
        : device_(std::move(device)), timeout_(timeout)
    {
    }

public:
    static KinectDevice create(std::chrono::milliseconds time_out)
    {
        auto device = k4a::device::open(K4A_DEVICE_DEFAULT);
        return KinectDevice(std::move(device), time_out);
    }

    void start(k4a_device_configuration_t configuration)
    {
        configuration_ = configuration;
        device_.start_cameras(&configuration);
    }

    k4a::calibration getCalibration()
    {
        return device_.get_calibration(configuration_.depth_mode,
                                       configuration_.color_resolution);
    }

    std::optional<k4a::capture> getCapture()
    {
        k4a::capture capture;
        if (!device_.get_capture(&capture, timeout_))
            return std::nullopt;

        return capture;
    }

private:
    k4a::device device_;
    std::chrono::milliseconds timeout_;
    k4a_device_configuration_t configuration_;
};

// Sends Azure Kinect frames through a TCP port.
void _send_frames(int port, bool binned_depth)
{
    const int TARGET_BITRATE = 2000;
    const short CHANGE_THRESHOLD = 10;
    const int INVALID_THRESHOLD = 2;
    const auto TIMEOUT = std::chrono::milliseconds(1000);

    printf("Start sending Frames (port: %d, binned_depth: %d)\n", port, binned_depth);

    //auto device = k4a::device::open(K4A_DEVICE_DEFAULT);
    auto device = KinectDevice::create(TIMEOUT);

    k4a_device_configuration_t configuration = K4A_DEVICE_CONFIG_INIT_DISABLE_ALL;
    configuration.color_format = K4A_IMAGE_FORMAT_COLOR_BGRA32;
    configuration.color_resolution = K4A_COLOR_RESOLUTION_720P;
    configuration.depth_mode = binned_depth ? K4A_DEPTH_MODE_NFOV_2X2BINNED : K4A_DEPTH_MODE_NFOV_UNBINNED;
    //device.start_cameras(&configuration);
    device.start(configuration);

    //auto calibration = device.get_calibration(configuration.depth_mode, configuration.color_resolution);
    auto calibration = device.getCalibration();
    k4a::transformation transformation(calibration);

    int depth_width = calibration.depth_camera_calibration.resolution_width;
    int depth_height = calibration.depth_camera_calibration.resolution_height;
    
    // Color encoder also uses the depth width/height since color pixels get transformed to the depth camera.
    Vp8Encoder color_encoder(depth_width, depth_height, TARGET_BITRATE);
    TrvlEncoder depth_encoder(depth_width * depth_height, CHANGE_THRESHOLD, INVALID_THRESHOLD);

    asio::io_context io_context;
    asio::ip::udp::socket socket(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), port));

    std::array<char, 1> recv_buf;
    asio::ip::udp::endpoint remote_endpoint;
    std::error_code error;
    socket.receive_from(asio::buffer(recv_buf), remote_endpoint, 0, error);
    socket.non_blocking(true);

    if (error) {
        std::cout << "Error receiving remote_endpoint: " << error.message() << std::endl;
        return;
    }

    printf("Found a Receiver at %s:%d\n", remote_endpoint.address().to_string().c_str(), remote_endpoint.port());

    // Sender is a class that will use the socket to send frames to the receiver that has the socket connected to this socket.
    SenderUdp sender(std::move(socket), remote_endpoint);
    sender.send(calibration);

    // frame_id is the ID of the frame the sender sends.
    int frame_id = 0;
    // receiver_frame_id is the ID that the receiver sent back saying it received the frame of that ID.
    int receiver_frame_id = 0;

    // Variables for profiling the sender.
    auto summary_start = std::chrono::system_clock::now();
    int frame_count = 0;
    std::chrono::microseconds latest_time_stamp;

    size_t frame_size = 0;
    for (;;) {
        auto frame_start = std::chrono::steady_clock::now();

        auto receive_result = sender.receive();
        if (receive_result) {
            uint8_t message_type = (*receive_result)[0];
            memcpy(&receiver_frame_id, receive_result->data() + 1, 4);
        }

        auto capture_start = std::chrono::steady_clock::now();
        //k4a::capture capture;
        //if (!device.get_capture(&capture, TIMEOUT)) {
        //    std::cout << "get_capture() timed out" << std::endl;
        //    continue;
        //}

        auto capture = device.getCapture();
        if (!capture)
            continue;

        auto color_image = capture->get_color_image();
        if (!color_image) {
            std::cout << "no color_image" << std::endl;
            continue;
        }

        int frame_id_diff = frame_id - receiver_frame_id;

        auto time_stamp = color_image.get_device_timestamp();
        auto time_diff = time_stamp - latest_time_stamp;
        // Rounding assuming that the framerate is 30 Hz.
        int device_frame_diff = (int)(time_diff.count() / 33000.0f + 0.5f);

        // Skip frame if the receiver is struggling.
        // if frame_id_diff == 1 or 2 -> don't skip
        // if frame_id_diff == n -> skip 2 ^ (n - 3) frames.
        // Do not test for the first frame.
        if (frame_id != 0 && device_frame_diff < pow_of_two(frame_id_diff - 1) / 4) {
            continue;
        }

        auto depth_image = capture->get_depth_image();
        if (!depth_image) {
            std::cout << "no depth_image" << std::endl;
            continue;
        }

        float frame_time_stamp = time_stamp.count() / 1000.0f;

        auto transformation_start = std::chrono::steady_clock::now();
        auto transformed_color_image = transformation.color_image_to_depth_camera(depth_image, color_image);

        auto compression_start = std::chrono::steady_clock::now();
        // Format the color pixels from the Kinect for the Vp8Encoder then encode the pixels with Vp8Encoder.
        auto yuv_image = createYuvImageFromAzureKinectBgraBuffer(transformed_color_image.get_buffer(),
                                                                 transformed_color_image.get_width_pixels(),
                                                                 transformed_color_image.get_height_pixels(),
                                                                 transformed_color_image.get_stride_bytes());
        auto vp8_frame = color_encoder.encode(yuv_image);

        auto depth_compression_start = std::chrono::steady_clock::now();
        // Compress the depth pixels.
        auto depth_encoder_frame = depth_encoder.encode(reinterpret_cast<short*>(depth_image.get_buffer()));

        auto message_start = std::chrono::steady_clock::now();

        auto send_start = std::chrono::steady_clock::now();

        sender.send(frame_id, frame_time_stamp, vp8_frame,
                    reinterpret_cast<uint8_t*>(depth_encoder_frame.data()), depth_encoder_frame.size());

        auto frame_end = std::chrono::steady_clock::now();

        // Per frame log.
        //int byte_size = vp8_frame.size() + depth_encoder_frame.size();
        //auto frame_time_diff = frame_end - frame_start;
        //auto capture_time_diff = compression_start - capture_start;
        //auto transformation_time = compression_start - transformation_start;
        //auto compression_time = frame_end - compression_start;
        //auto color_compression_time = depth_compression_start - compression_start;
        //auto depth_compression_time = message_start - depth_compression_start;
        //auto message_time = send_start - message_start;
        //auto send_time = frame_end - send_start;

        //std::cout << "frame_id: " << frame_id << ", "
        //          << "frame_id_diff: " << frame_id_diff << ", "
                  //<< "byte_size: " << (byte_size / 1024) << " KB, "
                  //<< "frame_time_diff: " << (frame_time_diff.count() / 1000000) << " ms, "
                  //<< "capture_time_diff: " << (capture_time_diff.count() / 1000000) << " ms, "
                  //<< "transformation_time: " << (transformation_time.count() / 1000000) << " ms, "
                  //<< "compression_time: " << (compression_time.count() / 1000000) << " ms, "
                  //<< "color_compression_time: " << (color_compression_time.count() / 1000000) << " ms, "
                  //<< "depth_compression_time: " << (depth_compression_time.count() / 1000000) << " ms, "
                  //<< "send_time: " << (send_time.count() / 1000000) << " ms, "
                  //<< "time_diff: " << (time_diff.count() / 1000) << " ms, "
                  //<< "device_time_stamp: " << (time_stamp.count() / 1000) << " ms, "
                  //<< std::endl;

        latest_time_stamp = time_stamp;

        // Print profile measures every 100 frames.
        if (frame_id % 100 == 0) {
            auto summary_end = std::chrono::system_clock::now();
            std::chrono::duration<double> diff = summary_end - summary_start;
            std::stringstream ss;
            ss << "Summary for frame " << frame_id << ", "
                << "FPS: " << frame_count / diff.count() << ", "
                << "Bandwidth: " << frame_size / (diff.count() * 131072) << " Mbps. "; // 131072 = 1024 * 1024 / 8
             // White spaces are added at the end to make sure to clean up the previous line.
             // std::cout << ss.str() << "       \r";
            std::cout << ss.str() << std::endl;
            summary_start = summary_end;
            frame_count = 0;
            frame_size = 0;
        }

        ++frame_id;

        // Updating variables for profiling.
        ++frame_count;
        frame_size += vp8_frame.size() + depth_encoder_frame.size();
    }

    std::cout << "Stopped sending Kinect frames." << std::endl;
}

// Repeats collecting the port number from the user and calling _send_frames() with it.
void send_frames()
{
    for (;;) {
        std::cout << "Enter a port number to start sending frames: ";
        std::string line;
        std::getline(std::cin, line);
        // The default port (the port when nothing is entered) is 7777.
        int port = line.empty() ? 7777 : std::stoi(line);

        std::cout << "Choose depth resolution (1: Full, 2: Half): ";
        std::getline(std::cin, line);

        // The default type is TRVL.
        bool binned_depth = false;
        if (line == "2")
            binned_depth = true;

        try {
            _send_frames(port, binned_depth);
        } catch (std::exception & e) {
            std::cout << e.what() << std::endl;
        }
    }
}
}

int main()
{
    kh::send_frames();
    return 0;
}