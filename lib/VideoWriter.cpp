/*
FFmpeg simple VideoWriter
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <algorithm>
#include <ctime>

#include <iostream>

extern "C" {
  #include <libavutil/dict.h>
}

#include "libvideoencoder/VideoWriter.h"


namespace libvideoencoder {
  using namespace std;


  VideoWriter::VideoWriter( const std::string &container, const AVCodecID codec_id )
    : _codec(nullptr),
    _outFormatContext( avformat_alloc_context() ),
    _streams()
  {
    av_log_set_level( AV_LOG_VERBOSE );

    avcodec_register_all();
    av_register_all();

    _outFormatContext->oformat = av_guess_format(container.c_str(), NULL, NULL);
    assert(_outFormatContext->oformat != nullptr );  // Should be an exception?

    _codec = avcodec_find_encoder( codec_id );
    assert( _codec != nullptr );   // Should be an exception?

    describeCodec( _codec->id );
  }



  VideoWriter::VideoWriter( const std::string &container, const std::string &codec_name )
    : _codec(nullptr),
    _outFormatContext( avformat_alloc_context() ),
    _streams()
  {
    av_log_set_level( AV_LOG_VERBOSE );

    avcodec_register_all();
    av_register_all();

    _outFormatContext->oformat = av_guess_format(container.c_str(), NULL, NULL);
    assert(_outFormatContext->oformat != nullptr );  // Should be an exception?

    _codec = avcodec_find_encoder_by_name( codec_name.c_str() );
    assert( _codec != nullptr );   // Should be an exception?

    describeCodec( _codec->id );
  }


  VideoWriter::~VideoWriter()
  {
    close();

    if (_outFormatContext) av_free(_outFormatContext);
  }

  void VideoWriter::describeCodec( AVCodecID codec_id ) {
    // cout << "Using container format " << _outFormat->name << " (" << _outFormat->long_name << ")" << endl;
    //
    const AVCodecDescriptor *codecDesc = avcodec_descriptor_get(codec_id);
    if( codecDesc ) {
      std::cout << "Using codec " << codec_id << ": " << codecDesc->name << " (" << codecDesc->long_name << ")"  << std::endl;
    } else {
      std::cerr << "Could not retrieve codec description for " << codec_id << std::endl;
    }
  }




  size_t VideoWriter::addVideoTrack( const int width, const int height, const float frameRate, int numStreams )
  {
    size_t idx = _streams.size();

    // Add video streams
    for( int i = 0; i < numStreams; i++ ) {
      _streams.push_back( shared_ptr<OutputTrack>(new VideoTrack( _outFormatContext, _codec, width, height, frameRate )) );
    }

    return idx;
  }

  size_t VideoWriter::addDataTrack(  )
  {
    size_t idx = _streams.size();
    _streams.push_back( shared_ptr<OutputTrack>(new DataTrack( _outFormatContext )) );

    return idx;
  }


  bool VideoWriter::open( const std::string &inputFile )
  //, const std::string &container, const AVCodecID codec, int numStreams)
  {
    assert( _outFormatContext != nullptr );

    //memcpy(_outFormatContext->filename, inputFile.c_str(), std::min(inputFile.size(), sizeof(_outFormatContext->filename)));

    av_dump_format(_outFormatContext, 0, inputFile.c_str(), 1);

    if (avio_open(&_outFormatContext->pb, inputFile.c_str(), AVIO_FLAG_WRITE) < 0)	{
      cerr << "Cannot open file" << endl;
      //Free();
      return false;
    }

    // AVDictionary *dict = nullptr;
    //av_dict_set_int( &dict, "write_tmcd", 1, 0 );

    // Set
    {
      std::time_t t = std::time(nullptr);
      char mbstr[100];
      std::strftime(mbstr, sizeof(mbstr), "%H:%M:%S.00", std::localtime(&t));

      av_dict_set( &_outFormatContext->metadata, "timecode", mbstr, 0 );
    }

    auto result = avformat_write_header(_outFormatContext, nullptr );

    // av_dict_free( &dict );

    return (result == 0);
  }



  bool VideoWriter::close()
  {
    if(_outFormatContext) {
      cerr << "Writing trailer" << endl;
      auto result = av_write_trailer(_outFormatContext);
      if( result != 0 ) {
        cerr << "Error writing AVIO trailer: " << std::hex << result;
      }
    }

    if (!(_outFormatContext->flags & AVFMT_NOFILE) && _outFormatContext->pb) {
      cerr << "Closing avio";
      avio_close(_outFormatContext->pb);
    }

    return true;
  }

  bool VideoWriter::addFrame(AVFrame* frame, unsigned int frameNum, unsigned int stream )
  {
    assert( _outFormatContext );
    assert( stream < _streams.size() );
    assert( stream < _outFormatContext->nb_streams );

    int result;

    // Encoding
    // measure time to encode
  //  std::chrono::system_clock::time_point startTime = std::chrono::system_clock::now();
    auto packet = _streams.at(stream)->encodeFrame(frame, frameNum);
  //  auto dt = std::chrono::system_clock::now() - startTime;
  //  std::cerr << "Encoding took " << float(dt.count())/1e6 << " ms" << endl;

    if( !packet ) {
      return false;
    }

  //  startTime = std::chrono::system_clock::now();
    auto res = addPacket( packet );
  //  dt = std::chrono::system_clock::now() - startTime;
  //  std::cerr << "Writing took " << float(dt.count())/1e6 << " ms" << endl;;

    return res;
  }

  bool VideoWriter::addPacket(AVPacket *packet )
  {
    assert( _outFormatContext );
    assert( packet->stream_index < _outFormatContext->nb_streams );

    {
      std::lock_guard<std::mutex> guard(_writeMutex);
      auto result = av_interleaved_write_frame(_outFormatContext, packet);
      av_packet_free( &packet );

      if( result < 0 ) {
        std::cerr << "Error writing interleaved packet" << std::endl;
        return false;
      }
    }



    return true;

  }








    };