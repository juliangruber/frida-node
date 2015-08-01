#include "session.h"

#include "events.h"
#include "operation.h"
#include "script.h"
#include "usage_monitor.h"

#include <nan.h>
#include <node.h>

#define SESSION_DATA_CONSTRUCTOR "session:ctor"

using v8::AccessorSignature;
using v8::DEFAULT;
using v8::Exception;
using v8::External;
using v8::Function;
using v8::Handle;
using v8::Integer;
using v8::Isolate;
using v8::Local;
using v8::Object;
using v8::Persistent;
using v8::ReadOnly;
using v8::String;
using v8::Value;
using Nan::HandleScope;

namespace frida {

Session::Session(FridaSession* handle, Runtime* runtime)
    : GLibObject(handle, runtime) {
  g_object_ref(handle_);
}

Session::~Session() {
  events_.Reset();
  frida_unref(handle_);
}

void Session::Init(Handle<Object> exports, Runtime* runtime) {
  auto isolate = Isolate::GetCurrent();

  auto name = Nan::New("Session").ToLocalChecked();
  auto tpl = CreateTemplate(isolate, name, Session::New, runtime);

  auto instance_tpl = tpl->InstanceTemplate();
  auto data = Handle<Value>();
  auto signature = AccessorSignature::New(isolate, tpl);
  Nan::SetAccessor(instance_tpl, Nan::New("pid").ToLocalChecked(), GetPid, 0,
      data, DEFAULT, ReadOnly, signature);

  Nan::SetPrototypeMethod(tpl, "detach", Detach);
  Nan::SetPrototypeMethod(tpl, "createScript", CreateScript);
  Nan::SetPrototypeMethod(tpl, "enableDebugger", EnableDebugger);
  Nan::SetPrototypeMethod(tpl, "disableDebugger", DisableDebugger);

  auto ctor = Nan::GetFunction(tpl).ToLocalChecked();
  Nan::Set(exports, name, ctor);
  runtime->SetDataPointer(SESSION_DATA_CONSTRUCTOR,
      new v8::Persistent<Function>(isolate, ctor));
}

Local<Object> Session::New(gpointer handle, Runtime* runtime) {
  auto isolate = Isolate::GetCurrent();

  auto ctor = Local<Function>::New(isolate,
      *static_cast<v8::Persistent<Function>*>(
      runtime->GetDataPointer(SESSION_DATA_CONSTRUCTOR)));
  const int argc = 1;
  Local<Value> argv[argc] = { External::New(isolate, handle) };
  return ctor->NewInstance(argc, argv);
}

void Session::New(const Nan::FunctionCallbackInfo<Value>& info) {
  HandleScope();

  if (info.IsConstructCall()) {
    if (info.Length() != 1 || !info[0]->IsExternal()) {
      Nan::ThrowTypeError("Bad argument, expected raw handle");
      return;
    }
    auto runtime = GetRuntimeFromConstructorArgs(info);

    auto handle = static_cast<FridaSession*>(
        Local<External>::Cast(info[0])->Value());
    auto wrapper = new Session(handle, runtime);
    auto obj = info.This();
    wrapper->Wrap(obj);
    Nan::Set(obj, Nan::New("events").ToLocalChecked(),
        Events::New(handle, runtime));

    auto monitor =
        new UsageMonitor<FridaSession>(frida_session_is_detached, "detached");
    monitor->Enable(wrapper);

    info.GetReturnValue().Set(obj);
  } else {
    info.GetReturnValue().Set(info.Callee()->NewInstance(0, NULL));
  }
}

void Session::GetPid(Local<String> property,
    const Nan::PropertyCallbackInfo<Value>& info) {
  HandleScope();

  auto isolate = info.GetIsolate();
  auto handle = ObjectWrap::Unwrap<Session>(
      info.Holder())->GetHandle<FridaSession>();

  info.GetReturnValue().Set(
      Integer::NewFromUnsigned(isolate, frida_session_get_pid(handle)));
}

class DetachOperation : public Operation<FridaSession> {
 public:
  void Begin() {
    frida_session_detach(handle_, OnReady, this);
  }

  void End(GAsyncResult* result, GError** error) {
    frida_session_detach_finish(handle_, result);
  }

