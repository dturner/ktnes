#include <memory>
#include "AudioEngine.h"
#include "../../../../../../oboe/src/common/OboeDebug.h"

void AudioEngine::start(
    std::vector<int> cpuIds,
    JavaVM *javaVm,
    jobject classLoader,
    jmethodID findClassMethod
) {
  LOGD("AudioEngine start()");
  mCpuIds = cpuIds;
  mJavaVM = javaVm;
  mClassLoader = classLoader;
  mFindClassMethod = findClassMethod;
  AudioStreamBuilder builder;
  builder.setCallback(this);
  builder.setPerformanceMode(PerformanceMode::LowLatency);
  builder.setFormat(AudioFormat::Float);
  builder.setChannelCount(1);
  builder.setSharingMode(SharingMode::Exclusive);
  Result result = builder.openStream(&mStream);
  if (result != Result::OK) {
    LOGE("Failed to open stream. Error: %s", convertToText(result));
    return;
  }
  mStream->setBufferSizeInFrames(mStream->getFramesPerBurst() * 2);
  mStream->requestStart();
}

void AudioEngine::stop() {
  LOGD("AudioEngine stop()");
  if (mStream != nullptr) {
    mStream->close();
  }
}

JNIEnv *AudioEngine::getEnv() {
  JNIEnv *env;
  int status = mJavaVM->GetEnv((void **) &env, JNI_VERSION_1_6);
  if (status < 0) {
    status = mJavaVM->AttachCurrentThread(&env, nullptr);
    if (status < 0) {
      return nullptr;
    }
  }
  return env;
}


jclass AudioEngine::findClass(JNIEnv *env, const char *name) {
  return static_cast<jclass>(
      env->CallObjectMethod(mClassLoader, mFindClassMethod, env->NewStringUTF(name)));
}

void AudioEngine::maybeInitAudioBufferMethod(JNIEnv *jniEnv) {
  if (mMainActivityClass == nullptr) {
    mMainActivityClass = findClass(jniEnv, "com/felipecsl/knes/app/MainActivity");
    mAudioBufferMethod = jniEnv->GetStaticMethodID(mMainActivityClass, "audioBuffer", "()[F");
  }
}

DataCallbackResult AudioEngine::onAudioReady(
    AudioStream *oboeStream,
    void *audioData,
    int32_t numFrames
) {
  JNIEnv *jniEnv = getEnv();
  if (!mIsThreadAffinitySet) setThreadAffinity();
  maybeInitAudioBufferMethod(jniEnv);
  auto arr = (jfloatArray) jniEnv->CallStaticObjectMethod(mMainActivityClass, mAudioBufferMethod);
  int length = jniEnv->GetArrayLength(arr);
  jboolean isCopy;
  jfloat *elements = jniEnv->GetFloatArrayElements(arr, &isCopy);
  auto *floatAudioData = static_cast<float *>(audioData);
  for (int i = 0; i < length; i++) {
    floatAudioData[i] = elements[i];
  }
  return DataCallbackResult::Continue;
}

/**
 * Set the thread affinity for the current thread to mCpuIds. This can be useful to call on the
 * audio thread to avoid underruns caused by CPU core migrations to slower CPU cores.
 */
void AudioEngine::setThreadAffinity() {
  pid_t current_thread_id = gettid();
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);

  // If the callback cpu ids aren't specified then bind to the current cpu
  if (mCpuIds.empty()) {
    int current_cpu_id = sched_getcpu();
    LOGV("Current CPU ID is %d", current_cpu_id);
    CPU_SET(current_cpu_id, &cpu_set);
  } else {

    for (int cpu_id : mCpuIds) {
      LOGV("CPU ID %d added to cores set", cpu_id);
      CPU_SET(cpu_id, &cpu_set);
    }
  }

  int result = sched_setaffinity(current_thread_id, sizeof(cpu_set_t), &cpu_set);
  if (result == 0) {
    LOGV("Thread affinity set");
  } else {
    LOGW("Error setting thread affinity. Error no: %d", result);
  }

  mIsThreadAffinitySet = true;
}