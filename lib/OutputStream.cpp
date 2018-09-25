extern "C"
{
  #include <libavcodec/avcodec.h>
  #include <libavformat/avformat.h>
  #include <libswscale/swscale.h>
  #include <libavutil/mathematics.h>
}

#include <assert.h>

#include <iostream>

#include "libvideoencoder/OutputStream.h"
#include "libvideoencoder/utils.h"


namespace libvideoencoder {

  OutputStream::OutputStream( AVFormatContext *oc, AVCodec *codec, int width, int height, float frameRate )
  : _stream( avformat_new_stream( oc, NULL ) ),
    _enc( avcodec_alloc_context3(codec) ),
    _numSamples(0),
    _scaledFrame(nullptr),
    _encodedData( av_buffer_alloc(10000000) ),
    _swsCtx(nullptr)
  {
    _stream->id = oc->nb_streams-1;

    _enc->frame_number = 0;
    _enc->codec_type = AVMEDIA_TYPE_VIDEO;
    _enc->codec_id = codec->id;
    _enc->width    = width;
    _enc->height   = height;

    _enc->time_base.den = int(frameRate * 100.0);
    _enc->time_base.num = 100;

    _stream->time_base = _enc->time_base;

    {
      const enum AVPixelFormat *pixFmt = codec->pix_fmts;
      // cout << "This codec supports these pixel formats: ";
      // for( int i = 0; pixFmt[i] != -1; i++ ) {
      //   cout << pixFmt[i] << " ";
      // }
      // cout << std::endl;
      _enc->pix_fmt = pixFmt[0];
    }

    // //st->codecpar->format = AV_PIX_FMT_YUV420P;
    if (_enc->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
      // Just for testing, we also add B frames
      //pCodecCxt->max_b_frames = 2;
    } else if (_enc->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
      /* Needed to avoid using macroblocks in which some coeffs overflow.
      This does not happen with normal video, it just happens here as
      the motion of the chroma plane does not match the luma plane. */
      //pCodecCxt->mb_decision = 2;
    } else if (_enc->codec_id == AV_CODEC_ID_PRORES ) {

      // For ProRes
      // 0 = Proxy
      // 1 = LT
      // 2 = normal
      // 3 = HQ
      _enc->profile = 2;
    }

    // AVDictionary *opt = NULL;
    // av_dict_copy(&opt, opt_arg, 0);
    {
      auto ret = avcodec_open2(_enc, _enc->codec, nullptr);
      //av_dict_free(&opt);
      if (ret < 0) {
        std::cerr << "Could not open video codec"; //: " <<  av_err2str(ret) << std::endl;
      }
    }

    {
      auto ret = avcodec_parameters_from_context(_stream->codecpar, _enc);
      if (ret < 0) {
        //fprintf(stderr, "Could not copy the stream parameters\n");
      }
    }

    std::cout << "Codec time base: " << _enc->time_base.num << "/" << _enc->time_base.den << std::endl;
    std::cout << "Video stream time base: " << _stream->time_base.num << "/" << _stream->time_base.den << std::endl;

  }

  OutputStream::~OutputStream()
  {
    if( _swsCtx ) sws_freeContext( _swsCtx );
    if( _scaledFrame ) av_frame_free( &_scaledFrame );
    if( _enc )      avcodec_close( _enc );
    //if( _stream )  avformat_free_context( &st );  // Stream is destroyed when AVFormatContext is cleaned up
  }


  AVPacket *OutputStream::addFrame( AVFrame *frame, int frameNum )
  {

    if ( !frame || !frame->data[0]) return nullptr;

    // Lazy-create the swscaler RGB to YUV420P.
    if (!_swsCtx) {
      _swsCtx = sws_getContext(_enc->width, _enc->height,
                                (AVPixelFormat)frame->format,              // Assume frame format will be consistent...
                                _enc->width, _enc->height,
                                _enc->pix_fmt,
                                SWS_BICUBLIN, NULL, NULL, NULL);
      assert( _swsCtx );
    }

    frame->pts = frameNum;

    if ( _enc->pix_fmt != (AVPixelFormat)frame->format )	{

      if( !_scaledFrame ) {
        // Lazy-allocate frame if you're going to be scaling
        _scaledFrame = alloc_frame(_enc->pix_fmt, _enc->width, _enc->height);
        assert(_scaledFrame);
      }

      // Convert RGB to YUV.
      auto res = sws_scale(_swsCtx, frame->data, frame->linesize, 0,
                            _enc->height, _scaledFrame->data, _scaledFrame->linesize);

      _scaledFrame->pts = frame->pts;
      return encode(_scaledFrame);
    }

    return encode(frame);
  }


  AVPacket *OutputStream::encode( AVFrame *frame ) {

    assert( _encodedData );

    // Encode
    AVPacket *packet = new AVPacket;
    packet->buf = av_buffer_ref(_encodedData);
    packet->size = _encodedData->size;
    packet->data = NULL;

    int nOutputSize = 0;

    // Encode frame to packet->
    // Todo:  switch to avcodec_send_frame / avcodec_receive_packet
    auto result = avcodec_encode_video2(_enc, packet, frame, &nOutputSize);

    packet->stream_index = _stream->index;
    packet->pts          = frame->pts;
    packet->dts          = frame->pts;

    if ((result < 0) || (nOutputSize <= 0)) {
      std::cerr << "Error encoding video" << std::endl;
      return nullptr;
    }

    av_packet_rescale_ts( packet, _enc->time_base, _stream->time_base );

    _numSamples++;

    return packet;
  }

};