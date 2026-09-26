// Library-load hook. Runs on the Java main thread, where the app's classes
// are visible, so this is the place to look up anything native worker threads
// will need later.

#include <android/log.h>
#include <jni.h>

#include "platform/android/http_fetch_android.h"

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    if (!teletext::androidHttpInit(env)) {
        __android_log_print(ANDROID_LOG_ERROR, "PVM", "HTTP transport setup failed");
    }
    return JNI_VERSION_1_6;
}
