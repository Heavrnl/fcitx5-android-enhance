# fcitx5-android-enhance
基于 [fcitx5-android](https://github.com/fcitx5-android/fcitx5-android) 二改

### 新增主要功能
- **跨设备剪切板同步**：需配合 [SyncClipboard](https://github.com/Jeric-X/SyncClipboard) 使用
- **验证码自动提取**
- **快捷短语**
- **语音输入**：基于 Qwen3-ASR，长按 `空格` 或从 `工具栏` 触发
- **键盘手写**
- **键盘调节**
- **单手模式**

### 其他改动
- **工具栏编辑**：长按工具栏图标进入编辑界面
- **布局优化**：左下角数字与符号键拆分为独立按键
- **快捷操作**：剪贴板 URL 项增加“打开链接”按钮

### 语音配置说明
- **模型**：Qwen3-ASR-Flash-Realtime（[API Key 获取](https://modelstudio.console.alibabacloud.com/ap-southeast-1/?tab=dashboard#/api-key)）
- **费用**：国际站 ~$0.324/h (免 10h) | 国内站 ~$0.1692/h
