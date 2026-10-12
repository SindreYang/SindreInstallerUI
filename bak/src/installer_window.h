#pragma once

#include <windows.h>

#include <map>
#include <functional>
#include <string>
#include <vector>

#include "UIlib.h"

namespace sindre {
namespace installer {

/**
 * DuiLib 安装器窗口。
 *
 * 该窗口既可以嵌入 NSIS 自定义页面，也可以被独立预览程序创建。
 */
class InstallerWindow final : public DuiLib::CWindowWnd,
                              public DuiLib::INotifyUI {
 public:
  explicit InstallerWindow(HWND parent);
  ~InstallerWindow();

  InstallerWindow(const InstallerWindow&) = delete;
  InstallerWindow& operator=(const InstallerWindow&) = delete;

  /** 创建并显示窗口。 */
  bool Create(const std::wstring& resource_directory);

  /** 切换到指定安装步骤。 */
  bool ShowPage(int page_index);

  /** 设置指定控件的文字。 */
  bool SetText(const std::wstring& control_name, const std::wstring& text);

  /** 读取指定控件的文字。 */
  bool GetText(const std::wstring& control_name, std::wstring* text) const;

  /** 读取选项控件的选中状态。 */
  bool GetSelected(const std::wstring& control_name, bool* selected) const;

  /** 更新安装进度。 */
  bool SetProgress(int value);

  /** 更新顶部向导步骤进度。 */
  bool SetStepProgress(int value);

  /** 绑定 NSIS 回调代码段。 */
  void BindCallback(const std::wstring& control_name, int code_segment);

  /** 设置控件点击后的回调执行器。 */
  void SetCallbackInvoker(std::function<void(int)> invoker);

  /** 请求关闭窗口，供 NSIS 回调安全退出消息循环。 */
  void CloseWindow();

  /** 注册窗口类名称。 */
  LPCTSTR GetWindowClassName() const override;

  /** 接收 DuiLib 控件通知。 */
  void Notify(DuiLib::TNotifyUI& message) override;

 protected:
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) override;
  void OnFinalMessage(HWND hwnd) override;

 private:
  /**
   * 注册随安装器分发的私有字体。
   *
   * \return 至少有一个字体注册成功时返回 true。
   */
  bool RegisterPrivateFonts();

  /**
   * 注销本窗口注册的私有字体。
   */
  void UnregisterPrivateFonts();

  void NotifyCallback(const std::wstring& control_name);
  DuiLib::CTabLayoutUI* PageLayout() const;

  HWND parent_ = nullptr;
  std::wstring resource_directory_;
  DuiLib::CPaintManagerUI paint_manager_;
  std::map<std::wstring, int> callbacks_;
  std::function<void(int)> callback_invoker_;
  std::vector<std::wstring> registered_font_paths_;
  bool nsis_page_active_ = false;
};

}  // namespace installer
}  // namespace sindre
