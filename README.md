# 可选插件

海图、船队、报警、视频是软件**主体**，源码在 `src/modules/`，编进 `shore_station.exe`。

`plugins/` 只放**可选扩展**。卸掉本目录全部 DLL 后，主体四条主流程仍必须能用。

## 构建

在仓库根目录使用聚合工程，一次编出主体和 Hello 插件：

```powershell
$env:PATH="D:\cache-QT6\Tools\mingw1310_64\bin;D:\cache-QT6\6.11.1\mingw_64\bin;$env:PATH"

D:\cache-QT6\6.11.1\mingw_64\bin\qmake.exe shore.pro
D:\cache-QT6\Tools\mingw1310_64\bin\mingw32-make.exe -j4
```

产物：

```text
bin/shore_station.exe
bin/plugins/hello_page.dll
bin/plugins/hello_service.dll
```

只编主体：`qmake shore_station.pro`。只编 Hello：进入 `plugins/hello_page` 再 `qmake hello_page.pro`。

## 运行时如何加载

启动时宿主扫描 `应用程序目录/plugins/*.dll`：

1. `QPluginLoader` 加载
2. `qobject_cast<IShorePlugin*>`
3. 检查 `sdk_version == 1.0`
4. `initialize(context)` → 应用插件在此调用 `context.ui().addPage(*this)`；服务插件不注册页面
5. 宿主把应用插件页面放进 `QStackedWidget`，**不**在左侧导航追加按钮
6. 全部 initialize 完成后 `start()`

某个 DLL 失败只打日志，不影响主体。

顶栏插件按钮或系统设置可打开插件管理中心。应用插件卡片提供「入口」，服务插件卡片提供「配置」（界面由插件自己实现）。使能开关只改草稿，「重置」撤销本次操作，「重启软件」写入 `config/application.json` 并重启后按使能加载。未出现在配置里的插件默认启用。海图 / 船队 / 报警 / 视频不在该页管理。

## 你要完成 Hello 的验收步骤

1. 按上面命令编译 `shore.pro`。
2. 确认 `bin/plugins/hello_page.dll` 与 `bin/plugins/hello_service.dll` 存在。
3. 运行 `bin/shore_station.exe`。
4. 顶栏打开插件管理中心：应看到「Hello」应用插件与「示例服务」服务插件。
5. 在 Hello 卡片点「入口」进入说明页；点「海图」或插件中心「返回」回到主体。
6. 把两个 DLL 挪走再启动：主体四页仍在，插件中心为空。

第 6 步用来确认：**插件不是编进 exe 的**。

## 开发一个新插件

应用插件复制 `plugins/hello_page`，服务插件复制 `plugins/hello_service`。

1. 改 `.pro` 里的 `TARGET`。
2. 改 `descriptor()` 的 `id`（必须唯一）、`name`、`version`、`kind`。
3. 改 JSON 里同样的 id / 版本 / `kind`（`application` 或 `service`），并与 `Q_PLUGIN_METADATA FILE` 指向的 JSON 一致。可选 `icon`：填插件自己的 qrc 路径（如 `:/你的插件/icon.svg`）或相对 DLL 的文件路径，不要用岸基 `:/icons/`。未填则插件中心使用宿主 `default_plugin.svg`。
4. 应用插件实现 `IPageProvider`，在 `initialize()` 成功返回前调用 `context.ui().addPage(*this)`。
5. 服务插件实现 `IConfigProvider::configure()`，自己弹出配置界面；返回 true 表示参数已改，宿主会询问是否重启。
6. 把新 `.pro` 加进根目录 `shore.pro` 的 `SUBDIRS`。
7. 只链接 `plugin_sdk.pri`，不要 `include` 主体模块。

## 禁止事项

- 插件 `#include` `main_window.h` 或任何 `src/modules` 头文件
- 跨插件边界抛 C++ 异常（用 `PluginResult`）
- 在 `initialize()` 里启动网络或重线程（放到 `start()`）
- 指望运行中卸载 DLL
- 把海图 / 船队 / 报警 / 视频改成插件
