#include <windows.h>

#include <cwchar>
#include <memory>
#include <string>

#include "pluginapi.h"
#include "installer_window.h"

// pluginapi.h 通过这三个外部符号维护 NSIS 栈，必须使用 C 链接名。
extern "C" unsigned int g_stringsize = 0;
extern "C" stack_t** g_stacktop = nullptr;
extern "C" LPTSTR g_variables = nullptr;

namespace {

std::unique_ptr<sindre::installer::InstallerWindow> g_window;
extra_parameters* g_parameters = nullptr;

void InitPlugin(unsigned int string_size, stack_t** stack_top,
                LPTSTR variables, extra_parameters* parameters) {
  // pluginapi.h 的栈函数依赖这三个固定名称，必须在每个导出函数进入时更新。
  g_stringsize = string_size;
  g_stacktop = stack_top;
  g_variables = variables;
  g_parameters = parameters;
}

std::wstring PopString() {
  std::wstring value(g_stringsize, L'\0');
  if (value.empty() || popstring(&value[0]) != 0) {
    return {};
  }
  value.resize(wcslen(value.c_str()));
  return value;
}

void PushBool(bool value) { pushstring(value ? _T("true") : _T("false")); }

}  // namespace

extern "C" __declspec(dllexport) void Init(HWND parent, int string_size,
                                             TCHAR* variables,
                                             stack_t** stack_top,
                                             extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const std::wstring resource_directory = PopString();
  if (resource_directory.empty()) {
    PushBool(false);
    return;
  }

  g_window = std::make_unique<sindre::installer::InstallerWindow>(parent);
  if (!g_window->Create(resource_directory)) {
    g_window.reset();
    PushBool(false);
    return;
  }
  g_window->SetCallbackInvoker([](int code_segment) {
    if (g_parameters != nullptr && g_parameters->ExecuteCodeSegment != nullptr &&
        g_window != nullptr) {
      g_parameters->ExecuteCodeSegment(code_segment, g_window->GetHWND());
    }
  });
  // 仅返回成功标志，避免把 HWND 的格式化字符串跨 NSIS 栈传递时破坏栈。
  // 插件窗口句柄由插件内部保存，脚本不需要直接使用它。
  PushBool(true);
}

extern "C" __declspec(dllexport) void ShowPage(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const int page_index = _ttoi(PopString().c_str());
  PushBool(g_window != nullptr && g_window->ShowPage(page_index));
}

extern "C" __declspec(dllexport) void SetText(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const std::wstring text = PopString();
  const std::wstring name = PopString();
  PushBool(g_window != nullptr && g_window->SetText(name, text));
}

extern "C" __declspec(dllexport) void SetProgress(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const int value = _ttoi(PopString().c_str());
  PushBool(g_window != nullptr && g_window->SetProgress(value));
}

extern "C" __declspec(dllexport) void SetStepProgress(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const int value = _ttoi(PopString().c_str());
  PushBool(g_window != nullptr && g_window->SetStepProgress(value));
}

extern "C" __declspec(dllexport) void GetText(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const std::wstring name = PopString();
  std::wstring text;
  if (g_window == nullptr || !g_window->GetText(name, &text)) {
    pushstring(_T(""));
    return;
  }
  pushstring(text.c_str());
}

extern "C" __declspec(dllexport) void GetSelected(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const std::wstring name = PopString();
  bool selected = false;
  if (g_window != nullptr) {
    g_window->GetSelected(name, &selected);
  }
  PushBool(selected);
}

extern "C" __declspec(dllexport) void BindCallback(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  const int code_segment = _ttoi(PopString().c_str());
  const std::wstring name = PopString();
  if (g_window != nullptr) {
    g_window->BindCallback(name, code_segment);
  }
  PushBool(g_window != nullptr);
}

extern "C" __declspec(dllexport) void Destroy(
    HWND /*parent*/, int string_size, TCHAR* variables, stack_t** stack_top,
    extra_parameters* parameters) {
  InitPlugin(string_size, stack_top, variables, parameters);
  if (g_window != nullptr) {
    // 不在 ShowPage 的消息循环栈帧中直接 delete this，避免回调返回后访问已释放对象。
    g_window->CloseWindow();
  }
  PushBool(true);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    DuiLib::CPaintManagerUI::SetInstance(module);
  }
  return TRUE;
}
