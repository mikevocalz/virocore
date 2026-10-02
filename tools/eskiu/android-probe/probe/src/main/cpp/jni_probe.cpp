#include <jni.h>

extern "C" int viro_eskiu_android_probe(int value);

extern "C"
JNIEXPORT jint JNICALL
Java_com_reactvision_eskiuprobe_Probe_nativeProbe(
        JNIEnv *,
        jclass,
        jint value) {
    return static_cast<jint>(viro_eskiu_android_probe(static_cast<int>(value)));
}
