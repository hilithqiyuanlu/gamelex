#include "gamelex/core.hpp"
#include <cstdio>
#include <cstdlib>
#include <csetjmp>
#include <cstring>
#include <memory>
#include <jpeglib.h>

namespace gamelex {
namespace {
struct JpegError { jpeg_error_mgr manager; std::jmp_buf jump; };
void jpeg_failed(j_common_ptr object) {
    auto* error = reinterpret_cast<JpegError*>(object->err);
    std::longjmp(error->jump, 1);
}
void jpeg_silent(j_common_ptr) {}
// All state modified after setjmp lives on the heap; no C++ unwinding is needed
// when libjpeg rejects a corrupt screenshot.
struct Codec {
    jpeg_decompress_struct decoder{};
    jpeg_compress_struct encoder{};
    JpegError error{};
    bool decoder_ready = false, encoder_ready = false;
    unsigned char* row = nullptr;
    unsigned char* output = nullptr;
    unsigned long output_size = 0;
    ~Codec() {
        if (encoder_ready) jpeg_destroy_compress(&encoder);
        if (decoder_ready) jpeg_destroy_decompress(&decoder);
        std::free(row); std::free(output);
    }
};
}
Status adopt_jpeg(std::vector<unsigned char>&& jpeg, Image& output) {
    if (jpeg.size()<4 || jpeg.size()>MaxImageBytes || jpeg[jpeg.size()-2]!=0xff || jpeg.back()!=0xd9)
        return Status::error("invalid_image","Screenshot JPEG is incomplete");
    auto state=std::make_unique<Codec>();
    auto* c=state.get();
    c->decoder.err=jpeg_std_error(&c->error.manager);
    c->error.manager.error_exit=jpeg_failed;
    c->error.manager.output_message=jpeg_silent;
    if (setjmp(c->error.jump)) return Status::error("invalid_image","Screenshot header is invalid");
    jpeg_create_decompress(&c->decoder); c->decoder_ready=true;
    jpeg_mem_src(&c->decoder,jpeg.data(),jpeg.size());
    jpeg_read_header(&c->decoder,TRUE);
    PixelRect pixels{};
    const auto status=pixel_crop(Rect{0,0,1,1},c->decoder.image_width,c->decoder.image_height,pixels);
    if (!status.ok()) return status;
    if (c->decoder.progressive_mode || c->decoder.arith_code)
        return Status::error("invalid_image","Only baseline system screenshots are supported");
    output={std::move(jpeg),pixels.width,pixels.height};
    return {};
}
Status crop_jpeg(const std::vector<unsigned char>& jpeg, const Rect& roi, Image& output,
                 const std::function<void()>& sample_memory) {
    if (jpeg.empty() || jpeg.size() > MaxImageBytes)
        return Status::error("invalid_image", "Screenshot size is invalid");
    auto s = validate_crop(roi);
    if (!s.ok()) return s;
    auto state = std::make_unique<Codec>();
    auto* c = state.get();
    c->decoder.err = jpeg_std_error(&c->error.manager);
    c->error.manager.error_exit = jpeg_failed;
    c->error.manager.output_message = jpeg_silent;
    c->encoder.err = &c->error.manager;
    if (setjmp(c->error.jump)) return Status::error("invalid_image", "Unable to decode or crop JPEG screenshot");
    jpeg_create_decompress(&c->decoder); c->decoder_ready = true;
    jpeg_mem_src(&c->decoder, jpeg.data(), jpeg.size());
    jpeg_read_header(&c->decoder, TRUE);
    PixelRect pixels{};
    s = pixel_crop(roi, c->decoder.image_width, c->decoder.image_height, pixels);
    if (!s.ok()) return s;
    // Baseline screenshots only: progressive decoding may allocate a full-frame
    // coefficient buffer and does not fit the measured-memory probe contract.
    if (c->decoder.progressive_mode || c->decoder.arith_code)
        return Status::error("invalid_image", "Progressive or arithmetic JPEG is not supported in the probe");
    c->decoder.out_color_space = JCS_RGB;
    jpeg_start_decompress(&c->decoder);
    c->row = static_cast<unsigned char*>(std::malloc(c->decoder.output_width * 3));
    if (!c->row) return Status::error("out_of_memory", "Unable to allocate screenshot row buffer");
    jpeg_create_compress(&c->encoder); c->encoder_ready = true;
    jpeg_mem_dest(&c->encoder, &c->output, &c->output_size);
    c->encoder.image_width = pixels.width; c->encoder.image_height = pixels.height;
    c->encoder.input_components = 3; c->encoder.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c->encoder); jpeg_set_quality(&c->encoder, 90, TRUE);
    jpeg_start_compress(&c->encoder, TRUE);
    if (sample_memory) sample_memory();
    while (c->decoder.output_scanline < c->decoder.output_height) {
        const int y = c->decoder.output_scanline;
        JSAMPROW row = c->row;
        jpeg_read_scanlines(&c->decoder, &row, 1);
        if (y >= pixels.y && y < pixels.y + pixels.height) {
            JSAMPROW cropped = c->row + pixels.x * 3;
            jpeg_write_scanlines(&c->encoder, &cropped, 1);
            if (sample_memory && y % 64 == 0) sample_memory();
        }
    }
    jpeg_finish_decompress(&c->decoder);
    jpeg_finish_compress(&c->encoder);
    if (sample_memory) sample_memory();
    if (c->output_size > MaxImageBytes) return Status::error("invalid_image", "Cropped image exceeds limit");
    Image result;
    result.jpeg.assign(c->output, c->output + c->output_size);
    result.width = pixels.width; result.height = pixels.height;
    output = std::move(result);
    return {};
}
}
