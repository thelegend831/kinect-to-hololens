#include <algorithm>
#include <iostream>
#include <asio.hpp>
#include "helper/soundio_helper.h"

namespace kh
{
int main(std::string ip_address, int port)
{
    const double MICROPHONE_LATENCY = 0.2; // seconds

    auto audio = Audio::create();
    if (!audio) {
        printf("out of memory\n");
        return 1;
    }
    int err = audio->connect();
    if (err) {
        printf("error connecting: %s\n", soundio_strerror(err));
        return 1;
    }
    audio->flushEvents();

    for (int i = 0; i < audio->getInputDeviceCount(); ++i) {
        printf("input_device[%d]: %s\n", i, audio->getInputDevice(i)->name());
    }
    for (int i = 0; i < audio->getOutputDeviceCount(); ++i) {
        printf("output_device[%d]: %s\n", i, audio->getOutputDevice(i)->name());
    }

    int default_out_device_index = audio->getDefaultOutputDeviceIndex();
    if (default_out_device_index < 0) {
        printf("no output device found\n");
        return 1;
    }

    auto out_device = audio->getOutputDevice(default_out_device_index);
    if (!out_device) {
        printf("could not get output device: out of memory\n");
        return 1;
    }

    const int K4AMicrophoneSampleRate = 48000;

    auto out_stream = AudioOutStream::create(*out_device);
    if (!out_stream) {
        printf("out of memory\n");
        return 1;
    }
    // These settings are those generic and similar to Azure Kinect's.
    // It is set to be Stereo, which is the default setting of Unity3D.
    out_stream->set_format(SoundIoFormatFloat32LE);
    out_stream->set_sample_rate(K4AMicrophoneSampleRate);
    out_stream->set_layout(*soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdStereo));
    out_stream->set_software_latency(MICROPHONE_LATENCY);
    out_stream->set_write_callback(libsoundio::helper::write_callback);
    out_stream->set_underflow_callback(libsoundio::helper::underflow_callback);
    if (err = out_stream->open()) {
        printf("unable to open output stream: %s\n", soundio_strerror(err));
        return 1;
    }

    //int capacity = microphone_latency * 2 * in_stream->ptr()->sample_rate * in_stream->ptr()->bytes_per_frame;
    // While the Azure Kinect is set to have 7.0 channel layout, which has 7 channels, only two of them gets used.
    const int STEREO_CHANNEL_COUNT = 2;
    //int capacity = MICROPHONE_LATENCY * 2 * in_stream->sample_rate() * in_stream->bytes_per_sample() * STEREO_CHANNEL_COUNT;
    int capacity = MICROPHONE_LATENCY * 2 * out_stream->sample_rate() * out_stream->bytes_per_sample() * STEREO_CHANNEL_COUNT;
    libsoundio::helper::ring_buffer = soundio_ring_buffer_create(audio->ptr(), capacity);
    if (!libsoundio::helper::ring_buffer) {
        printf("unable to create ring buffer: out of memory\n");
    }
    char* buf = soundio_ring_buffer_write_ptr(libsoundio::helper::ring_buffer);
    int fill_count = MICROPHONE_LATENCY * out_stream->sample_rate() * out_stream->bytes_per_frame();
    memset(buf, 0, fill_count);
    soundio_ring_buffer_advance_write_ptr(libsoundio::helper::ring_buffer, fill_count);

    if (err = out_stream->start()) {
        printf("unable to start output device: %s\n", soundio_strerror(err));
        return 1;
    }

    asio::io_context io_context;
    asio::ip::udp::socket socket(io_context);
    socket.open(asio::ip::udp::v4());
    socket.non_blocking(true);
    asio::ip::udp::endpoint remote_endpoint(asio::ip::address::from_string(ip_address), port);
    std::array<char, 1> send_buf = { { 0 } };
    socket.send_to(asio::buffer(send_buf), remote_endpoint);

    printf("start for loop\n");
    for (;;) {
        audio->flushEvents();
        char* write_ptr = soundio_ring_buffer_write_ptr(libsoundio::helper::ring_buffer);
        int free_bytes = soundio_ring_buffer_free_count(libsoundio::helper::ring_buffer);
        int left_bytes = free_bytes;

        int cursor = 0;
        std::error_code error;
        while(left_bytes > 0) {
            std::vector<uint8_t> packet(1500);
            size_t packet_size = socket.receive_from(asio::buffer(packet), remote_endpoint, 0, error);

            if (error)
                break;

            memcpy(write_ptr + cursor, packet.data(), packet_size);

            cursor += packet_size;
            left_bytes -= packet_size;
        }
        int fill_bytes = soundio_ring_buffer_fill_count(libsoundio::helper::ring_buffer);
        printf("free_bytes: %d, fill_bytes: %d\n", free_bytes, fill_bytes);

        soundio_ring_buffer_advance_write_ptr(libsoundio::helper::ring_buffer, cursor);
    }
    return 0;
}
}

int main()
{
    return kh::main("127.0.0.1", 7777);
}