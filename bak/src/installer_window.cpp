#include "installer_window.h"

#include <shlwapi.h>

#include <string>

namespace sindre {
namespace installer {

namespace {

constexpr LPCTSTR kWindowClassName = _T("SindreInstallerUI.Window");
constexpr UINT kNotifyCustomReady = WM_USER + 0xD;

int ClampProgress(int value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return value;
}

BOOL CALLBACK HideNsisChromeChild(HWND child, LPARAM) {
  TCHAR class_name[64] = {};
  GetClassName(child, class_name, static_cast<int>(_countof(class_name)));
  // NSIS 的原生页面包含无 ID 的 Static 分隔线和底部 Button；递归隐藏
  // 所有这类控件，避免不同 NSIS 版本把残留横线放入嵌套容器后再次露出。
  if (_tcscmp(class_name, _T("Static")) == 0 ||
      _tcscmp(class_name, _T("Button")) == 0) {
    ShowWindow(child, SW_HIDE);
  }
  return TRUE;
}

void HideNsisChrome(HWND parent) {
  if (parent == nullptr) {
    return;
  }
  const int nsis_control_ids[] = {1, 2, 3, 1018, 1028};
  for (const int control_id : nsis_control_ids) {
    if (const HWND control = GetDlgItem(parent, control_id)) {
      ShowWindow(control, SW_HIDE);
    }
  }

  EnumChildWindows(parent, HideNsisChromeChild, 0);
}

}  // namespace

InstallerWindow::InstallerWindow(HWND parent) : parent_(parent) {}

InstallerWindow::~InstallerWindow() { UnregisterPrivateFonts(); }

LPCTSTR InstallerWindow::GetWindowClassName() const { return kWindowClassName; }

bool InstallerWindow::Create(const std::wstring& resource_directory) {
  resource_directory_ = resource_directory;
  if (!resource_directory_.empty() && resource_directory_.back() != L'\\') {
    resource_directory_.push_back(L'\\');
  }

  // NSIS 自定义页要求插件窗口保持为传入父窗口的子窗口，并向父窗口发送
  // WM_NOTIFY_CUSTOM_READY；即使父窗口在回调期间尚未显示，也不能改成顶层窗口。

  if (!PathFileExistsW((resource_directory_ + L"install.xml").c_str())) {
    return false;
  }

  // 字体随资源目录分发，使用 FR_PRIVATE 避免修改用户的系统字体列表。
  RegisterPrivateFonts();

  DuiLib::CPaintManagerUI::SetCurrentPath(resource_directory_.c_str());
  DuiLib::CPaintManagerUI::SetResourcePath(resource_directory_.c_str());

  if (parent_ != nullptr) {
    // NSIS 默认对话框小于 HTML 原型；先扩大根窗口，随后由 WM_SIZE/定时器
    // 将自定义页同步到 980x620 的设计尺寸。
    const HWND host_window = GetAncestor(parent_, GA_ROOT);
    if (host_window != nullptr) {
      // 去掉 NSIS 原生标题框，让 XML 中的标题栏成为唯一窗口 chrome，
      // 与 index.html 的单层安装器外观保持一致。
      LONG_PTR style = GetWindowLongPtr(host_window, GWL_STYLE);
      style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                 WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
      style |= WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
      SetWindowLongPtr(host_window, GWL_STYLE, style);
      SetWindowPos(host_window, nullptr, 0, 0, 980, 620,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                       SWP_FRAMECHANGED);
      // 原生 NSIS 对话框本身没有圆角；裁剪宿主窗口后，外轮廓与 HTML
      // 原型的 10px 圆角和内容裁切保持一致。
      if (HRGN region = CreateRoundRectRgn(0, 0, 980, 620, 20, 20)) {
        SetWindowRgn(host_window, region, TRUE);
      }
    }
  }

  RECT parent_rect{};
  if (parent_ != nullptr) {
    GetClientRect(parent_, &parent_rect);
    // NSIS 自定义页在回调初始阶段可能尚未完成布局，先使用设计尺寸，
    // 后续由定时器和 WM_SIZE 根据父窗口实际区域调整。
    if (parent_rect.right <= parent_rect.left ||
        parent_rect.bottom <= parent_rect.top) {
      parent_rect = {0, 0, 980, 620};
    }
  } else {
    parent_rect = {0, 0, 980, 620};
  }

  // 独立预览使用无标题栏弹窗，标题栏区域由 DuiLib 自定义皮肤绘制。
  const DWORD style = parent_ == nullptr
                          ? (WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
                          : UI_WNDSTYLE_CHILD;
  const DWORD ex_style = parent_ == nullptr ? WS_EX_APPWINDOW : 0;
  HWND hwnd = CWindowWnd::Create(parent_, _T("SindreInstallerUI"), style,
                                 ex_style, parent_rect);
  if (hwnd == nullptr) {
    return false;
  }

  if (parent_ != nullptr) {
    // 自定义页仍会保留 NSIS 默认的 Install/Cancel/Branding 控件；隐藏它们，
    // 避免与 index.html 的单一底部操作栏叠加。
    HideNsisChrome(parent_);
  }

  ShowWindow(true, parent_ == nullptr);
  if (parent_ != nullptr) {
    // NSIS 可能在插件 Init 返回后才给自定义页分配最终尺寸。
    SetTimer(m_hWnd, 1, 50, nullptr);
  }
  return true;
}

bool InstallerWindow::RegisterPrivateFonts() {
  const wchar_t* font_names[] = {
      L"fonts\\AlibabaPuHuiTi-2-55-Regular.ttf",
      L"fonts\\AlibabaPuHuiTi-2-75-SemiBold.ttf",
  };
  bool registered = false;
  for (const wchar_t* font_name : font_names) {
    const std::wstring path = resource_directory_ + font_name;
    if (!PathFileExistsW(path.c_str())) {
      continue;
    }
    if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) != 0) {
      registered_font_paths_.push_back(path);
      registered = true;
    }
  }
  return registered;
}

void InstallerWindow::UnregisterPrivateFonts() {
  for (const std::wstring& path : registered_font_paths_) {
    RemoveFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
  }
  registered_font_paths_.clear();
}

LRESULT InstallerWindow::HandleMessage(UINT message, WPARAM wparam,
                                       LPARAM lparam) {
  if (message == WM_LBUTTONDOWN) {
    RECT client_rect{};
    GetClientRect(m_hWnd, &client_rect);
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    // 关闭按钮在按下时立即处理，避免 NSIS 宿主吞掉 WM_LBUTTONUP。
    if (y >= 0 && y < 34 && x >= client_rect.right - 38 &&
        x < client_rect.right - 10) {
      const HWND target = parent_ == nullptr ? m_hWnd
                                             : GetAncestor(parent_, GA_ROOT);
      if (parent_ == nullptr) {
        CloseWindow();
      } else if (target != nullptr) {
        ::DestroyWindow(target);
      }
      return 0;
    }
  }

  if (message == WM_LBUTTONUP) {
    RECT client_rect{};
    GetClientRect(m_hWnd, &client_rect);
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    // 标题栏按钮同时覆盖 NSIS 宿主和独立预览，避免不同消息路由让按钮失效。
    if (y >= 0 && y < 34) {
      const int right = client_rect.right;
      const HWND target = parent_ == nullptr ? m_hWnd
                                             : GetAncestor(parent_, GA_ROOT);
      if (x >= right - 38 && x < right - 10) {
        if (parent_ == nullptr) {
          CloseWindow();
        } else if (target != nullptr) {
          ::DestroyWindow(target);
        }
        return 0;
      }
      if (x >= right - 71 && x < right - 43) {
        if (target != nullptr) {
          ::ShowWindow(target, ::IsZoomed(target) ? SW_RESTORE : SW_MAXIMIZE);
        }
        NotifyCallback(L"btnMaximize");
        return 0;
      }
      if (x >= right - 104 && x < right - 76) {
        if (target != nullptr) {
          ::ShowWindow(target, SW_MINIMIZE);
        }
        NotifyCallback(L"btnMinimize");
        return 0;
      }
    }
  }

  if (message == WM_LBUTTONDOWN && parent_ != nullptr) {
    RECT client_rect{};
    GetClientRect(m_hWnd, &client_rect);
    const int x = GET_X_LPARAM(lparam);
    const int y = GET_Y_LPARAM(lparam);
    // 避开右侧三个按钮，把标题栏空白区域交给 NSIS 根窗口拖动。
    // 三个按钮和间距总宽约 94 像素，额外留出 16 像素避免最小化按钮
    // 被误判为标题栏拖动区域。
    if (y >= 0 && y < 34 && x >= 0 && x < client_rect.right - 110) {
      ReleaseCapture();
      const HWND host_window = GetAncestor(parent_, GA_ROOT);
      ::SendMessage(host_window, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      return 0;
    }
  }

  if (message == WM_NCHITTEST && parent_ == nullptr) {
    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    ScreenToClient(m_hWnd, &point);
    RECT client_rect{};
    GetClientRect(m_hWnd, &client_rect);
    if (point.y >= 0 && point.y < 34 && point.x < client_rect.right - 110) {
      return HTCAPTION;
    }
    return HTCLIENT;
  }

  // 某些系统 DPI 缩放组合下，DuiLib 的默认消息分发会收到已经缩放过的
  // 鼠标坐标，导致底部按钮的绘制位置和命中矩形不一致。先按当前布局的
  // 实际控件矩形做一次兜底命中，只处理按钮控件，复选框和文本编辑仍交给
  // DuiLib 原有的鼠标状态处理。
  if (message == WM_LBUTTONUP) {
    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    if (auto* control = paint_manager_.FindControl(point)) {
      const std::wstring name = control->GetName().GetData();
      if (name.rfind(L"btn", 0) == 0 &&
          control->GetInterface(DUI_CTR_BUTTON) != nullptr) {
        DuiLib::TNotifyUI click{};
        click.sType = _T("click");
        click.pSender = control;
        click.ptMouse = point;
        Notify(click);
        return 0;
      }
    }
  }

  if (message == WM_CREATE) {
    paint_manager_.Init(m_hWnd);
    DuiLib::CDialogBuilder builder;
    DuiLib::CControlUI* root =
        builder.Create(_T("install.xml"), 0, nullptr, &paint_manager_);
    if (root == nullptr) {
      return -1;
    }
    paint_manager_.AttachDialog(root);
    paint_manager_.AddNotifier(this);
    return 0;
  }

  if (message == WM_SIZE && parent_ != nullptr) {
    RECT rect{};
    GetClientRect(parent_, &rect);
    if (rect.right > rect.left && rect.bottom > rect.top) {
      SetWindowPos(m_hWnd, nullptr, 0, 0, rect.right - rect.left,
                   rect.bottom - rect.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  if (message == WM_TIMER && wparam == 1 && parent_ != nullptr) {
    // NSIS 有时在插件 Init 返回后才创建默认按钮，因此延迟布局时再隐藏一次。
    HideNsisChrome(parent_);
    RECT rect{};
    GetClientRect(parent_, &rect);
    if (rect.right > rect.left && rect.bottom > rect.top) {
      SetWindowPos(m_hWnd, nullptr, 0, 0, rect.right - rect.left,
                   rect.bottom - rect.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      KillTimer(m_hWnd, 1);
    }
  }

  LRESULT result = 0;
  if (paint_manager_.MessageHandler(message, wparam, lparam, result)) {
    return result;
  }
  return CWindowWnd::HandleMessage(message, wparam, lparam);
}

void InstallerWindow::OnFinalMessage(HWND hwnd) {
  KillTimer(hwnd, 1);
  nsis_page_active_ = false;
}

DuiLib::CTabLayoutUI* InstallerWindow::PageLayout() const {
  return static_cast<DuiLib::CTabLayoutUI*>(
      paint_manager_.FindControl(_T("wizardPages")));
}

bool InstallerWindow::ShowPage(int page_index) {
  auto* pages = PageLayout();
  if (pages == nullptr || pages->GetCount() <= 0) {
    return false;
  }
  const int page_count = pages->GetCount();
  const int selected_page =
      page_index < 0 ? 0 : (page_index >= page_count ? page_count - 1
                                                       : page_index);
  pages->SelectItem(selected_page);

  // 同步左侧步骤轨道，让当前页面、数字指示和步骤高亮始终一致。
  const int error_page = page_count - 1;
  const int finish_page = page_count >= 2 ? page_count - 2 : 0;
  const int install_page = page_count >= 3 ? page_count - 3 : 0;
  const int active_step =
      selected_page == error_page
          ? 3
          : (selected_page == finish_page
                 ? 4
                 : (selected_page >= install_page ? 3 : selected_page));
  for (int step_index = 0; step_index < 5; ++step_index) {
    const std::wstring step_name =
        std::wstring(L"step") + std::to_wstring(step_index);
    auto* step = static_cast<DuiLib::CContainerUI*>(
        paint_manager_.FindControl(step_name.c_str()));
    if (step == nullptr || step->GetCount() < 3) {
      continue;
    }
    const bool is_active = step_index == active_step;
    if (auto* marker = step->GetItemAt(0)) {
      marker->SetBkColor(is_active ? 0xFF6C63FF : 0x335C6A9E);
    }
    auto* label = static_cast<DuiLib::CLabelUI*>(step->GetItemAt(2));
    if (label != nullptr) {
      label->SetTextColor(is_active ? 0xFFDDE8F7 : 0xFF8FA9C9);
    }
  }

  // 页面编号随当前步骤更新，避免预览和 NSIS 回调显示过期状态。
  if (auto* indicator = paint_manager_.FindControl(_T("pageIndicator"))) {
    const std::wstring text = selected_page == error_page
                                  ? L"ERR"
                                  : std::wstring(L"步骤 ") +
                                        std::to_wstring(selected_page + 1) + L" / " +
                                        std::to_wstring(page_count - 1);
    indicator->SetText(text.c_str());
  }

  // 页面切换也要同步顶部进度条。安装脚本在需要覆盖步骤值时使用
  // SetStepProgress；这里只负责让欢迎、许可、组件和配置页不再
  // 显示上一次页面残留的进度。
  if (auto* top_progress = static_cast<DuiLib::CProgressUI*>(
          paint_manager_.FindControl(_T("topProgress")))) {
    const int step_count = page_count > 1 ? page_count - 1 : 1;
    const int step_value = selected_page == error_page
                               ? 100
                               : ((selected_page + 1) * 100 + step_count / 2) /
                                     step_count;
    top_progress->SetValue(static_cast<short>(ClampProgress(step_value)));
  }

  const std::wstring page_name =
      std::wstring(L"step") + std::to_wstring(selected_page);
  if (auto* active_step = paint_manager_.FindControl(page_name.c_str())) {
    active_step->SetVisible(true);
  }
  paint_manager_.NeedUpdate();

  // NSIS 的自定义页由插件负责进入消息循环，直到页面窗口被销毁。
  // 创建阶段只建立子窗口；首次由脚本调用 ShowPage 时才通知 NSIS 页面就绪。
  if (parent_ != nullptr && !nsis_page_active_) {
    nsis_page_active_ = true;
    ::SendMessage(parent_, kNotifyCustomReady,
                  reinterpret_cast<WPARAM>(m_hWnd), 0);
    MSG message{};
    while (m_hWnd != nullptr) {
      const BOOL status = ::GetMessage(&message, nullptr, 0, 0);
      if (status <= 0) {
        break;
      }
      if (!::IsDialogMessage(m_hWnd, &message) &&
          !::IsDialogMessage(parent_, &message)) {
        ::TranslateMessage(&message);
        ::DispatchMessage(&message);
      }
    }
    nsis_page_active_ = false;
  }
  return true;
}

bool InstallerWindow::SetText(const std::wstring& control_name,
                              const std::wstring& text) {
  auto* control = paint_manager_.FindControl(control_name.c_str());
  if (control == nullptr) {
    return false;
  }
  control->SetText(text.c_str());
  paint_manager_.NeedUpdate();
  return true;
}

bool InstallerWindow::GetText(const std::wstring& control_name,
                              std::wstring* text) const {
  if (text == nullptr) {
    return false;
  }
  auto* control = paint_manager_.FindControl(control_name.c_str());
  if (control == nullptr) {
    return false;
  }
  *text = control->GetText().GetData();
  return true;
}

bool InstallerWindow::GetSelected(const std::wstring& control_name,
                                  bool* selected) const {
  if (selected == nullptr) {
    return false;
  }
  auto* control = paint_manager_.FindControl(control_name.c_str());
  if (control == nullptr) {
    return false;
  }
  auto* option = static_cast<DuiLib::COptionUI*>(
      control->GetInterface(DUI_CTR_OPTION));
  if (option == nullptr) {
    return false;
  }
  *selected = option->IsSelected();
  return true;
}

bool InstallerWindow::SetProgress(int value) {
  auto* progress = static_cast<DuiLib::CProgressUI*>(
      paint_manager_.FindControl(_T("installProgress")));
  const int clamped_value = ClampProgress(value);
  bool updated = false;
  if (progress != nullptr) {
    progress->SetValue(static_cast<short>(clamped_value));
    updated = true;
  }
  if (auto* percent = paint_manager_.FindControl(_T("installPercent"))) {
    const std::wstring text = std::to_wstring(clamped_value) + L"%";
    percent->SetText(text.c_str());
    updated = true;
  }
  if (!updated) {
    return false;
  }
  paint_manager_.NeedUpdate();
  return true;
}

bool InstallerWindow::SetStepProgress(int value) {
  auto* top_progress = static_cast<DuiLib::CProgressUI*>(
      paint_manager_.FindControl(_T("topProgress")));
  if (top_progress == nullptr) {
    return false;
  }
  top_progress->SetValue(static_cast<short>(ClampProgress(value)));
  paint_manager_.NeedUpdate();
  return true;
}

void InstallerWindow::BindCallback(const std::wstring& control_name,
                                   int code_segment) {
  callbacks_[control_name] = code_segment;
}

void InstallerWindow::SetCallbackInvoker(std::function<void(int)> invoker) {
  callback_invoker_ = std::move(invoker);
}

void InstallerWindow::CloseWindow() {
  Close();
  // 独立预览没有 NSIS 消息循环负责退出，销毁窗口后主动结束消息循环。
  if (parent_ == nullptr) {
    ::PostQuitMessage(0);
  }
}

void InstallerWindow::Notify(DuiLib::TNotifyUI& message) {
  if (message.sType != _T("click") || message.pSender == nullptr) {
    return;
  }
  const std::wstring name = message.pSender->GetName().GetData();
  auto* pages = PageLayout();
  const int page_count = pages == nullptr ? 0 : pages->GetCount();
  if (name == L"selectAll") {
    auto* master = static_cast<DuiLib::COptionUI*>(
        message.pSender->GetInterface(DUI_CTR_OPTION));
    if (master != nullptr) {
      const bool selected = master->IsSelected();
      const wchar_t* component_names[] = {L"cliComponent", L"sdkComponent",
                                          L"docsComponent"};
      for (const wchar_t* component_name : component_names) {
        auto* component = paint_manager_.FindControl(component_name);
        if (component == nullptr) {
          continue;
        }
        auto* option = static_cast<DuiLib::COptionUI*>(
            component->GetInterface(DUI_CTR_OPTION));
        if (option != nullptr) {
          option->Selected(selected, false);
        }
      }
      paint_manager_.NeedUpdate();
    }
    // 组件全选由插件内部处理；如果调用方也绑定了回调，仍然通知调用方。
    NotifyCallback(name);
    return;
  }

  // 返回按钮属于界面导航，插件先完成切页，再通知 NSIS 脚本同步状态。
  // 这样即使宿主没有正确转发回调，独立预览和安装包仍能返回上一页。
  if (name == L"btnLicenseBack") {
    ShowPage(0);
    NotifyCallback(name);
    return;
  }
  if (name == L"btnComponentsBack") {
    // 升级页面把 brandCode 改为 UPDATE，组件页应直接回到升级欢迎页。
    auto* brand_code = paint_manager_.FindControl(_T("brandCode"));
    const bool upgrade = brand_code != nullptr &&
                         std::wstring(brand_code->GetText().GetData()) ==
                             L"UPDATE";
    ShowPage(upgrade ? 0 : 1);
    NotifyCallback(name);
    return;
  }
  if (name == L"btnOptionsBack") {
    ShowPage(2);
    NotifyCallback(name);
    return;
  }

  // 极简主题的欢迎页把安装选项收在“自定义安装”面板中，保持与
  // preview/ui_sample/index.html 相同的默认折叠状态。完整主题没有这些控件，
  // 因此找不到对应控件时不会影响完整主题。
  if (name == L"btnOptions") {
    if (auto* options = paint_manager_.FindControl(_T("welcomeOptions"))) {
      const bool expanded = !options->IsVisible();
      options->SetVisible(expanded);
      if (auto* button = paint_manager_.FindControl(_T("btnOptions"))) {
        button->SetText(expanded ? _T("收起安装选项") : _T("自定义安装 ›"));
      }
      paint_manager_.NeedUpdate();
    }
    NotifyCallback(name);
    return;
  }

  // 极简主题使用页面内许可弹层；完整主题没有该控件时继续交给脚本处理。
  if (name == L"btnLicenseView") {
    if (auto* dialog = paint_manager_.FindControl(_T("licenseDialog"))) {
      dialog->SetVisible(true);
      paint_manager_.NeedUpdate();
      return;
    }
  }
  if (name == L"btnLicenseClose") {
    if (auto* dialog = paint_manager_.FindControl(_T("licenseDialog"))) {
      dialog->SetVisible(false);
      paint_manager_.NeedUpdate();
      return;
    }
  }

  // 窗口控制直接作用于宿主根窗口。NSIS 回调仍会收到通知，但不再依赖
  // $HWNDPARENT 是否指向插件子窗口。
  if (name == L"btnMinimize" || name == L"btnMaximize") {
    const HWND target = parent_ == nullptr ? m_hWnd
                                           : GetAncestor(parent_, GA_ROOT);
    if (target != nullptr) {
      if (name == L"btnMinimize") {
        ::ShowWindow(target, SW_MINIMIZE);
      } else {
        ::ShowWindow(target, ::IsZoomed(target) ? SW_RESTORE : SW_MAXIMIZE);
      }
    }
    NotifyCallback(name);
    return;
  }

  // 关闭按钮直接关闭顶层宿主，避免 NSIS 版本差异导致脚本回调丢失。
  // 安装包由脚本负责业务状态，窗口层只保证用户能退出当前向导。
  if (name == L"btnClose") {
    if (parent_ == nullptr) {
      CloseWindow();
    } else {
      const HWND host_window = GetAncestor(parent_, GA_ROOT);
      if (host_window != nullptr) {
        ::DestroyWindow(host_window);
      }
    }
    return;
  }

  // NSIS 绑定回调后，由脚本完全负责页面切换、校验和错误处理。
  // 这样不会先执行一套内置翻页，再被外部回调覆盖，避免升级流程闪过错误页面。
  if (callbacks_.find(name) != callbacks_.end() && callback_invoker_) {
    NotifyCallback(name);
    return;
  }

  // 没有外部回调时保留独立预览所需的默认翻页行为。
  if (name == L"btnWelcomeNext") {
    ShowPage(1);
  } else if (name == L"btnLicenseNext") {
    ShowPage(2);
  } else if (name == L"btnComponentsNext") {
    ShowPage(3);
  } else if (name == L"btnInstall") {
    ShowPage(page_count >= 3 ? page_count - 3 : 0);
  } else if (name == L"btnProgressError") {
    ShowPage(page_count >= 1 ? page_count - 1 : 0);
  } else if (name == L"btnProgressContinue") {
    ShowPage(page_count >= 2 ? page_count - 2 : 0);
  } else if (name == L"btnRetry") {
    ShowPage(page_count >= 4 ? page_count - 4 : 0);
  } else if (name == L"btnFinish") {
    if (parent_ == nullptr) {
      CloseWindow();
    } else {
      ShowPage(page_count >= 2 ? page_count - 2 : 0);
    }
  }
  NotifyCallback(name);
}

void InstallerWindow::NotifyCallback(const std::wstring& control_name) {
  const auto callback = callbacks_.find(control_name);
  if (callback != callbacks_.end() && callback_invoker_) {
    callback_invoker_(callback->second);
  }
}

}  // namespace installer
}  // namespace sindre


