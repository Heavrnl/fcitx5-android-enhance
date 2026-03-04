# fcitx5-android-enhance

基于 [fcitx5-android](https://github.com/fcitx5-android/fcitx5-android) 的二次修改版本。

## ✨ 新增功能
* **核心增强**：跨设备剪切板同步、验证码自动提取、快捷短语。
* **输入优化**：键盘手写、键盘调节、单手模式。
* **语音输入**：支持 qwen3-asr-flash-realtime 实时语音转文字。

## 🛠️ 其他改动
* **工具栏**：新增编辑界面（长按工具栏图标进入）；剪贴板 URL 增加“打开链接”按钮。
* **布局优化**：左下角“数字+符号”混合键拆分为两个独立按键。

## 📝 说明与配置

### 1. 剪切板同步
需配合 [SyncClipboard](https://github.com/Jeric-X/SyncClipboard) 使用，请自行部署服务端。

### 2. 语音输入 (qwen3-asr-flash-realtime)
* **触发**：长按 `空格键` 直接开始，或通过 `工具栏` 打开专用界面。
* **配置**：需在 [阿里云 Model Studio](https://modelstudio.console.alibabacloud.com/ap-southeast-1/?tab=dashboard#/api-key) 获取 API Key。
* **费用参考**：
    | 平台 | 价格 | 备注 |
    | :--- | :--- | :--- |
    | **国际站** | ~$0.324 / 小时 | 赠送 10 小时免费额度 |
    | **国内站** | ~$0.1692 / 小时 | - |
