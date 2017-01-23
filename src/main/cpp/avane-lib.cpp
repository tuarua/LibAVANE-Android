#include <jni.h>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include "json/json.h"

unsigned int numAvailableThreads = boost::thread::hardware_concurrency();
boost::thread threads[1];

boost::thread createThread(void(*otherFunction)(int p), int p) {
    boost::thread t(*otherFunction, p);
    return boost::move(t);
}

extern char *hex2str(const uint8_t *data, size_t len) {
    static char *str = NULL;
    size_t i;
    str = (char*)realloc(str, 2 * len + 1);
    *str = 0;
    for (i = 0; i < len; i++)
        sprintf(str + 2 * i, "%02X", data[i]);
    return str;
}


extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavutil/avassert.h"
    #include "libavutil/avstring.h"
    #include "libavutil/bprint.h"
    #include "libavutil/common.h"
    #include "libavutil/display.h"
    #include "libavutil/hash.h"
    #include "libavutil/opt.h"
    #include "libavutil/pixdesc.h"
    #include "libavutil/dict.h"
    #include "libavutil/intreadwrite.h"
    #include "libavutil/parseutils.h"
    #include "libavutil/timecode.h"
    #include "libavutil/timestamp.h"
    #include "libavutil/ffversion.h"
    #include "libswscale/swscale.h"
    #include "libswresample/swresample.h"
    #include "libpostproc/postprocess.h"
    #include "libavresample/version.h"
    #include "cmdutils.h"
    #include "ffmpeg.h"

    bool isEncoding = false;
    int logLevel = 32;
    typedef struct {
        std::vector<std::string> commandLine;
    }Input;
    Input inputContext;

    const HWAccel hwaccels_[] = {
    #if HAVE_VDPAU_X11
            { "vdpau", vdpau_init, HWACCEL_VDPAU, AV_PIX_FMT_VDPAU },
    #endif
    #if HAVE_DXVA2_LIB
            { "dxva2", dxva2_init, HWACCEL_DXVA2, AV_PIX_FMT_DXVA2_VLD },
    #endif
    #if CONFIG_VDA
            { "vda",   videotoolbox_init,   HWACCEL_VDA,   AV_PIX_FMT_VDA },
    #endif
    #if CONFIG_VIDEOTOOLBOX
            { "videotoolbox",   videotoolbox_init,   HWACCEL_VIDEOTOOLBOX,   AV_PIX_FMT_VIDEOTOOLBOX },
    #endif
    #if CONFIG_LIBMFX
            { "qsv",   qsv_init,   HWACCEL_QSV,   AV_PIX_FMT_QSV },
    #endif
    #if CONFIG_VAAPI
            { "vaapi", vaapi_decode_init, HWACCEL_VAAPI, AV_PIX_FMT_VAAPI },
    #endif
    #if CONFIG_CUVID
            { "cuvid", cuvid_init, HWACCEL_CUVID, AV_PIX_FMT_CUDA },
    #endif
            { 0 },
    };

    static const char *get_level_str(int level) {
        switch (level) {
            case AV_LOG_QUIET:
                return "quiet";
            case AV_LOG_DEBUG:
                return "debug";
            case AV_LOG_VERBOSE:
                return "verbose";
            case AV_LOG_INFO:
                return "info";
            case AV_LOG_WARNING:
                return "warning";
            case AV_LOG_ERROR:
                return "error";
            case AV_LOG_FATAL:
                return "fatal";
            case AV_LOG_PANIC:
                return "panic";
            default:
                return "";
        }
    }

    typedef struct {
        AVFormatContext *fmt_ctx = NULL;
        std::string fileName;
        std::string playList = "";
    }Probe;
    Probe probeContext;

    typedef struct jni_context {
        JavaVM *javaVM;
        JNIEnv *env;
        jclass jniHelperClz;
        jobject jniHelperObj;
        jmethodID asyncEventFunc;
    } JniContext;
    JniContext g_ctx;

    static AVInputFormat *iformat = NULL;
    static char *stream_specifier;
    static int *selected_streams;


    jint JNI_OnLoad(JavaVM* vm, void* reserved)  {
        JNIEnv* env;
        memset(&g_ctx, 0, sizeof(g_ctx));

        g_ctx.javaVM = vm;

        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
            return -1;

        g_ctx.env = env;
        jclass clz = env->FindClass("com/tuarua/avane/android/JniHandler");
        g_ctx.jniHelperClz = (jclass) env->NewGlobalRef(clz);

        jmethodID jniHelperCtor = env->GetMethodID(g_ctx.jniHelperClz, "<init>", "()V");
        jobject handler = env->NewObject(g_ctx.jniHelperClz, jniHelperCtor);
        g_ctx.jniHelperObj = env->NewGlobalRef(handler);
        g_ctx.asyncEventFunc = env->GetMethodID(g_ctx.jniHelperClz, "dispatchStatusEventAsync", "(Ljava/lang/String;Ljava/lang/String;)V");

        return JNI_VERSION_1_6;
    }

    void dispatchJniEventAsync(JNIEnv *env, jobject instance, jmethodID func,const char* msg,const char* type) {
        jstring javaMsg = env->NewStringUTF(msg);
        jstring javaType = env->NewStringUTF(type);
        env->CallVoidMethod(instance, func, javaMsg, javaType);
        env->DeleteLocalRef(javaMsg);
        env->DeleteLocalRef(javaType);
    }

    extern void trace(std::string msg) {
        if (logLevel > 0)
            dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc, msg.c_str(),"TRACE");
    }

    extern void logInfo(std::string msg) {
        if (logLevel > 0)
            dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc,msg.c_str(),"INFO");
    }

    extern void avaneLogProgress(double size, int secs, int us, double bitrate, double speed, float fps, int frame_number) {
        Json::Value j;
        j["speed"] = speed;
        j["bitrate"] = bitrate;
        j["secs"] = secs;
        j["us"] = us;
        j["size"] = size;
        j["fps"] = fps;
        j["frame"] = frame_number;

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(j).c_str();
        dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc,json_str, "ON_ENCODE_PROGRESS");
    }
    void avaneLog(void *ptr, int level, const char *fmt, va_list vl) {


        if(logLevel > 0){
            static char message[8192];
            const char *module = NULL;
            using namespace std;
            using namespace boost;
            if (ptr) {
                AVClass *avc = *(AVClass**)ptr;
                if (avc->item_name)
                    module = avc->item_name(ptr);
            }
            string logStr;
            string logHtml;


    #ifdef _WIN32
            vsnprintf_s(message, sizeof message, sizeof message, fmt, vl);
    #else
            vsprintf(message,fmt,vl);
    #endif

            string messageTrimmed = string(message);
            boost::algorithm::trim(messageTrimmed);

            logStr = " [ffmpeg][" + (module ? string(module) : "") + "][" + lexical_cast<string>(get_level_str(level)) + "] : " + messageTrimmed;

            //TODO
            //logHtml = " <p class=\"" + lexical_cast<string>(get_level_str(level)) + "\">" + (module ? string(module) + ":": "") + lexical_cast<string>(get_level_str(level)) + ": " + messageTrimmed + "</p>";

            if (level <= logLevel && !messageTrimmed.empty()) {
                logInfo(logStr);
                //logInfoHtml(logHtml);
            }
            /*
                //also make a nice html string

                if (level == 16 || level == 8)
                logError(string(message));
            */

        }

    }


    static int is_device(const AVClass *avclass) {
    if (!avclass) return 0;
        return AV_IS_INPUT_DEVICE(avclass->category) || AV_IS_OUTPUT_DEVICE(avclass->category);
    }

