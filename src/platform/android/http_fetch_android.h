#pragma once

#include <jni.h>

namespace teletext {

// Resolves and caches what the JNI HTTP transport needs (the JavaVM, the
// app.pvmplayer.HttpClient class and its method/field IDs) and registers its
// native callback. Must run on a thread that can see the app's classes -- in
// practice JNI_OnLoad, on the Java main thread -- because FindClass on a
// native worker thread only sees system classes. Returns false (having
// logged why) if the Java side is missing.
bool androidHttpInit(JNIEnv* env);

}  // namespace teletext