  Local<Value> Result(Isolate* isolate) {
    return Undefined(isolate);
  }
};

void Session::Detach(const Nan::FunctionCallbackInfo<Value>& info) {
  HandleScope();

  auto isolate = info.GetIsolate();
  auto obj = info.Holder();
  auto wrapper = ObjectWrap::Unwrap<Session>(obj);

  auto operation = new DetachOperation();
  operation->Schedule(isolate, wrapper);

  info.GetReturnValue().Set(operation->GetPromise(isolate));
}

class CreateScriptOperation : public Operation<FridaSession> {
 public:
  CreateScriptOperation(gchar* name, gchar* source)
    : name_(name),
      source_(source) {
  }

  ~CreateScriptOperation() {
    g_free(source_);
    g_free(name_);
  }

  void Begin() {
    frida_session_create_script(handle_, name_, source_, OnReady, this);
  }

  void End(GAsyncResult* result, GError** error) {
    script_ = frida_session_create_script_finish(handle_, result, error);
  }

  Local<Value> Result(Isolate* isolate) {
    auto wrapper = Script::New(script_, runtime_);
    g_object_unref(script_);
    return wrapper;
  }

  gchar* name_;
  gchar* source_;
  FridaScript* script_;
};

void Session::CreateScript(const Nan::FunctionCallbackInfo<Value>& info) {
  HandleScope();

  auto isolate = info.GetIsolate();
  auto obj = info.Holder();
  auto wrapper = ObjectWrap::Unwrap<Session>(obj);

  if (info.Length() < 2 ||
      !(info[0]->IsString() || info[0]->IsNull()) ||
      !info[1]->IsString()) {
    Nan::ThrowTypeError("Bad argument, expected string|null and string");
    return;
  }
  gchar* name = NULL;
  if (info[0]->IsString()) {
    String::Utf8Value val(Local<String>::Cast(info[0]));
    name = g_strdup(*val);
  }
  String::Utf8Value source(Local<String>::Cast(info[1]));

  auto operation = new CreateScriptOperation(name, g_strdup(*source));
  operation->Schedule(isolate, wrapper);

  info.GetReturnValue().Set(operation->GetPromise(isolate));
}

class EnableDebuggerOperation : public Operation<FridaSession> {
 public:
  EnableDebuggerOperation(guint16 port) : port_(port) {
  }

  void Begin() {
    frida_session_enable_debugger(handle_, port_, OnReady, this);
  }

  void End(GAsyncResult* result, GError** error) {
    frida_session_enable_debugger_finish(handle_, result, error);
  }

  Local<Value> Result(Isolate* isolate) {
    return Undefined(isolate);
  }

  guint16 port_;
};

void Session::EnableDebugger(const Nan::FunctionCallbackInfo<Value>& info) {
  HandleScope();

  auto isolate = info.GetIsolate();
  auto obj = info.Holder();
  auto wrapper = ObjectWrap::Unwrap<Session>(obj);

  if (info.Length() < 1 || !info[0]->IsNumber()) {
    Nan::ThrowTypeError("Bad argument, expected port number");
    return;
  }
  guint16 port = static_cast<guint16>(info[0]->ToInteger()->Value());

  auto operation = new EnableDebuggerOperation(port);
  operation->Schedule(isolate, wrapper);

  info.GetReturnValue().Set(operation->GetPromise(isolate));
}

class DisableDebuggerOperation : public Operation<FridaSession> {
 public:
  void Begin() {
    frida_session_disable_debugger(handle_, OnReady, this);
  }

  void End(GAsyncResult* result, GError** error) {
    frida_session_disable_debugger_finish(handle_, result, error);
  }

  Local<Value> Result(Isolate* isolate) {
    return Undefined(isolate);
  }
};

void Session::DisableDebugger(const Nan::FunctionCallbackInfo<Value>& info) {
  HandleScope();

  auto isolate = info.GetIsolate();
  auto obj = info.Holder();
  auto wrapper = ObjectWrap::Unwrap<Session>(obj);

  auto operation = new DisableDebuggerOperation();
  operation->Schedule(isolate, wrapper);

  info.GetReturnValue().Set(operation->GetPromise(isolate));
}

}
