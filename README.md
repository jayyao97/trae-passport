<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Trae Passport

Trae Passport is an independently maintained fork of
[FoloToy AI Passport](https://github.com/folotoy/ai-passport) for an expressive
ESP32-C3 wearable badge experience.

## Current experience

- Boots directly into a full-screen Bloub avatar.
- Recreates 14 animated states with smooth transitions and automatic playback.
- Uses a compact antialiased RGB565 renderer with double-buffered DMA at about
  40 FPS on hardware without PSRAM.
- Matches the black enclosure with a 24 px rounded display mask and keeps the
  eyes distinct from the warm paper background.
- Supports manual state navigation with the UP and DOWN buttons; OK pauses or
  resumes automatic playback.

## Relationship to upstream

The hardware support, ESP-IDF foundation, and board documentation come from
FoloToy AI Passport. This repository maintains its product experience, issues,
releases, and development direction independently while periodically merging
appropriate upstream improvements.

The original MIT license and FoloToy copyright notice are retained. See
[LICENSE](LICENSE) and the [upstream project](https://github.com/folotoy/ai-passport).

## Development

See the [project documentation](docs/README.md) for hardware capabilities and
the [build and test guide](docs/development/engineering/build-and-test.md) for
the ESP-IDF 5.5.3 workflow.
