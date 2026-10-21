<div align="center">

# herdcat

**Wayland 桌面上的一只猫，替你的每个编程 agent 会话举一块牌子。**

[![Release](https://img.shields.io/github/v/release/ZChenW/herdcat?style=flat-square&color=24292f)](https://github.com/ZChenW/herdcat/releases)
[![License](https://img.shields.io/badge/license-MIT-24292f?style=flat-square)](LICENSE)

[English](README.md) · 简体中文

<img src="docs/demo-zh.gif" alt="猫跟着你打字，agent 等待批准时举起牌子" width="680">

</div>

herdcat 趴在屏幕底部，跟着你一起打字。Claude Code、Codex、Grok、Kimi、Cursor、Copilot、Pi、opencode、Qwen Code 和 Antigravity 的每个会话都有一块牌子，写着它在做什么、正在忙、在等你，还是已经做完。点一下牌子，就跳到那个终端。

## 安装

```sh
# Arch Linux
git clone https://github.com/ZChenW/herdcat && cd herdcat/packaging/arch && makepkg -si

# 从源码构建：需要 wayland、freetype2 和 fontconfig
git clone https://github.com/ZChenW/herdcat && cd herdcat && make && sudo make install
```

Nix 用户：`nix run github:ZChenW/herdcat`。其他方式见[安装](docs/install.md)。

## 开始使用

```sh
herdcat setup     # 接入已经装好的 agent
herdcat -w        # 启动；改配置后保存即生效
```

开机自启：`systemctl --user enable --now herdcat`。

## 文档

文档为英文。

- [安装](docs/install.md)：安装包、键盘辅助程序、开机启动
- [配置](docs/configuration.md)：全部选项，附截图
- [牌子](docs/signs.md)和 [agent](docs/agents.md)：牌子上显示什么，各个 agent 怎么接入
- [安全](docs/security.md)：键盘辅助程序能看到什么、看不到什么
- [常见问题](docs/troubleshooting.md)

## 致谢

herdcat 源自 Saatvik Sharma 的 [wayland-bongocat](https://github.com/saatvik333/wayland-bongocat)。Pi 和 opencode 的桥接改编自 [OpenPets](https://github.com/OpenPetsHQ/openpets)。以 [MIT 许可证](LICENSE)发布。
