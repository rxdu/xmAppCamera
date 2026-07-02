// GstCompressedDecoder tests — fully self-contained: H.264 access units are
// produced in-process by videotestsrc ! x264enc, then replayed through the
// decoder exactly like V4L2 H.264 capture buffers would be.
#include "xmcam/pipeline/gst_compressed_decoder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <gst/app/gstappsink.h>

#include "xmcam/pipeline/gst_util.hpp"

using namespace xmotion;

namespace {

// Encode `n` frames of videotestsrc as Annex-B byte-stream access units.
std::vector<std::vector<uint8_t>> MakeH264Aus(int n) {
  GstEnsureInit();
  std::vector<std::vector<uint8_t>> aus;
  GError* err = nullptr;
  std::string desc =
      "videotestsrc num-buffers=" + std::to_string(n) +
      " ! video/x-raw,width=320,height=240,framerate=30/1 ! "
      "x264enc tune=zerolatency key-int-max=15 ! "
      "video/x-h264,stream-format=byte-stream,alignment=au ! "
      "appsink name=sink sync=false";
  GstElement* p = gst_parse_launch(desc.c_str(), &err);
  if (!p || err) {
    if (err) g_error_free(err);
    return aus;
  }
  GstAppSink* sink =
      GST_APP_SINK(gst_bin_get_by_name(GST_BIN(p), "sink"));
  gst_element_set_state(p, GST_STATE_PLAYING);
  while (true) {
    GstSample* s = gst_app_sink_try_pull_sample(sink, 2 * GST_SECOND);
    if (!s) break;
    GstBuffer* b = gst_sample_get_buffer(s);
    GstMapInfo map;
    if (gst_buffer_map(b, &map, GST_MAP_READ)) {
      aus.emplace_back(map.data, map.data + map.size);
      gst_buffer_unmap(b, &map);
    }
    gst_sample_unref(s);
  }
  gst_element_set_state(p, GST_STATE_NULL);
  gst_object_unref(sink);
  gst_object_unref(p);
  return aus;
}

}  // namespace

TEST(CompressedDecoder, RejectsUnsupportedFormat) {
  GstCompressedDecoder dec;
  EXPECT_FALSE(dec.Open(PixelFormat::kYuyv).ok());
  EXPECT_FALSE(GstCompressedDecoder::Supports(PixelFormat::kRgba8));
  EXPECT_TRUE(GstCompressedDecoder::Supports(PixelFormat::kMjpeg));
  EXPECT_TRUE(GstCompressedDecoder::Supports(PixelFormat::kH264));
}

TEST(CompressedDecoder, DecodesH264AccessUnits) {
  auto aus = MakeH264Aus(30);
  ASSERT_GE(aus.size(), 25u) << "encoder produced too few AUs";

  GstCompressedDecoder dec;
  ASSERT_TRUE(dec.Open(PixelFormat::kH264).ok());

  int decoded = 0, warmup_timeouts = 0;
  uint64_t seq = 0;
  bool geometry_ok = true;
  for (const auto& au : aus) {
    VideoFrame f;
    Status st = dec.Decode(au.data(), au.size(), 0, seq++, &f);
    if (st.ok()) {
      ++decoded;
      if (f.width != 320 || f.height != 240 ||
          f.format != PixelFormat::kI420 || f.plane2 == nullptr)
        geometry_ok = false;
    } else if (st.code() == ErrorCode::kTimeout) {
      ++warmup_timeouts;  // expected before the first IDR / decoder delay
    } else {
      FAIL() << "decode error: " << st.message();
    }
  }
  dec.Close();

  EXPECT_TRUE(geometry_ok) << "decoded frames were not 320x240 I420";
  // The decoder has a small warmup; the vast majority must decode.
  EXPECT_GE(decoded, static_cast<int>(aus.size()) - 5)
      << "decoded=" << decoded << " timeouts=" << warmup_timeouts;
}
