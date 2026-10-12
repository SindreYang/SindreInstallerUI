#ifndef __WEBVIEW2HELPER_H__
#define __WEBVIEW2HELPER_H__

#pragma once

// WebView2 运行时动态加载助手
// 1. 通过 LoadLibrary("WebView2Loader.dll") + GetProcAddress 获取
//    CreateCoreWebView2EnvironmentWithOptions / GetAvailableCoreWebView2BrowserVersionString，
//    避免对 WebView2LoaderStatic.lib 的链接期依赖，从而保证：
//    - 低版本系统（Win7/8）编译出的 DuiLib.dll 无需携带任何 WebView2 相关依赖；
//    - 老编译器（VS2010 v100，_MSC_VER < 1900）即使定义了 DUILIB_USE_WEBVIEW2 也不会被编译进来。
// 2. WebView2Loader.dll 由 WebView2 Runtime（Evergreen）安装器提供，应用只需将 SDK 自带的
//    WebView2Loader.dll 部署到 exe 同目录即可（见 3rd/WebView2/lib/{x86,x64}/）。
// 3. 函数参数使用 IUnknown* 代替强类型接口指针，仅作函数指针传递，避免本头文件依赖 WebView2.h。

#include <Windows.h>
#include <unknwn.h>
#include <wchar.h>	// wcsrchr / swprintf_s
#include "VersionHelpers.h"

namespace DuiLib
{
	namespace WebView2Helper
	{
		// 与 WebView2Loader.dll 导出一致的函数签名（COM 接口参数以 IUnknown* 承载）
		typedef HRESULT(__stdcall *PFN_CreateCoreWebView2EnvironmentWithOptions)(
			LPCWSTR browserExecutableFolder,
			LPCWSTR userDataFolder,
			IUnknown* environmentOptions,
			IUnknown* environmentCreatedHandler);

		typedef HRESULT(__stdcall *PFN_GetAvailableCoreWebView2BrowserVersionString)(
			LPCWSTR browserExecutableFolder,
			LPWSTR* versionInfo);

		// 加载 WebView2Loader.dll（进程内只加载一次）
		// LoadLibrary 默认仅在 exe 同目录/系统目录搜索，若未部署到 exe 目录会返回 NULL。
		// 为兼容开发期与不同部署布局，依次尝试：
		//   1) 标准搜索（exe 同目录，正式部署场景）
		//   2) exe 目录下的 3rd\WebView2\lib\{x86|x64}（开发期从仓库 bin 运行）
		//   3) exe 目录上一级仓库根下的 3rd\WebView2\lib\{x86|x64}
		//   4) DuiLib.dll 模块目录（DLL 随程序集部署场景）
		// 其中按进程位数（_WIN64）匹配 x64/x86 子目录——两种位数的 Loader 不能混用，
		// 位数不匹配时 LoadLibraryW 会失败（ERROR_BAD_EXE_FORMAT）。
		inline HMODULE LoadWebView2Loader()
		{
			static HMODULE s_hModule = NULL;
			if (s_hModule != NULL) return s_hModule;

			// 1) 标准搜索（优先 exe 同目录）
			s_hModule = ::LoadLibraryW(L"WebView2Loader.dll");
			if (s_hModule != NULL) return s_hModule;

			// 组装完整路径逐级兜底
			WCHAR szBase[MAX_PATH] = { 0 };
			WCHAR szPath[MAX_PATH] = { 0 };
			const WCHAR* szArch =
#ifdef _WIN64
				L"x64";
#else
				L"x86";
#endif

			if (::GetModuleFileNameW(NULL, szBase, MAX_PATH) > 0) {
				WCHAR* pSlash = wcsrchr(szBase, L'\\');
				if (pSlash != NULL) *pSlash = 0;

				// 2) exe 目录\3rd\WebView2\lib\<arch>\
				swprintf_s(szPath, MAX_PATH, L"%s\\3rd\\WebView2\\lib\\%s\\WebView2Loader.dll", szBase, szArch);
				s_hModule = ::LoadLibraryW(szPath);
				if (s_hModule != NULL) return s_hModule;

				// 3) exe 目录上一级\3rd\WebView2\lib\<arch>\（仓库根目录布局）
				swprintf_s(szPath, MAX_PATH, L"%s\\..\\3rd\\WebView2\\lib\\%s\\WebView2Loader.dll", szBase, szArch);
				s_hModule = ::LoadLibraryW(szPath);
				if (s_hModule != NULL) return s_hModule;

				// 2b) exe 目录\WebView2Loader.dll（显式完整路径，等价于标准搜索）
				swprintf_s(szPath, MAX_PATH, L"%s\\WebView2Loader.dll", szBase);
				s_hModule = ::LoadLibraryW(szPath);
				if (s_hModule != NULL) return s_hModule;
			}

			// 4) DuiLib.dll 模块目录
			HMODULE hSelf = ::GetModuleHandleW(L"DuiLib.dll");
			if (hSelf != NULL && ::GetModuleFileNameW(hSelf, szBase, MAX_PATH) > 0) {
				WCHAR* pSlash = wcsrchr(szBase, L'\\');
				if (pSlash != NULL) *pSlash = 0;
				swprintf_s(szPath, MAX_PATH, L"%s\\WebView2Loader.dll", szBase);
				s_hModule = ::LoadLibraryW(szPath);
				if (s_hModule != NULL) return s_hModule;
			}
			return s_hModule;	// 仍为 NULL：WebView2Loader.dll 未部署（IsWebView2Supported 返回 false，自动回退 IE）
		}

		// 获取 CreateCoreWebView2EnvironmentWithOptions 函数指针
		inline PFN_CreateCoreWebView2EnvironmentWithOptions GetCreateEnvironmentFunc()
		{
			HMODULE hModule = LoadWebView2Loader();
			if (hModule == NULL) return NULL;
			return (PFN_CreateCoreWebView2EnvironmentWithOptions)::GetProcAddress(hModule, "CreateCoreWebView2EnvironmentWithOptions");
		}

		// 获取 GetAvailableCoreWebView2BrowserVersionString 函数指针
		inline PFN_GetAvailableCoreWebView2BrowserVersionString GetVersionFunc()
		{
			HMODULE hModule = LoadWebView2Loader();
			if (hModule == NULL) return NULL;
			return (PFN_GetAvailableCoreWebView2BrowserVersionString)::GetProcAddress(hModule, "GetAvailableCoreWebView2BrowserVersionString");
		}

		// 检测 WebView2 Runtime（Evergreen）是否已安装
		inline bool IsWebView2RuntimeAvailable()
		{
			PFN_GetAvailableCoreWebView2BrowserVersionString pfnGetVersion = GetVersionFunc();
			if (pfnGetVersion == NULL) return false;
			LPWSTR lpszVersion = NULL;
			HRESULT hr = pfnGetVersion(NULL, &lpszVersion);
			if (SUCCEEDED(hr) && lpszVersion != NULL) {
				::CoTaskMemFree(lpszVersion);
				return true;
			}
			return false;
		}

		// 完整可用性判断：Windows 10 及以上系统 且 已安装 WebView2 Runtime
		inline bool IsWebView2Supported()
		{
			return IsWindows10OrGreater() && IsWebView2RuntimeAvailable();
		}
	}
} // namespace DuiLib

#endif // __WEBVIEW2HELPER_H__
