#ifndef FRIDANODE_APPLICATION_H
#define FRIDANODE_APPLICATION_H

#include "glib_object.h"

#include <frida-core.h>
#include <nan.h>

namespace frida {

class Application : public GLibObject {
 public:
  static void Init(v8::Handle<v8::Object> exports, Runtime* runtime);
  static v8::Local<v8::Object> New(gpointer handle, Runtime* runtime);

 private:
  explicit Application(FridaApplication* handle, Runtime* runtime);
  ~Application();

  static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);

  static void GetIdentifier(v8::Local<v8::String> property,
      const Nan::PropertyCallbackInfo<v8::Value>& info);
  static void GetName(v8::Local<v8::String> property,
      const Nan::PropertyCallbackInfo<v8::Value>& info);
  static void GetPid(v8::Local<v8::String> property,
      const Nan::PropertyCallbackInfo<v8::Value>& info);
  static void GetSmallIcon(v8::Local<v8::String> property,
      const Nan::PropertyCallbackInfo<v8::Value>& info);
  static void GetLargeIcon(v8::Local<v8::String> property,
      const Nan::PropertyCallbackInfo<v8::Value>& info);
};

}

#endif
