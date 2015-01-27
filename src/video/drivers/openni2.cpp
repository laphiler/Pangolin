/* This file is part of the Pangolin Project.
 * http://github.com/stevenlovegrove/Pangolin
 *
 * Copyright (c) 2014 Richard Newcombe
 *               2014 Steven Lovegrove
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <pangolin/video/drivers/openni2.h>

#include <PS1080.h>
#include <OniVersion.h>

namespace pangolin
{

VideoPixelFormat VideoFormatFromOpenNI2(openni::PixelFormat fmt)
{
    std::string pvfmt;

    switch (fmt) {
    case openni::PIXEL_FORMAT_DEPTH_1_MM:   pvfmt = "GRAY16LE"; break;
    case openni::PIXEL_FORMAT_DEPTH_100_UM: pvfmt = "GRAY16LE"; break;
    case openni::PIXEL_FORMAT_SHIFT_9_2:    pvfmt = "GRAY16LE"; break; // ?
    case openni::PIXEL_FORMAT_SHIFT_9_3:    pvfmt = "GRAY16LE"; break; // ?
    case openni::PIXEL_FORMAT_RGB888:       pvfmt = "RGB24"; break;
    case openni::PIXEL_FORMAT_GRAY8:        pvfmt = "GRAY8"; break;
    case openni::PIXEL_FORMAT_GRAY16:       pvfmt = "GRAY16LE"; break;
    case openni::PIXEL_FORMAT_YUV422:       pvfmt = "YUYV422"; break;
#if ONI_VERSION_MAJOR >= 2 && ONI_VERSION_MINOR >= 2
    case openni::PIXEL_FORMAT_YUYV:         pvfmt = "Y400A"; break;
#endif
    default:
        throw VideoException("Unknown OpenNI pixel format");
        break;
    }

    return VideoFormatFromString(pvfmt);
}

void OpenNiVideo2::PrintOpenNI2Modes(openni::SensorType sensorType)
{
    // Query supported modes for device
    const openni::Array<openni::VideoMode>& modes =
            device[0].getSensorInfo(sensorType)->getSupportedVideoModes();

    switch (sensorType) {
    case openni::SENSOR_COLOR: pango_print_info("OpenNI Colour Modes:\n"); break;
    case openni::SENSOR_DEPTH: pango_print_info("OpenNI Depth Modes:\n"); break;
    case openni::SENSOR_IR:    pango_print_info("OpenNI IR Modes:\n"); break;
    }

    for(int i = 0; i < modes.getSize(); i++) {
        std::string sfmt = "PangolinUnknown";
        try{
            sfmt = VideoFormatFromOpenNI2(modes[i].getPixelFormat()).format;
        }catch(VideoException){}
        pango_print_info( "  %dx%d, %d fps, %s\n",
            modes[i].getResolutionX(), modes[i].getResolutionY(),
            modes[i].getFps(), sfmt.c_str()
        );
    }
}

openni::VideoMode OpenNiVideo2::FindOpenNI2Mode(
    openni::Device & device,
    openni::SensorType sensorType,
    int width, int height,
    int fps, openni::PixelFormat fmt
) {
    // Query supported modes for device
    const openni::Array<openni::VideoMode>& modes =
            device.getSensorInfo(sensorType)->getSupportedVideoModes();

    // Select last listed mode which matches parameters
    int best_mode = -1;
    for(int i = 0; i < modes.getSize(); i++) {
        if( (!width || modes[i].getResolutionX() == width) &&
            (!height || modes[i].getResolutionY() == height) &&
            (!fps || modes[i].getFps() == fps) &&
            (!fmt || modes[i].getPixelFormat() == fmt)
        ) {
            best_mode = i;
        }
    }

    if(best_mode >= 0) {
        return modes[best_mode];
    }

    throw pangolin::VideoException("Video mode not supported");
}

OpenNiVideo2::OpenNiVideo2(ImageDim dim, int fps)
{
    InitialiseOpenNI();

    openni::Array<openni::DeviceInfo> deviceList;
    openni::OpenNI::enumerateDevices(&deviceList);

    numDevices = deviceList.getSize();
    numStreams = 0;

    if (numDevices < 1) {
        throw VideoException("No OpenNI Devices available. Ensure your camera is plugged in.");
    }

    for(int i = 0 ; i < numDevices ;i ++) {
        const char*  device1Uri = deviceList[i].getUri();
        openni::Status rc = device[i].open(device1Uri);
        if (rc != openni::STATUS_OK) {
            throw VideoException( "OpenNI2: Couldn't open device.", openni::OpenNI::getExtendedError() );
        }

        sensor_type[numStreams].sensor_type = pangolin::OpenNiDepth;
        sensor_type[numStreams].dim = dim;
        sensor_type[numStreams].fps = fps;
        sensor_type[numStreams].device = i;
        rc = video_stream[numStreams].create(device[i], openni::SENSOR_DEPTH);
        if (rc != openni::STATUS_OK) {
            throw VideoException( "OpenNI2: Couldn't create stream.", openni::OpenNI::getExtendedError() );
        }
        numStreams++;

        sensor_type[numStreams].sensor_type = pangolin::OpenNiRgb;
        sensor_type[numStreams].dim = dim;
        sensor_type[numStreams].fps = fps;
        sensor_type[numStreams].device = i;
        rc = video_stream[numStreams].create(device[i], openni::SENSOR_COLOR);
        if (rc != openni::STATUS_OK) {
            throw VideoException( "OpenNI2: Couldn't create stream.", openni::OpenNI::getExtendedError() );
        }
        numStreams++;
    }

    SetupStreamModes();
    Start();
}

inline openni::SensorType SensorType(OpenNiSensorType& sensor)
{
    switch (sensor) {
    case OpenNiRgb:
    case OpenNiGrey:
        return openni::SENSOR_COLOR;
    case OpenNiDepth:
    case OpenNiDepthRegistered:
        return openni::SENSOR_DEPTH;
    case OpenNiIr:
    case OpenNiIr8bit:
    case OpenNiIr24bit:
    case OpenNiIrProj:
    case OpenNiIr8bitProj:
        return openni::SENSOR_IR;
    default:
        throw std::invalid_argument("OpenNI: Bad sensor type");
    }
}

OpenNiVideo2::OpenNiVideo2(std::vector<OpenNiStreamMode>& stream_modes)
{
    InitialiseOpenNI();

    openni::Array<openni::DeviceInfo> deviceList;
    openni::OpenNI::enumerateDevices(&deviceList);

    numDevices = deviceList.getSize();
    numStreams = 0;

    if (numDevices < 1) {
        throw VideoException("OpenNI2: No devices available. Ensure your camera is plugged in.");
    }

    for(size_t i=0; i < stream_modes.size(); ++i) {
        OpenNiStreamMode& mode = stream_modes[i];
        sensor_type[numStreams] = mode;

        // Open device if it isn't already
        if(!device[mode.device].isValid()) {
            const char*  device1Uri = deviceList[mode.device].getUri();
            openni::Status rc = device[mode.device].open(device1Uri);
            if (rc != openni::STATUS_OK) {
                throw VideoException( "OpenNI2: Couldn't open device.", openni::OpenNI::getExtendedError() );
            }
        }

        openni::Status rc = video_stream[numStreams].create(device[mode.device], SensorType(mode.sensor_type) );
        if (rc != openni::STATUS_OK) {
            throw VideoException( "OpenNI2: Couldn't create stream.", openni::OpenNI::getExtendedError() );
        }
        ++numStreams;
    }

    SetupStreamModes();
    Start();
}

void OpenNiVideo2::InitialiseOpenNI()
{
    openni::Status rc = openni::STATUS_OK;

    rc = openni::OpenNI::initialize();
    if (rc != openni::STATUS_OK) {
        throw VideoException( "Unable to initialise OpenNI library", openni::OpenNI::getExtendedError() );
    }
}

void OpenNiVideo2::SetupStreamModes()
{
    streams_properties = &frame_properties["streams"];
    *streams_properties = json::value(json::array_type,false);
    streams_properties->get<json::array>().resize(numStreams);

    use_depth = false;
    use_ir = false;
    use_rgb = false;
    depth_to_color = false;
    use_ir_and_rgb = false;

    //    const char* deviceURI = openni::ANY_DEVICE;
    fromFile = false;//(deviceURI!=NULL);

    sizeBytes =0;
    for(int i=0; i<numStreams; ++i) {
        const OpenNiStreamMode& mode = sensor_type[i];
        openni::SensorType nisensortype;
        openni::PixelFormat nipixelfmt;

        switch( mode.sensor_type ) {
        case OpenNiDepthRegistered:
            depth_to_color = true;
        case OpenNiDepth:
            nisensortype = openni::SENSOR_DEPTH;
            nipixelfmt = openni::PIXEL_FORMAT_DEPTH_1_MM;
            use_depth = true;
            break;
        case OpenNiIrProj:
        case OpenNiIr:
            nisensortype = openni::SENSOR_IR;
            nipixelfmt = openni::PIXEL_FORMAT_GRAY16;
            use_ir = true;
            break;
        case OpenNiIr24bit:
            nisensortype = openni::SENSOR_IR;
            nipixelfmt = openni::PIXEL_FORMAT_RGB888;
            use_ir = true;
            break;
        case OpenNiIr8bitProj:
        case OpenNiIr8bit:
            nisensortype = openni::SENSOR_IR;
            nipixelfmt = openni::PIXEL_FORMAT_GRAY8;
            use_ir = true;
            break;
        case OpenNiRgb:
            nisensortype = openni::SENSOR_COLOR;
            nipixelfmt = openni::PIXEL_FORMAT_RGB888;
            use_rgb = true;
            break;
        case OpenNiGrey:
            nisensortype = openni::SENSOR_COLOR;
            nipixelfmt = openni::PIXEL_FORMAT_GRAY8;
            use_rgb = true;
            break;
        case OpenNiUnassigned:
        default:
            continue;
        }

        openni::VideoMode onivmode;
        try {
            onivmode = FindOpenNI2Mode(device[mode.device], nisensortype, mode.dim.x, mode.dim.y, mode.fps, nipixelfmt);
        }catch(VideoException e) {
            pango_print_error("Unable to find compatible OpenNI Video Mode. Please choose from:\n");
            PrintOpenNI2Modes(nisensortype);
            fflush(stdout);
            throw e;
        }

        if(fromFile) {
            // do something with mode?
        }

        openni::Status rc = video_stream[i].setVideoMode(onivmode);
        if(rc != openni::STATUS_OK)
            throw VideoException("Couldn't set OpenNI VideoMode", openni::OpenNI::getExtendedError());

        const VideoPixelFormat fmt = VideoFormatFromOpenNI2(nipixelfmt);
        const StreamInfo stream(
            fmt, onivmode.getResolutionX(), onivmode.getResolutionY(),
            (onivmode.getResolutionX() * fmt.bpp) / 8,
            (unsigned char*)0 + sizeBytes
        );

        sizeBytes += stream.SizeBytes();
        streams.push_back(stream);
    }

    use_ir_and_rgb = use_rgb && use_ir;
}

void OpenNiVideo2::UpdateProperties()
{
    json::value& jsopenni = device_properties["openni"];

#define SET_PARAM(param_type, param) \
    { \
        param_type val; \
        if(device[0].getProperty(param, &val) == openni::STATUS_OK) { \
            jsdevice[#param] = val; \
        } \
    }

    json::value& jsdevice = jsopenni["device"];
    SET_PARAM( unsigned long long, XN_MODULE_PROPERTY_USB_INTERFACE );
    SET_PARAM( bool,  XN_MODULE_PROPERTY_MIRROR );
#undef SET_PARAM

    json::value& stream = jsopenni["streams"];
    stream = json::value(json::array_type,false);
    stream.get<json::array>().resize(Streams().size());
    for(unsigned int i=0; i<Streams().size(); ++i) {
        if(sensor_type[i].sensor_type != OpenNiUnassigned)
        {
#define SET_PARAM(param_type, param) \
            {\
                param_type val; \
                if(video_stream[i].getProperty(param, &val) == openni::STATUS_OK) { \
                    jsstream[#param] = val; \
                } \
            }

            json::value& jsstream = stream[i];
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_INPUT_FORMAT );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_CROPPING_MODE );

            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_CLOSE_RANGE );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_WHITE_BALANCE_ENABLED );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_GAIN );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_HOLE_FILTER );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_REGISTRATION_TYPE );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_CONST_SHIFT );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_PIXEL_SIZE_FACTOR );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_MAX_SHIFT );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_PARAM_COEFF );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_SHIFT_SCALE );
            SET_PARAM( unsigned long long, XN_STREAM_PROPERTY_ZERO_PLANE_DISTANCE );
            SET_PARAM( double, XN_STREAM_PROPERTY_ZERO_PLANE_PIXEL_SIZE );
            SET_PARAM( double, XN_STREAM_PROPERTY_EMITTER_DCMOS_DISTANCE );
            SET_PARAM( double, XN_STREAM_PROPERTY_DCMOS_RCMOS_DISTANCE );
#undef SET_PARAM
        }
    }
}

void OpenNiVideo2::SetMirroring(bool enable)
{
    // Set this property on all streams. It doesn't matter if it fails.
    for(unsigned int i=0; i<Streams().size(); ++i) {
        video_stream[i].setMirroringEnabled(enable);
    }
}

void OpenNiVideo2::SetAutoExposure(bool enable)
{
    // Set this property on all streams exposing CameraSettings
    for(unsigned int i=0; i<Streams().size(); ++i) {
        openni::CameraSettings* cam = video_stream[i].getCameraSettings();
        if(cam) cam->setAutoExposureEnabled(enable);
    }
}

void OpenNiVideo2::SetAutoWhiteBalance(bool enable)
{
    // Set this property on all streams exposing CameraSettings
    for(unsigned int i=0; i<Streams().size(); ++i) {
        openni::CameraSettings* cam = video_stream[i].getCameraSettings();
        if(cam) cam->setAutoWhiteBalanceEnabled(enable);
    }
}

void OpenNiVideo2::SetDepthCloseRange(bool enable)
{
    // Set this property on all streams. It doesn't matter if it fails.
    for(unsigned int i=0; i<Streams().size(); ++i) {
        video_stream[i].setProperty(XN_STREAM_PROPERTY_CLOSE_RANGE, enable);
    }
}

void OpenNiVideo2::SetDepthHoleFilter(bool enable)
{
    // Set this property on all streams. It doesn't matter if it fails.
    for(unsigned int i=0; i<Streams().size(); ++i) {
        video_stream[i].setProperty(XN_STREAM_PROPERTY_HOLE_FILTER, enable);
        video_stream[i].setProperty(XN_STREAM_PROPERTY_GAIN,50);
    }
}

void OpenNiVideo2::SetDepthColorSyncEnabled(bool enable)
{
    for(int i = 0 ; i < numDevices; i++) {
        device[i].setDepthColorSyncEnabled(enable);
    }
}

void OpenNiVideo2::SetRegisterDepthToImage(bool enable)
{
    if(enable) {
        for(int i = 0 ; i < numDevices; i++) {
            device[i].setImageRegistrationMode(openni::IMAGE_REGISTRATION_DEPTH_TO_COLOR);
        }
    }else{
        for(int i = 0 ; i < numDevices ; i++) {
            device[i].setImageRegistrationMode(openni::IMAGE_REGISTRATION_OFF);
        }
    }
}

void OpenNiVideo2::SetPlaybackSpeed(float speed)
{
    for(int i = 0 ; i < numDevices; i++) {
        openni::PlaybackControl* control = device[i].getPlaybackControl();
        if(control) control->setSpeed(speed);
    }
}

OpenNiVideo2::~OpenNiVideo2()
{
    Stop();

    for(int i=0; i<numStreams; ++i) {
        if( video_stream[i].isValid()) {
            video_stream[i].destroy();
        }
    }

    openni::OpenNI::shutdown();
}

size_t OpenNiVideo2::SizeBytes() const
{
    return sizeBytes;
}

const std::vector<StreamInfo>& OpenNiVideo2::Streams() const
{
    return streams;
}

void OpenNiVideo2::Start()
{
    for(unsigned int i=0; i<Streams().size(); ++i) {
        video_stream[i].start();
    }
}

void OpenNiVideo2::Stop()
{
    for(unsigned int i=0; i<Streams().size(); ++i) {
        video_stream[i].stop();
    }
}

bool OpenNiVideo2::GrabNext( unsigned char* image, bool wait )
{
    unsigned char* out_img = image;

    openni::Status rc = openni::STATUS_OK;

    for(unsigned int i=0; i<Streams().size(); ++i) {
        if(sensor_type[i].sensor_type == OpenNiUnassigned) {
            rc = openni::STATUS_OK;
            continue;
        }

        if(!video_stream[i].isValid()) {
            rc = openni::STATUS_NO_DEVICE;
            continue;
        }

        if(use_ir_and_rgb) video_stream[i].start();

        rc = video_stream[i].readFrame(&video_frame[i]);
        if(rc != openni::STATUS_OK) {
            pango_print_error("Error reading frame:\n%s", openni::OpenNI::getExtendedError() );
        }

        const bool toGreyscale = false;
        if(toGreyscale) {
            const int w = streams[i].Width();
            const int h = streams[i].Height();

            openni::RGB888Pixel* pColour = (openni::RGB888Pixel*)video_frame[i].getData();
            for(int i = 0 ; i  < w*h;i++){
                openni::RGB888Pixel rgb = pColour[i];
                int grey = ((int)(rgb.r&0xFF) +  (int)(rgb.g&0xFF) + (int)(rgb.b&0xFF))/3;
                grey = std::min(255,std::max(0,grey));
                out_img[i] = grey;
            }
        }else{
            memcpy(out_img, video_frame[i].getData(), streams[i].SizeBytes());
        }

        // update frame properties
        (*streams_properties)[i]["devtime_us"] = video_frame[i].getTimestamp();

        if(use_ir_and_rgb) video_stream[i].stop();

        out_img += streams[i].SizeBytes();
    }

    return rc == openni::STATUS_OK;
}

bool OpenNiVideo2::GrabNewest( unsigned char* image, bool wait )
{
    return GrabNext(image,wait);
}

}