#define REALLOCZ_ARRAY_STREAM(ptr, cur_n, new_n)                        \
	{                                                                       \
		ret = av_reallocp_array(&(ptr), (new_n), sizeof(*(ptr)));           \
		if (ret < 0)                                                        \
			goto end;                                                       \
		memset( (ptr) + (cur_n), 0, ((new_n) - (cur_n)) * sizeof(*(ptr)) ); \
	}

    //probe input
    static void closeFileToProbe(AVFormatContext **ctx_ptr) {
        int i;
        AVFormatContext *fmt_ctx = *ctx_ptr;
        /* close decoder for each stream */
        for (i = 0; i < fmt_ctx->nb_streams; i++)
            if (fmt_ctx->streams[i]->codec->codec_id != AV_CODEC_ID_NONE)
                avcodec_close(fmt_ctx->streams[i]->codec);
        avformat_close_input(ctx_ptr);
    }


    static int probeFile(const char *filename) {
        probeContext.fmt_ctx = NULL;
        int ret = 0;
        int err, i, orig_nb_streams;

        AVDictionaryEntry *t;
        AVDictionary **opts;
        av_dict_set(&format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);

        if ((err = avformat_open_input(&probeContext.fmt_ctx, filename, iformat, &format_opts)) < 0) {
            trace("Error opening file");
            return err;
        }

        av_dict_set(&format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);

        // fill the streams in the format context
        opts = setup_find_stream_info_opts(probeContext.fmt_ctx, codec_opts);
        orig_nb_streams = probeContext.fmt_ctx->nb_streams;
        err = avformat_find_stream_info(probeContext.fmt_ctx, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);

        av_freep(&opts);

        if (err < 0) {
            trace("Error finding stream info");
            return err;
        }

        for (i = 0; i < probeContext.fmt_ctx->nb_streams; i++) {
            AVStream *stream = probeContext.fmt_ctx->streams[i];
            AVCodec *codec;

            if (stream->codecpar->codec_id == AV_CODEC_ID_PROBE) {
                trace("Failed to probe codec for input stream");
            }
            else if (!(codec = avcodec_find_decoder(stream->codecpar->codec_id))) {
                trace("Unsupported codec with id %d for input stream");
            }
            else {
                AVDictionary *opts = filter_codec_opts(codec_opts, stream->codecpar->codec_id, probeContext.fmt_ctx, stream, codec);
                if (avcodec_open2(stream->codec, codec, &opts) < 0) {
                    trace("Could not open codec for input stream");
                }
                if ((t = av_dict_get(opts, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
                    trace("Option for input stream not found");
                    return AVERROR_OPTION_NOT_FOUND;
                }
            }
        }

#define CHECK_END if (ret < 0) goto end
        CHECK_END;

        REALLOCZ_ARRAY_STREAM(selected_streams, 0, probeContext.fmt_ctx->nb_streams);


        for (i = 0; i < probeContext.fmt_ctx->nb_streams; i++) {
            if (stream_specifier) {
                ret = avformat_match_stream_specifier(probeContext.fmt_ctx, probeContext.fmt_ctx->streams[i], stream_specifier);
                CHECK_END;
                else
                    selected_streams[i] = ret;
                ret = 0;
            }
            else {
                selected_streams[i] = 1;
            }
        }

        end:
        if(ret < 0) {
            closeFileToProbe(&probeContext.fmt_ctx);
            av_freep(&selected_streams);
        }
        return ret;
    }
    void threadProbe(int p) {
        boost::mutex mutex;
        using boost::this_thread::get_id;


        JavaVM *javaVM = g_ctx.javaVM;
        JNIEnv *env;
        jint res = javaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res != JNI_OK) {
            res = javaVM->AttachCurrentThread(&env, NULL);
            if (JNI_OK != res) {
                return;
            } else{
                g_ctx.env = env;
                g_ctx.asyncEventFunc = env->GetMethodID(g_ctx.jniHelperClz, "dispatchStatusEventAsync", "(Ljava/lang/String;Ljava/lang/String;)V");
            }
        }


        mutex.lock();
        ////////////////// ************************ //////////////////

        av_log_set_flags(AV_LOG_SKIP_REPEATED);
        av_register_all();
        avformat_network_init();
        init_opts();
    #if CONFIG_AVDEVICE
        avdevice_register_all();
    #endif

        int ret;
        ret = probeFile(probeContext.fileName.c_str());
        uninit_opts();

        avformat_network_deinit();

        std::string returnVal = "";
        dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc,returnVal.c_str(), (ret == 0) ? "ON_PROBE_INFO_AVAILABLE" : "NO_PROBE_INFO");

        ////////////////// ************************ //////////////////
        mutex.unlock();
        javaVM->DetachCurrentThread();
    }

    JNIEXPORT void JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1triggerProbeInfo(JNIEnv *env, jobject instance,
                                                                 jstring filename_) {
        using namespace std;
        const char *filename = env->GetStringUTFChars(filename_, 0);

        probeContext.fileName = string(filename);
        env->ReleaseStringUTFChars(filename_, filename);
        threads[0] = boost::move(createThread(&threadProbe, 1));
    }


    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getProbeInfo(JNIEnv *env, jobject instance) {
        using namespace boost;
        using namespace std;

        Json::Value probe;
        if (probeContext.fmt_ctx) {

            //streams
            Json::Value vecVideoStreams;
            Json::Value vecAudioStreams;
            Json::Value vecSubtitleStreams;

            int i = 0, j = 0, numVideoStreams = 0, numAudioStreams = 0, numSubtitleStreams = 0, currVideoStream = -1, currAudioStream = -1, currSubtitleStream = -1;

            //count the number of each stream type
            for (j = 0; j < probeContext.fmt_ctx->nb_streams; j++) {
                if (selected_streams[j]) {
                    AVCodecContext *dec_ctx;
                    AVStream *stream = probeContext.fmt_ctx->streams[j];
                    if ((dec_ctx = stream->codec)) {
                        switch (dec_ctx->codec_type) {
                            case AVMEDIA_TYPE_VIDEO:
                                numVideoStreams++;
                                break;
                            case AVMEDIA_TYPE_AUDIO:
                                numAudioStreams++;
                                break;
                            case AVMEDIA_TYPE_SUBTITLE:
                                numSubtitleStreams++;
                                break;
                            default:
                                break;
                        }
                    }
                }
            }

            for (i = 0; i < probeContext.fmt_ctx->nb_streams; i++) {
                if (selected_streams[i]) {
                    AVStream *stream = probeContext.fmt_ctx->streams[i];
                    AVCodecContext *dec_ctx;
                    const AVCodec *dec;
                    const AVCodecDescriptor *cd;
                    const char *s;
                    AVRational sar, dar;

                    char val_str[128];

                    Json::Value objStream;
                    if ((dec_ctx = stream->codec)) {
                        const char *profile = NULL;
                        dec = dec_ctx->codec;

                        switch (dec_ctx->codec_type) {
                            case AVMEDIA_TYPE_VIDEO:
                                currVideoStream++;
                                break;
                            case AVMEDIA_TYPE_AUDIO:
                                currAudioStream++;
                                break;
                            case AVMEDIA_TYPE_SUBTITLE:
                                currSubtitleStream++;
                                break;
                            default:
                                break;
                        }

                        objStream["index"] = stream->index;

                        if (dec) {
                            objStream["codecName"] = dec->name;
                            objStream["codecLongName"] = (dec->long_name) ? dec->long_name : "unknown";
                        } else if ((cd = avcodec_descriptor_get(stream->codec->codec_id))) {
                            objStream["codecName"] = cd->name;
                            objStream["codecLongName"] = (cd->long_name) ? cd->long_name : "unknown";
                        } else {
                            objStream["codecName"] = "unknown";
                            objStream["codecLongName"] = "unknown";
                        }


                        if (dec && (profile = av_get_profile_name(dec, dec_ctx->profile))) {
                            objStream["profile"] = profile;
                        }else {
                            if (dec_ctx->profile != FF_PROFILE_UNKNOWN) {
                                char profile_num[12];
                                snprintf(profile_num, sizeof(profile_num), "%d", dec_ctx->profile);
                                objStream["codecLongName"] = profile_num;
                            }
                            else {
                                objStream["profile"] = "unknown";
                            }
                        }

                        s = av_get_media_type_string(dec_ctx->codec_type);
                        objStream["codecType"] = (s) ? s : "unknown";
                        objStream["codecTimeBase"] = lexical_cast<string>(dec_ctx->time_base.num) + "/" + lexical_cast<string>(dec_ctx->time_base.den);


                        /* AVI/FourCC tag */
                        av_get_codec_tag_string(val_str, sizeof(val_str), dec_ctx->codec_tag);

                        objStream["codecTagString"] = lexical_cast<string>(val_str);
                        objStream["codecTag"] = dec_ctx->codec_tag;

                        switch (dec_ctx->codec_type) {
                            case AVMEDIA_TYPE_VIDEO:

                                objStream["width"] = dec_ctx->width;
                                objStream["height"] = dec_ctx->height;
                                objStream["codedWidth"] = dec_ctx->coded_width;
                                objStream["codedHeight"] = dec_ctx->coded_height;
                                objStream["hasBframes"] = dec_ctx->has_b_frames;


                                sar = av_guess_sample_aspect_ratio(probeContext.fmt_ctx, stream, NULL);
                                if (sar.den) {
                                    objStream["sampleAspectRatio"] = lexical_cast<string>(sar.num) + ":" + lexical_cast<string>(sar.den);
                                    av_reduce(&dar.num, &dar.den, dec_ctx->width  * sar.num, dec_ctx->height * sar.den, 1024 * 1024);
                                    objStream["displayAspectRatio"] = lexical_cast<string>(dar.num) + ":" + lexical_cast<string>(dar.den);
                                }

                                s = av_get_pix_fmt_name(dec_ctx->pix_fmt);

                                objStream["pixelFormat"] = (s) ? s : "unknown";
                                objStream["level"] = dec_ctx->level;

                                if (dec_ctx->color_range != AVCOL_RANGE_UNSPECIFIED)
                                    objStream["colorRange"] = av_color_range_name(dec_ctx->color_range);


                                s = av_get_colorspace_name(dec_ctx->colorspace);
                                objStream["colorSpace"] = string((s) ? s : "unknown");

                                if (dec_ctx->color_trc != AVCOL_TRC_UNSPECIFIED)
                                    objStream["colorTransfer"] = av_color_transfer_name(dec_ctx->color_trc);
                                if (dec_ctx->color_primaries != AVCOL_PRI_UNSPECIFIED)
                                    objStream["colorPrimaries"] = av_color_primaries_name(dec_ctx->color_primaries);
                                if (dec_ctx->chroma_sample_location != AVCHROMA_LOC_UNSPECIFIED)
                                    objStream["chromaLocation"] = av_chroma_location_name(dec_ctx->chroma_sample_location);

                                if (dec_ctx->timecode_frame_start >= 0) {
                                    char tcbuf[AV_TIMECODE_STR_SIZE];
                                    av_timecode_make_mpeg_tc_string(tcbuf, dec_ctx->timecode_frame_start);
                                    objStream["timecode"] = tcbuf;
                                }else {
                                    objStream["timecode"] = "N/A";
                                }
                                objStream["refs"] = dec_ctx->refs;

                                break;

                            case AVMEDIA_TYPE_AUDIO:
                                s = av_get_sample_fmt_name(dec_ctx->sample_fmt);

                                objStream["sampleFormat"] = (s) ? s : "unknown";
                                objStream["sampleRate"] = dec_ctx->sample_rate;
                                objStream["channels"] = dec_ctx->channels;

                                char channel_layout[128];
                                av_get_channel_layout_string(channel_layout, sizeof(channel_layout), dec_ctx->channels, dec_ctx->channel_layout);
                                //av_get_channel_layout
                                if (dec_ctx->channel_layout)
                                    objStream["channelLayout"] = channel_layout;
                                else
                                    objStream["channelLayout"] = "unknown";

                                objStream["bitsPerSample"] = av_get_bits_per_sample(dec_ctx->codec_id);

                                break;

                            case AVMEDIA_TYPE_SUBTITLE:
                                if (dec_ctx->width)
                                    objStream["width"] = dec_ctx->width;
                                if (dec_ctx->height)
                                    objStream["height"] = dec_ctx->height;
                                break;
                            default:
                                break;

                        }

                    } else {
                        objStream["codecType"] = "unknown";
                    }

                    double v;
                    int rnded;
                    if (stream->r_frame_rate.num > 0) {
                        v = double(stream->r_frame_rate.num) / stream->r_frame_rate.den;
                        rnded = round(v * 1000);
                        objStream["realFrameRate"] = double(rnded) / 1000;
                    }

                    if (stream->avg_frame_rate.num > 0) {
                        v = double(stream->avg_frame_rate.num) / stream->avg_frame_rate.den;
                        rnded = round(v * 1000);
                        objStream["averageFrameRate"] = double(rnded) / 1000;
                    }
                    objStream["timeBase"] = lexical_cast<string>(stream->time_base.num) + ":" + lexical_cast<string>(stream->time_base.den);
                    if (probeContext.fmt_ctx->iformat->flags & AVFMT_SHOW_IDS)
                        objStream["id"] = (probeContext.fmt_ctx->iformat->flags & AVFMT_SHOW_IDS) ? lexical_cast<string>(stream->id) : "N/A";

                    if (stream->duration > 0) {
                        v = double(stream->duration) / stream->time_base.den;
                        rnded = round(v * 1000);
                        objStream["duration"] = double(rnded)/1000;
                        objStream["durationTimestamp"] = double(stream->duration);
                    }
                    objStream["startPTS"] = double(stream->start_time);
                    v = double(stream->start_time) *  av_q2d(stream->time_base);
                    rnded = round(v * 100000);

                    objStream["startTime"] = double(rnded) / 100000;
                    if (dec_ctx->rc_max_rate > 0)
                        objStream["maxBitRate"] = double(dec_ctx->rc_max_rate);
                    if (dec_ctx->bits_per_raw_sample > 0)
                        objStream["bitsPerRawSample"] = double(dec_ctx->bits_per_raw_sample);
                    if (stream->nb_frames > 0)
                        objStream["numFrames"] = double(stream->nb_frames);

                    if (dec_ctx->bit_rate > 0)
                        objStream["bitRate"] = double(dec_ctx->bit_rate);

                    AVDictionaryEntry *tag = NULL;
                    Json::Value streamTags;
                    while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
                        streamTags[tag->key] = tag->value;

                    objStream["tags"] = streamTags;

                    switch (dec_ctx->codec_type) {
                        case AVMEDIA_TYPE_VIDEO:
                            vecVideoStreams[currVideoStream] = objStream;
                            break;
                        case AVMEDIA_TYPE_AUDIO:
                            vecAudioStreams[currAudioStream] = objStream;
                            break;
                        case AVMEDIA_TYPE_SUBTITLE:
                            vecSubtitleStreams[currSubtitleStream] = objStream;
                            break;
                        default:
                            break;
                    }

                }
            }


            if(numVideoStreams > 0)
                probe["videoStreams"] = vecVideoStreams;
            if(numAudioStreams > 0)
                probe["audioStreams"] = vecAudioStreams;
            if(numSubtitleStreams > 0)
                probe["subtitleStreams"] = vecSubtitleStreams;

            Json::Value objFormat;

            objFormat["filename"] = probeContext.fmt_ctx->filename;
            objFormat["numStreams"] = probeContext.fmt_ctx->nb_streams;
            objFormat["numPrograms"] = probeContext.fmt_ctx->nb_programs;

            objFormat["formatName"] = probeContext.fmt_ctx->iformat->name;
            if (probeContext.fmt_ctx->iformat->long_name)
                objFormat["formatLongName"] = probeContext.fmt_ctx->iformat->long_name;

            objFormat["startTime"] = (double(probeContext.fmt_ctx->start_time)) / 1000000;
            objFormat["duration"] = (double(probeContext.fmt_ctx->duration)) / 1000000;

            int64_t size = probeContext.fmt_ctx->pb ? avio_size(probeContext.fmt_ctx->pb) : -1;
            objFormat["size"] = int32_t(size);

            objFormat["bitRate"] = int32_t(probeContext.fmt_ctx->bit_rate);
            objFormat["probeScore"] = av_format_get_probe_score(probeContext.fmt_ctx);

            //Tags
            AVDictionaryEntry *tag = NULL;
            int numTags = av_dict_count(probeContext.fmt_ctx->metadata);

            Json::Value formatTags;
            while ((tag = av_dict_get(probeContext.fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
                formatTags[tag->key] = tag->value;

            objFormat["tags"] = formatTags;
            probe["format"] = objFormat;

        }

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(probe).c_str();
        return env->NewStringUTF(json_str);

    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getFilters(JNIEnv *env, jobject instance) {
        using namespace std;
        Json::Value vec;

#if CONFIG_AVFILTER
        const AVFilter *filter = NULL;
        char descr[64], *descr_cur;
        int i, j;
        int cnt = 0;
        const AVFilterPad *pad;

        avfilter_register_all();

        while ((filter = avfilter_next(filter))) {
            Json::Value obj;
            descr_cur = descr;
            for (i = 0; i < 2; i++) {
                if (i) {
                    *(descr_cur++) = '-';
                    *(descr_cur++) = '>';
                }
                pad = i ? filter->outputs : filter->inputs;
                for (j = 0; pad && avfilter_pad_get_name(pad, j); j++) {
                    if (descr_cur >= descr + sizeof(descr) - 4)
                        break;
                    AVMediaType af = avfilter_pad_get_type(pad, j);
                    switch (af) {
                        case AVMEDIA_TYPE_VIDEO:
                            *(descr_cur++) = 'V';
                            break;
                        case AVMEDIA_TYPE_AUDIO:
                            *(descr_cur++) = 'A';
                            break;
                        case AVMEDIA_TYPE_DATA:
                            *(descr_cur++) = 'D';
                            break;
                        case AVMEDIA_TYPE_SUBTITLE:
                            *(descr_cur++) = 'S';
                            break;
                        case AVMEDIA_TYPE_ATTACHMENT:
                            *(descr_cur++) = 'T';
                            break;
                        default:
                            *(descr_cur++) = '?';
                            break;
                    }
                }
                if (!j)
                    *(descr_cur++) = ((!i && (filter->flags & AVFILTER_FLAG_DYNAMIC_INPUTS)) ||
                                      (i && (filter->flags & AVFILTER_FLAG_DYNAMIC_OUTPUTS))) ? 'N' : '|';
            }
            *descr_cur = 0;

            obj["hts"] = (filter->flags & AVFILTER_FLAG_SUPPORT_TIMELINE);
            obj["hst"] = (filter->flags & AVFILTER_FLAG_SLICE_THREADS);
            obj["hcs"] = (filter->process_command);

            obj["n"] = filter->name;
            obj["d"] = filter->description;
            obj["t"] = descr;
            vec[cnt] = obj;
            cnt++;
        }
#endif

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vec).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getPixelFormats(JNIEnv *env, jobject instance) {
        using namespace boost;
        using namespace std;
        Json::Value vec;

        const AVPixFmtDescriptor *pix_desc = NULL;
#if !CONFIG_SWSCALE
        #   define sws_isSupportedInput(x)  0
#   define sws_isSupportedOutput(x) 0
#endif
        int cnt = 0;
        while ((pix_desc = av_pix_fmt_desc_next(pix_desc))) {
            Json::Value obj;
            enum AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(pix_desc);
            obj["name"] = pix_desc->name;
            obj["isInput"] = (sws_isSupportedInput(pix_fmt) == 1);
            obj["isOutput"] = (sws_isSupportedOutput(pix_fmt) == 1);
            obj["isHardwareAccelerated"] = ((pix_desc->flags & AV_PIX_FMT_FLAG_HWACCEL) == 1);
            obj["isPalleted"] = ((pix_desc->flags & AV_PIX_FMT_FLAG_PAL) == 1);
            obj["isBitStream"] = ((pix_desc->flags & AV_PIX_FMT_FLAG_BITSTREAM) == 1);
            obj["numComponents"] = pix_desc->nb_components;
            obj["bitsPerPixel"] = av_get_bits_per_pixel(pix_desc);
            vec[cnt] = obj;
            cnt++;
        }

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vec).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getLayouts(JNIEnv *env, jobject instance) {
        using namespace boost;
        using namespace std;
        Json::Value objLayouts;
        int i,cnt = 0;
        uint64_t layout, j;
        const char *name, *descr;
        Json::Value vecIndividual;
        Json::Value vecStandard;
        for (i = 0; i < 63; i++) {
            name = av_get_channel_name((uint64_t)1 << i);
            if (!name)
                continue;
            descr = av_get_channel_description((uint64_t)1 << i);
            Json::Value objLayout;
            objLayout["n"] = name;
            objLayout["d"] = descr;
            vecIndividual[cnt] = objLayout;
            cnt++;
        }

        objLayouts["individual"] = vecIndividual;

        cnt = 0;
        for (i = 0; !av_get_standard_channel_layout(i, &layout, &name); i++) {
            if (name) {
                Json::Value objLayout;
                objLayout["n"] = name;
                std::stringstream ss;
                for (j = 1; j; j <<= 1) {
                    if ((layout & j)) {
                        if (layout & (j - 1)) ss << "+";
                        ss << av_get_channel_name(j);
                    }
                }
                objLayout["d"] = descr;
                vecStandard[cnt] = objLayout;
                cnt++;
            }
        }
        objLayouts["standard"] = vecStandard;
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(objLayouts).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getColors(JNIEnv *env, jobject instance) {
        using namespace boost;
        using namespace std;
        Json::Value vecColors;

        const char *name;
        const uint8_t *rgb;
        int i;

        for (i = 0; name = av_get_known_color_name(i, &rgb); i++) {
            Json::Value objColor;
            objColor["n"] = name;
            objColor["v"] = "#"+lexical_cast<string>(hex2str(rgb, sizeof(rgb)-1));
            vecColors[i] = objColor;
        }
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vecColors).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getProtocols(JNIEnv *env, jobject instance) {
        Json::Value objProtocols;
        Json::Value vecInputProtocols;
        Json::Value vecOutputProtocols;

        av_register_all();
        avformat_network_init();

        void *opaque = NULL;
        const char *name;
        int cnt = 0;

        name = "000";
        while ((name = avio_enum_protocols(&opaque, 0))) {
            Json::Value objProtocol;
            objProtocol["n"] = name;
            vecInputProtocols[cnt] = objProtocol;
            cnt++;
        }
        objProtocols["i"] = vecInputProtocols;

        cnt = 0;
        name = "000";
        while ((name = avio_enum_protocols(&opaque, 1))) {
            Json::Value objProtocol;
            objProtocol["n"] = name;
            vecOutputProtocols[cnt] = objProtocol;
            cnt++;
        }
        objProtocols["o"] = vecOutputProtocols;
        avformat_network_deinit();

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(objProtocols).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getLicense(JNIEnv *env, jobject instance) {
        std::string license = "";
#if CONFIG_NONFREE
        license = "This version of ffmpeg has nonfree parts compiled in.\nTherefore it is not legally redistributable.";
#elif CONFIG_GPLV3
        license = "ffmpeg is free software; you can redistribute it and/or modify\nit under the terms of the GNU General Public License"
                "as published by the Free Software Foundation; either version 3 of the License, or \n(at your option) any later version.\n"
                "ffmpeg is distributed in the hope that it will be useful, \nbut WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the\nGNU General Public License for more details.\n\n"
                "You should have received a copy of the GNU General Public License\nalong with ffmpeg.If not, see <http://www.gnu.org/licenses/>.";
#elif CONFIG_GPL
        license = "ffmpeg is free software; you can redistribute it and/or modify\nit under the terms of the GNU General Public License as published by\n"
"the Free Software Foundation; either version 2 of the License, or \n""(at your option) any later version.\n\n"
"ffmpeg is distributed in the hope that it will be useful,\nbut WITHOUT ANY WARRANTY; without even the implied warranty of\n"
			"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\nGNU General Public License for more details.\nYou should have received a copy of the GNU General Public License\nalong with ffmpeg; if not, write to the Free Software\nFoundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n";
#elif CONFIG_LGPLV3
		license = "ffmpeg is free software; you can redistribute it and/or modify\nit under the terms of the GNU Lesser General Public License as published by\nthe Free Software Foundation; either version 3 of the License, or\n(at your option) any later version.\n\n"
			"ffmpeg is distributed in the hope that it will be useful,\nbut WITHOUT ANY WARRANTY; without even the implied warranty of\n"
			"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\nGNU Lesser General Public License for more details.\n\n"
			"You should have received a copy of the GNU Lesser General Public License\nalong with ffmpeg.If not, see <http://www.gnu.org/licenses/>.";
#else
		license = "ffmpeg is free software; you can redistribute it and/or\nmodify it under the terms of the GNU Lesser General Public\nLicense as published by the Free Software Foundation; either\nversion 2.1 of the License, or (at your option) any later version.\n\nffmpeg is distributed in the hope that it will be useful,\n"
			"but WITHOUT ANY WARRANTY; without even the implied warranty of\nMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\nLesser General Public License for more details.\n\nYou should have received a copy of the GNU Lesser General Public\n"
			"License along with ffmpeg; if not, write to the Free Software\nFoundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA\n";
#endif

        return env->NewStringUTF(license.c_str());
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getBuildConfiguration(JNIEnv *env, jobject instance) {
        return env->NewStringUTF(std::string(FFMPEG_CONFIGURATION).c_str());
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getHardwareAccelerations(JNIEnv *env,
                                                                         jobject instance) {
        Json::Value vecHWAccels;
        int numHWAccels = FF_ARRAY_ELEMS(hwaccels_) - 1;
        if(numHWAccels < 1)
            return env->NewStringUTF("");
        for (int i = 0; i < numHWAccels; i++) {
            Json::Value objHW;
            objHW["n"] = hwaccels_[i].name;
            vecHWAccels[i] = objHW;
        }
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vecHWAccels).c_str();
        return env->NewStringUTF(json_str);
    }

    static int compare_codec_desc(const void *a, const void *b) {
    const AVCodecDescriptor * const *da = (const AVCodecDescriptor **)a;
    const AVCodecDescriptor * const *db = (const AVCodecDescriptor **)b;
    return (*da)->type != (*db)->type ? FFDIFFSIGN((*da)->type, (*db)->type) : strcmp((*da)->name, (*db)->name);
}
    static unsigned get_codecs_sorted(const AVCodecDescriptor ***rcodecs) {
    const AVCodecDescriptor *desc = NULL;
    const AVCodecDescriptor **codecs;
    unsigned nb_codecs = 0, i = 0;

    while ((desc = avcodec_descriptor_next(desc)))
        nb_codecs++;
    if (!(codecs = (const AVCodecDescriptor **)(av_calloc(nb_codecs, sizeof(*codecs)))))
        av_log(NULL, AV_LOG_ERROR, "Out of memory\n");
    desc = NULL;
    while ((desc = avcodec_descriptor_next(desc)))
        codecs[i++] = desc;
    av_assert0(i == nb_codecs);
    qsort(codecs, nb_codecs, sizeof(*codecs), compare_codec_desc);
    *rcodecs = codecs;
    return nb_codecs;
}
    static const AVCodec *next_codec_for_id(enum AVCodecID id, const AVCodec *prev, int encoder) {
    while ((prev = av_codec_next(prev))) {
        if (prev->id == id && (encoder ? av_codec_is_encoder(prev) : av_codec_is_decoder(prev)))
            return prev;
    }
    return NULL;
}
    Json::Value getAvailableFormatsDevices(int device_only){
        using namespace std;

        Json::Value vecFormats;

        AVInputFormat *ifmt = NULL;
        AVOutputFormat *ofmt = NULL;
        const char *last_name;
        int is_dev;
        last_name = "000";
        int cnt = 0;


        av_register_all();
        avformat_network_init();

        for (;;) {
            int decode = 0;
            int encode = 0;
            const char *name = NULL;
            const char *long_name = NULL;

            while ((ofmt = av_oformat_next(ofmt))) {
                is_dev = is_device(ofmt->priv_class);
                if (!is_dev && device_only)
                    continue;
                if ((!name || strcmp(ofmt->name, name) < 0) &&
                    strcmp(ofmt->name, last_name) > 0) {
                    name = ofmt->name;
                    long_name = ofmt->long_name;
                    encode = 1;
                }
            }
            while ((ifmt = av_iformat_next(ifmt))) {
                is_dev = is_device(ifmt->priv_class);
                if (!is_dev && device_only)
                    continue;
                if ((!name || strcmp(ifmt->name, name) < 0) &&
                    strcmp(ifmt->name, last_name) > 0) {
                    name = ifmt->name;
                    long_name = ifmt->long_name;
                    encode = 0;
                }
                if (name && strcmp(ifmt->name, name) == 0)
                    decode = 1;
            }
            if (!name)
                break;
            last_name = name;

            Json::Value objFormat;
            objFormat["n"] = name;
            objFormat["nl"] = (long_name) ? long_name : name;
            objFormat["d"] = (decode == 1);
            objFormat["m"] = (encode == 1);

            vecFormats[cnt] = objFormat;

            cnt++;
        }
        avformat_network_deinit();

        return vecFormats;
    }
    Json::Value buildEncoderDecoder(int encoder) {
    using namespace std;
    Json::Value vec;
    const AVCodecDescriptor **codecs;
    unsigned i, nb_codecs = get_codecs_sorted(&codecs);
    int cnt = 0;
    avcodec_register_all();

    for (i = 0; i < nb_codecs; i++) {
        const AVCodecDescriptor *desc = codecs[i];
        const AVCodec *codec = NULL;

        while ((codec = next_codec_for_id(desc->id, codec, encoder))) {
            Json::Value obj;
            obj["n"] = codec->name;

            if (strcmp(codec->name, desc->name))
                obj["nl"] = string(codec->long_name) + " (codec " + string(desc->name) + ")";
            else
                obj["nl"] = codec->long_name;

            switch (desc->type) {
                case AVMEDIA_TYPE_VIDEO:
                    obj["v"] = true;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    obj["a"] = true;
                    break;
                case AVMEDIA_TYPE_SUBTITLE:
                    obj["s"] = true;
                    break;
                default:
                    break;
            }

            if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
                obj["flm"] = true;
            if (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
                obj["slm"] = true;
            if (codec->capabilities & AV_CODEC_CAP_EXPERIMENTAL)
                obj["ex"] = true;
            if (codec->capabilities & AV_CODEC_CAP_DRAW_HORIZ_BAND)
                obj["hb"] = true;
            if (codec->capabilities & AV_CODEC_CAP_DR1)
                obj["dr"] = true;

            vec[cnt] = obj;
            cnt++;
        }
    }
    av_free(codecs);

    return vec;
}

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getDevices(JNIEnv *env, jobject instance) {
#if CONFIG_AVDEVICE
        avdevice_register_all();
#endif
        Json::Value vecDevices;
        vecDevices = getAvailableFormatsDevices(1);
        if(vecDevices.size() == 0)
            return env->NewStringUTF("");
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vecDevices).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getAvailableFormats(JNIEnv *env, jobject instance) {
        using namespace std;
        using namespace boost;

        Json::Value vecFormats;
        vecFormats = getAvailableFormatsDevices(0);

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vecFormats).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getSampleFormats(JNIEnv *env, jobject instance) {
        using namespace std;
        using namespace boost;
        Json::Value vecFormats;
        int i;
        int cnt = 0;
        char fmt_str[128];
        for (i = -1; i < AV_SAMPLE_FMT_NB; i++) {
            Json::Value objFormat;
            vector<string> partsList;
            string str = string(av_get_sample_fmt_string(fmt_str, sizeof(fmt_str), (AVSampleFormat)i));
            split(partsList, str, boost::is_any_of(" "));

            if (partsList.at(0) == "name") continue;
            cnt++;
            objFormat["n"] = partsList.at(0);
            for (std::vector<string>::const_iterator p = partsList.begin(); p != partsList.end(); ++p) {
                if (p->empty()) continue;
                objFormat["d"] = p->data();
                vecFormats[cnt] = objFormat;
            }
        }
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vecFormats).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getBitStreamFilters(JNIEnv *env, jobject instance) {
        Json::Value vec;
        int len = 0;
        int cnt = 0;
        AVBitStreamFilter *bsf = NULL;
        while ((bsf = av_bitstream_filter_next(bsf)))
            len++;
        while ((bsf = av_bitstream_filter_next(bsf))) {
            Json::Value obj;
            obj["n"] = bsf->name;
            vec[cnt] = obj;
            cnt++;
        }
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vec).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getCodecs(JNIEnv *env, jobject instance) {

        using namespace std;
        Json::Value vecCodecs;
        const AVCodecDescriptor **codecs;
        unsigned i, nb_codecs = get_codecs_sorted(&codecs);

        if(nb_codecs == 0)
            return env->NewStringUTF("");
        avcodec_register_all();

        for (i = 0; i < nb_codecs; i++) {
            const AVCodecDescriptor *desc = codecs[i];

            if (strstr(desc->name, "_deprecated"))
                continue;
            Json::Value objCodec;

            objCodec["n"] = desc->name;
            objCodec["nl"] = desc->long_name;

            objCodec["d"] = (avcodec_find_decoder(desc->id));
            objCodec["e"] = (avcodec_find_encoder(desc->id));

            switch (desc->type) {
                case AVMEDIA_TYPE_VIDEO:
                    objCodec["v"] = true;
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    objCodec["a"] = true;
                    break;
                case AVMEDIA_TYPE_SUBTITLE:
                    objCodec["s"] = true;
                    break;
                default:
                    break;
            }

            if((desc->props & AV_CODEC_PROP_LOSSY))
                objCodec["ly"] = true;
            if((desc->props & AV_CODEC_PROP_LOSSLESS))
                objCodec["ll"] = true;
            if((desc->props & AV_CODEC_PROP_INTRA_ONLY))
                objCodec["in"] = true;

            vecCodecs[i] = objCodec;
        }

        av_free(codecs);

        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vecCodecs).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getDecoders(JNIEnv *env, jobject instance) {
        Json::Value vec;
        vec = buildEncoderDecoder(0);
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vec).c_str();
        return env->NewStringUTF(json_str);
    }

    JNIEXPORT jstring JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1getEncoders(JNIEnv *env, jobject instance) {
        Json::Value vec;
        vec = buildEncoderDecoder(1);
        Json::FastWriter fastWriter;
        const char* json_str = fastWriter.write(vec).c_str();
        return env->NewStringUTF(json_str);
    }



    JNIEXPORT void JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1setLogLevel(JNIEnv *env, jobject instance, jint level) {
        logLevel = level;
    }


    void threadEncode(int p) {
        int ret = -1;
        boost::mutex mutex;
        using boost::this_thread::get_id;
        using namespace std;


        JavaVM *javaVM = g_ctx.javaVM;
        JNIEnv *env;
        jint res = javaVM->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (res != JNI_OK) {
            res = javaVM->AttachCurrentThread(&env, NULL);
            if (JNI_OK != res) {
                return;
            } else{
                g_ctx.env = env;
                g_ctx.asyncEventFunc = env->GetMethodID(g_ctx.jniHelperClz, "dispatchStatusEventAsync", "(Ljava/lang/String;Ljava/lang/String;)V");
            }
        }

        mutex.lock();
        ////////////////// ************************ //////////////////

        char ** charVec = new char*[inputContext.commandLine.size()];
        for (size_t i = 0; i < inputContext.commandLine.size(); i++) {
            charVec[i] = new char[inputContext.commandLine[i].size() + 1];
            strcpy(charVec[i], inputContext.commandLine[i].c_str());
        }
        //free(charVec);

        setvbuf(stderr, NULL, _IONBF, 0);
        av_log_set_flags(AV_LOG_SKIP_REPEATED);
        av_log_set_callback(&avaneLog);

        avcodec_register_all();
    #if CONFIG_AVDEVICE
        avdevice_register_all();
    #endif
    #if CONFIG_AVFILTER
        avfilter_register_all();
    #endif
        av_register_all();
        avformat_network_init();

        ret = ffmpeg_parse_options(inputContext.commandLine.size(), charVec);

        avane_set_pause_transcode(0);
        isEncoding = true;
        std::string returnVal = "";

        if (ret > -1) {
            dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc,returnVal.c_str(), "ON_ENCODE_START");
            ret = avane_main_transcode();
        }
        isEncoding = false;

        if (ret < 0)
            dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc,returnVal.c_str(), "ON_ENCODE_ERROR");
        else
            dispatchJniEventAsync(g_ctx.env, g_ctx.jniHelperObj, g_ctx.asyncEventFunc,returnVal.c_str(), "ON_ENCODE_FINISH");

        avane_main_cleanup();

        ////////////////// ************************ //////////////////
        mutex.unlock();
        //problems with trace or log info ?
        javaVM->DetachCurrentThread();

    }


    JNIEXPORT void JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1encode(JNIEnv *env, jobject instance,
                                                       jobjectArray stringArray) {
        using namespace std;
        int stringCount = env->GetArrayLength(stringArray);
        inputContext.commandLine.clear();
        for (int i=0; i<stringCount; i++) {
            jstring str = (jstring) (env->GetObjectArrayElement(stringArray, i));
            const char *rawString = env->GetStringUTFChars(str, 0);
            string valueAsString = string(rawString);
            inputContext.commandLine.push_back(valueAsString);
            // Don't forget to call `ReleaseStringUTFChars` when you're done.
            env->ReleaseStringUTFChars(str,rawString);

        }
        threads[0] = boost::move(createThread(&threadEncode, 1));
    }

    JNIEXPORT void JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1cancelEncode(JNIEnv *env, jobject instance) {
        // TODO
        if(isEncoding)
            avane_set_cancel_transcode(1);
        isEncoding = false;
    }

    JNIEXPORT void JNICALL
    Java_com_tuarua_avane_android_LibAVANE_jni_1pauseEncode(JNIEnv *env, jobject instance,
                                                            jboolean value) {
        avane_set_pause_transcode((value) ? 1 : 0);
    }

    JNIEXPORT jstring JNICALL
        Java_com_tuarua_avane_android_LibAVANE_jni_1getVersion(JNIEnv *env, jobject instance) {
        using namespace boost;
        std::stringstream ss;
        ss << "ffmpeg " << std::string(FFMPEG_VERSION) << std::endl;
#if CONFIG_AVUTIL
        ss << "libavutil " << format("%2d.%3d.%3d") % LIBAVUTIL_VERSION_MAJOR % LIBAVUTIL_VERSION_MINOR % LIBAVUTIL_VERSION_MICRO << std::endl;
#endif
#if CONFIG_AVCODEC
        ss << "libavcodec " << format("%2d.%3d.%3d") % LIBAVCODEC_VERSION_MAJOR % LIBAVCODEC_VERSION_MINOR % LIBAVCODEC_VERSION_MICRO << std::endl;
#endif
#if CONFIG_AVFORMAT
        ss << "libavformat " << format("%2d.%3d.%3d") % LIBAVFORMAT_VERSION_MAJOR % LIBAVFORMAT_VERSION_MINOR % LIBAVFORMAT_VERSION_MICRO << std::endl;
#endif
#if (CONFIG_AVDEVICE)
        ss << "libavdevice " << format("%2d.%3d.%3d") % LIBAVDEVICE_VERSION_MAJOR % LIBAVDEVICE_VERSION_MINOR % LIBAVDEVICE_VERSION_MICRO << std::endl;
#endif
#if (CONFIG_AVFILTER)
        ss << "libavfilter " << format("%2d.%3d.%3d") % LIBAVFILTER_VERSION_MAJOR % LIBAVFILTER_VERSION_MINOR % LIBAVFILTER_VERSION_MICRO << std::endl;
#endif
#if (CONFIG_AVRESAMPLE)
        ss << "libavresample " << format("%2d.%3d.%3d") % LIBAVRESAMPLE_VERSION_MAJOR % LIBAVRESAMPLE_VERSION_MINOR % LIBAVRESAMPLE_VERSION_MICRO << std::endl;
#endif
#if (CONFIG_SWSCALE)
        ss << "libavswscale " << format("%2d.%3d.%3d") % LIBSWSCALE_VERSION_MAJOR % LIBSWSCALE_VERSION_MINOR % LIBSWSCALE_VERSION_MICRO << std::endl;
#endif
#if (CONFIG_SWRESAMPLE)
        ss << "libswresample " << format("%2d.%3d.%3d") % LIBSWRESAMPLE_VERSION_MAJOR % LIBSWRESAMPLE_VERSION_MINOR % LIBSWRESAMPLE_VERSION_MICRO << std::endl;
#endif
#if (CONFIG_POSTPROC)
        ss << "libpostproc " << format("%2d.%3d.%3d") % LIBPOSTPROC_VERSION_MAJOR % LIBPOSTPROC_VERSION_MINOR % LIBPOSTPROC_VERSION_MICRO << std::endl;
#endif
        std::string ret = ss.str();
        return env->NewStringUTF(ret.c_str());
    }
}