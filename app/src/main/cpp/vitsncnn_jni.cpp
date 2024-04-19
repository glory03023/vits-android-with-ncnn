#include <jni.h>
#include "openjtalk/api/api.h"
#include "vits/SynthesizerTrn.h"
#include "wave_utils/wave.h"

static ncnn::UnlockedPoolAllocator g_blob_pool_allocator;
static ncnn::PoolAllocator g_workspace_pool_allocator;
static SynthesizerTrn* net_g = nullptr;
static OpenJtalk openJtalk;
static Option opt;

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    LOGD("JNI_OnLoad");
    ncnn::create_gpu_instance();
    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM *vm, void *reserved) {
    LOGD("JNI_OnUnload");
    ncnn::destroy_gpu_instance();
    if (net_g != nullptr){
        delete net_g;
        net_g = nullptr;
    }
}

// vits utils
extern "C" {
JNIEXPORT jboolean JNICALL
Java_com_example_moereng_utils_text_JapaneseCleaners_initOpenJtalk(JNIEnv *env, jobject thiz,
                                                                    jobject asset_manager) {
    AssetJNI assetJni(env, thiz, asset_manager);
    bool state = openJtalk.init("open_jtalk_dic_utf_8-1.11", &assetJni);
    if (state) return JNI_TRUE;
    else return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_moereng_utils_VitsUtils_testGpu(JNIEnv *env, jobject thiz) {
    int n = get_gpu_count();
    if (n != 0) return JNI_TRUE;
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_example_moereng_utils_VitsUtils_checkThreadsCpp(JNIEnv *env, jobject thiz) {
    return ncnn::get_physical_big_cpu_count();
}

JNIEXPORT jobject JNICALL
Java_com_example_moereng_utils_text_JapaneseCleaners_extract_1labels(JNIEnv *env, jobject thiz,
                                                                     jstring text) {
    char *ctext = (char *) env->GetStringUTFChars(text, nullptr);
    // 转换编码
    string stext(ctext);
    wstring wtext = utf8_decode(stext);
    jclass array_list_class = env->FindClass("java/util/ArrayList");
    jmethodID array_list_constructor = env->GetMethodID(array_list_class, "<init>", "()V");
    jobject array_list = env->NewObject(array_list_class, array_list_constructor);
    jmethodID array_list_add = env->GetMethodID(array_list_class, "add", "(Ljava/lang/Object;)Z");

    auto features = openJtalk.run_frontend(wtext);
    auto labels = openJtalk.make_label(features);

    // vector到列表
    for (const wstring &label: labels) {
        jstring str = env->NewStringUTF(utf8_encode(label).c_str());
        env->CallBooleanMethod(array_list, array_list_add, str);
        env->DeleteLocalRef(str);
    }
    return array_list;
}

// vits model
JNIEXPORT jboolean JNICALL
Java_com_example_moereng_Vits_init_1vits(JNIEnv *env, jobject thiz, jobject asset_manager,
                                         jstring path, jboolean voice_convert, jboolean multi,
                                         jint n_vocab) {
    if (net_g != nullptr) {
        delete net_g;
        net_g = nullptr;
    }

    net_g = new SynthesizerTrn();

    const char *_path = env->GetStringUTFChars(path, nullptr);

    opt.lightmode = true;
    opt.use_packing_layout = true;
    opt.use_fp16_storage = true;
    opt.num_threads = get_big_cpu_count();
    opt.blob_allocator = &g_blob_pool_allocator;
    opt.workspace_allocator = &g_workspace_pool_allocator;

    // use vulkan compute
    if (ncnn::get_gpu_count() != 0)
        opt.use_vulkan_compute = true;

    auto assetManager = AAssetManager_fromJava(env, asset_manager);

    bool ret = net_g->init(_path, voice_convert, multi, n_vocab, assetManager, opt);
    if (ret) return JNI_TRUE;
    else return JNI_FALSE;
}

JNIEXPORT jfloatArray JNICALL
Java_com_example_moereng_Vits_forward(JNIEnv *env, jobject thiz, jintArray x, jboolean vulkan,
                                      jboolean multi, jint sid,
                                      jfloat noise_scale,
                                      jfloat noise_scale_w,
                                      jfloat length_scale,
                                      jint num_threads) {
    if (net_g == nullptr) return {};

    // jarray to ncnn mat
    int *x_ = env->GetIntArrayElements(x, nullptr);
    jsize x_size = env->GetArrayLength(x);

    Mat data(x_size, 1);
    for (int i = 0; i < data.c; i++) {
        float *p = data.channel(i);
        for (int j = 0; j < x_size; j++) {
            p[j] = (float) x_[j];
        }
    }

    // inference
    if (vulkan) LOGI("vulkan on");
    else
        LOGI("vulkan off");

    opt.num_threads = num_threads;
    LOGD("threads = %d", opt.num_threads);
    auto start = get_current_time();
    auto output = net_g->forward(data, opt, vulkan, multi, sid,
                                          noise_scale, noise_scale_w, length_scale);
    if (output.empty()) return {};
    auto end = get_current_time();
    LOGI("time cost: %f ms", end - start);
    jfloatArray res = env->NewFloatArray(output.h * output.w);
    env->SetFloatArrayRegion(res, 0, output.w * output.h, output);
    return res;
}

JNIEXPORT jfloatArray JNICALL
Java_com_example_moereng_Vits_voice_1convert(JNIEnv *env, jobject thiz, jfloatArray audio,
                                             jint raw_sid, jint target_sid, jboolean vulkan,
                                             jint num_threads) {
    if (net_g == nullptr) return {};

    // audio to ncnn mat
    float *audio_ = env->GetFloatArrayElements(audio, nullptr);
    jsize audio_size = env->GetArrayLength(audio);
    Mat audio_mat(audio_size, 1);
    for (int i = 0; i < audio_mat.c; i++) {
        float *p = audio_mat.channel(i);
        for (int j = 0; j < audio_size; j++) {
            p[j] = audio_[j];
        }
    }

    // voice conversion
    if (vulkan) LOGI("vulkan on");
    else
        LOGI("vulkan off");

    opt.num_threads = num_threads;
    LOGD("threads = %d", opt.num_threads);
    auto start = get_current_time();
    auto output = net_g->voice_convert(audio_mat, raw_sid, target_sid, opt,
                                                vulkan);
    auto end = get_current_time();
    LOGI("time cost: %f ms", end - start);
    jfloatArray res = env->NewFloatArray(output.h * output.w);
    env->SetFloatArrayRegion(res, 0, output.w * output.h, output);
    return res;
}

// wave utils
JNIEXPORT jbyteArray JNICALL
Java_com_example_moereng_utils_audio_WaveUtils_convertAudioPCMToWaveByteArray(JNIEnv *env,
                                      jobject thiz,
                                      jfloatArray jaudio,
                                      jint sampling_rate) {
    float *audio = env->GetFloatArrayElements(jaudio, nullptr);
    jsize audio_size = env->GetArrayLength(jaudio);
    // convert audio
    auto wave = PCMToWavFormat(audio, audio_size, sampling_rate);
    if (wave == nullptr) return nullptr;
    auto size = static_cast<jsize>(audio_size * 4 + 44);
    jbyteArray out = env->NewByteArray(size);
    env->SetByteArrayRegion(out, 0, size, reinterpret_cast<const jbyte *>(wave));
    return out;
}
}