#include <windows.h>

#include <string>

#include "installer_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR command_line,
                    int show_command) {
  // 预览程序与 NSIS 安装包使用同一套物理像素，避免高 DPI 下出现黑色空白区域。
  ::SetProcessDPIAware();
  DuiLib::CPaintManagerUI::SetInstance(instance);
  wchar_t path_buffer[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path_buffer, MAX_PATH);
  std::wstring resource_directory(path_buffer);
  const std::wstring::size_type separator = resource_directory.find_last_of(L"\\/");
  resource_directory = separator == std::wstring::npos
                           ? L"skin"
                           : resource_directory.substr(0, separator + 1) + L"skin";

  sindre::installer::InstallerWindow window(nullptr);
  if (!window.Create(resource_directory)) {
    MessageBoxW(nullptr, L"Unable to load installer UI resources.", L"SindreInstallerUI",
                MB_ICONERROR | MB_OK);
    return 1;
  }
  int preview_page = 0;
  const std::wstring command_line_text = command_line == nullptr ? L"" : command_line;
  const std::wstring page_prefix = L"--page=";
  const std::wstring::size_type page_offset = command_line_text.find(page_prefix);
  if (page_offset != std::wstring::npos) {
    preview_page = _wtoi(command_line_text.c_str() + page_offset + page_prefix.size());
  }
  window.ShowPage(preview_page);
  ShowWindow(window.GetHWND(), show_command);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return 0;
}
