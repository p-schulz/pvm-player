// Library-load hook. Runs on the Java main thread, where the app's classes
// are visible, so this is the place to look up anything native worker threads
// will need later.

#include <android/log.h>
#include <jni.h>

#include "platform/android/http_fetch_android.h"

#if defined(PVM_HAVE_LIBMPV)
// Exported by the FFmpeg inside libmpv: MediaCodec decoding reaches Java
// through JNI and needs the JavaVM, which must be set before mpv is created.
extern "C" int av_jni_set_java_vm(void* vm, void* logContext);
#endif

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (!teletext::androidHttpInit(env)) {
        __android_log_print(ANDROID_LOG_ERROR, "PVM", "HTTP transport setup failed");
    }
#if defined(PVM_HAVE_LIBMPV)
    if (av_jni_set_java_vm(vm, nullptr) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "PVM", "av_jni_set_java_vm failed; hardware decoding is unavailable");
    }
#endif
    return JNI_VERSION_1_6;
}
