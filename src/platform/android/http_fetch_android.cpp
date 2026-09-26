// HTTP transport for Android: the same httpGet()/httpPostJson() as the libcurl
// one in teletext/http_fetch.cpp, implemented by app.pvmplayer.HttpClient
// (HttpURLConnection) through JNI. The teletext services call it from their
// own std::threads, so every call attaches the calling thread to the JVM
// (once per thread, detached again when the thread exits) and uses only IDs
// cached up front by androidHttpInit().

#include "teletext/http_fetch.h"

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <cstdint>

#include "platform/android/http_fetch_android.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "PVM", __VA_ARGS__)

namespace teletext {

namespace {

JavaVM* gVm = nullptr;
jclass gClientClass = nullptr;  // global refs: a native thread's FindClass can't find app classes
jclass gResultClass = nullptr;
jmethodID gRequest = nullptr;
jfieldID gResultStatus = nullptr;
jfieldID gResultBody = nullptr;
jfieldID gResultError = nullptr;

// Called by HttpClient's watchdog thread to poll the caller's cancel flag.
jboolean JNICALL nativeIsCancelled(JNIEnv*, jclass, jlong token) {
    const auto* flag = reinterpret_cast<const std::atomic<bool>*>(static_cast<intptr_t>(token));
    return flag && flag->load() ? JNI_TRUE : JNI_FALSE;
}

// The JNIEnv for the calling thread, attaching it to the JVM on first use.
// The thread-local guard detaches it again when the thread ends.
class ThreadEnv {
public:
    static JNIEnv* get() {
        thread_local ThreadEnv instance;
        return instance.env_;
    }

private:
    ThreadEnv() {
        if (!gVm) {
            return;
        }
        if (gVm->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6) == JNI_OK) {
            return;  // already attached by someone else (e.g. the Java main thread): not ours to detach
        }
        if (gVm->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
            attached_ = true;
        } else {
            env_ = nullptr;
        }
    }
    ~ThreadEnv() {
        if (attached_) {
            gVm->DetachCurrentThread();
        }
    }
    ThreadEnv(const ThreadEnv&) = delete;
    ThreadEnv& operator=(const ThreadEnv&) = delete;

    JNIEnv* env_ = nullptr;
    bool attached_ = false;
};

FetchResult httpRequest(const std::string& url, const FetchOptions& options, const std::string* postBody) {
    FetchResult result;
    JNIEnv* env = ThreadEnv::get();
    if (!env || !gRequest) {
        result.error = "HTTP transport is not initialised";
        return result;
    }

    jstring jUrl = env->NewStringUTF(url.c_str());
    jstring jUserAgent = env->NewStringUTF(options.userAgent.c_str());
    jbyteArray jBody = nullptr;
    if (postBody) {
        jBody = env->NewByteArray(static_cast<jsize>(postBody->size()));
        if (jBody) {
            env->SetByteArrayRegion(jBody, 0, static_cast<jsize>(postBody->size()),
                                    reinterpret_cast<const jbyte*>(postBody->data()));
        }
    }

    // The flag is only read while this call is in progress (the watchdog is
    // finished before request() returns), so passing its address is safe.
    const jlong cancelToken = static_cast<jlong>(reinterpret_cast<intptr_t>(options.cancel));
    jobject jResult = env->CallStaticObjectMethod(
        gClientClass, gRequest, jUrl, jBody, jUserAgent,
        static_cast<jint>(options.connectTimeoutSeconds * 1000), static_cast<jint>(options.totalTimeoutSeconds * 1000),
        static_cast<jlong>(options.maxBodyBytes), cancelToken);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        result.error = "HTTP transport threw an exception";
    } else if (jResult) {
        result.status = env->GetIntField(jResult, gResultStatus);
        auto* jError = static_cast<jstring>(env->GetObjectField(jResult, gResultError));
        auto* jResponse = static_cast<jbyteArray>(env->GetObjectField(jResult, gResultBody));
        if (jError) {
            const char* chars = env->GetStringUTFChars(jError, nullptr);
            result.error = chars ? chars : "unknown error";
            if (chars) {
                env->ReleaseStringUTFChars(jError, chars);
            }
        } else if (jResponse) {
            const jsize length = env->GetArrayLength(jResponse);
            result.body.resize(static_cast<size_t>(length));
            env->GetByteArrayRegion(jResponse, 0, length, reinterpret_cast<jbyte*>(result.body.data()));
            result.ok = true;
        } else {
            result.error = "empty response";
        }
        if (jError) env->DeleteLocalRef(jError);
        if (jResponse) env->DeleteLocalRef(jResponse);
    } else {
        result.error = "HTTP transport returned nothing";
    }

    if (jResult) env->DeleteLocalRef(jResult);
    if (jBody) env->DeleteLocalRef(jBody);
    env->DeleteLocalRef(jUserAgent);
    env->DeleteLocalRef(jUrl);
    return result;
}

}  // namespace

bool androidHttpInit(JNIEnv* env) {
    if (env->GetJavaVM(&gVm) != JNI_OK) {
        LOGE("GetJavaVM failed");
        return false;
    }

    jclass client = env->FindClass("app/pvmplayer/HttpClient");
    jclass result = env->FindClass("app/pvmplayer/HttpClient$Result");
    if (!client || !result) {
        env->ExceptionClear();
        LOGE("app.pvmplayer.HttpClient not found; HTTP is unavailable");
        return false;
    }
    gClientClass = static_cast<jclass>(env->NewGlobalRef(client));
    gResultClass = static_cast<jclass>(env->NewGlobalRef(result));
    gRequest = env->GetStaticMethodID(
        client, "request", "(Ljava/lang/String;[BLjava/lang/String;IIJJ)Lapp/pvmplayer/HttpClient$Result;");
    gResultStatus = env->GetFieldID(result, "status", "I");
    gResultBody = env->GetFieldID(result, "body", "[B");
    gResultError = env->GetFieldID(result, "error", "Ljava/lang/String;");

    const JNINativeMethod natives[] = {
        {"nativeIsCancelled", "(J)Z", reinterpret_cast<void*>(nativeIsCancelled)},
    };
    if (!gRequest || !gResultStatus || !gResultBody || !gResultError ||
        env->RegisterNatives(client, natives, 1) != JNI_OK) {
        env->ExceptionClear();
        LOGE("HttpClient does not have the expected shape; HTTP is unavailable");
        gRequest = nullptr;
        return false;
    }
    return true;
}

FetchResult httpGet(const std::string& url, const FetchOptions& options) {
    return httpRequest(url, options, nullptr);
}

FetchResult httpPostJson(const std::string& url, const std::string& jsonBody, const FetchOptions& options) {
    return httpRequest(url, options, &jsonBody);
}

}  // namespace teletext
