# 共享运行时

`plugins/x86-unicode/SindreInstallerUI.dll` 是 `skill_simple` 和 `skill_full`
共同使用的 DuiLib_Ultimate NSIS 插件运行时。

运行时不包含具体主题。每个 NSIS 脚本通过 `SindreInstallerUI::Init` 传入自己的
`resources/skin` 目录，因此两套主题可以使用同一份 DLL：

- `skill_simple` 提供三页面皮肤和简单安装流程；
- `skill_full` 提供许可、组件、安装选项、进度、完成和错误页面。

只有修改 `src/`、DuiLib 或 NSIS 插件接口时才需要重新编译 DLL。修改 XML、图片、字体
等主题资源时，只需重新运行对应的 `makensis`，无需重新编译 DLL。

两个主题脚本均从 `../common/plugins/x86-unicode` 加载该 DLL；主题目录中的旧插件副本
不再参与构建。
