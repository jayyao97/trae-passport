<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# Trae Passport

Trae Passport 是基于
[FoloToy AI Passport](https://github.com/folotoy/ai-passport) 独立维护的 Fork，
用于打造具有表现力的 ESP32-C3 可穿戴工卡体验。

## 当前体验

- 开机直接进入全屏 Bloub 头像。
- 还原 14 种动画状态，支持平滑过渡和自动播放。
- 在无 PSRAM 的硬件上使用轻量抗锯齿 RGB565 渲染器和 DMA 双缓冲，实机约
  40 FPS。
- 使用 24 px 黑色圆角屏幕遮罩贴合黑色外壳，并通过暖纸色背景区分白色眼睛。
- 使用上、下按键手动切换状态；OK 键暂停或恢复自动播放。

## 与上游项目的关系

硬件支持、ESP-IDF 基础和板级文档来自 FoloToy AI Passport。本仓库独立维护
产品体验、Issue、Release 和后续开发方向，并按需定期合并上游改进。

本仓库保留原始 MIT 许可证和 FoloToy 版权声明。详见
[LICENSE](LICENSE) 和[上游项目](https://github.com/folotoy/ai-passport)。

## 开发

硬件能力请参阅[项目文档](docs/README.zh_CN.md)，ESP-IDF 5.5.3 开发流程请参阅
[构建与测试指南](docs/development/engineering/build-and-test.zh_CN.md)。
