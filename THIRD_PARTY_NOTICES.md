# Third-party notices

## Upstream application

This software is derived from [A3Boy/codex-voice-input](https://github.com/A3Boy/codex-voice-input), MIT, copyright (c) 2026 A3Boy and Codex Voice Input contributors. The original copyright and full MIT license are retained in LICENSE. Robot integration and public packaging changes are contributed by Carti111 and contributors.

The upstream project referenced [Wangnov/codex-asr](https://github.com/Wangnov/codex-asr), MIT, in its authentication/transcription research. The current robot-only desktop build does **not** read Codex authentication files or perform that transcription flow.

## Runtime and compatibility dependencies

- Microsoft .NET / ASP.NET Core / Windows App SDK / Windows SDK build tools and System.Drawing.Common retain their Microsoft and .NET Foundation license terms. See package metadata and the license files included by their distributions. SDK build tools are build dependencies, not a separate ASR service.
- BouncyCastle.Cryptography 2.7.0: Bouncy Castle MIT-style license; licenses/BouncyCastle-LICENSE.txt. Used for official Security2 SRP arithmetic.
- Google.Protobuf 3.36.2: BSD-3-Clause; licenses/Google-Protobuf-LICENSE.txt. Used for official Security2 messages.
- System.IO.Ports 8.0.0: MIT, Microsoft / .NET contributors; licenses/System-IO-Ports-LICENSE.txt. Retained maintenance serial implementation; desktop daily transport is Wi-Fi.
- ModelContextProtocol.AspNetCore / ModelContextProtocol / ModelContextProtocol.Core 2.3.1: Apache-2.0, Model Context Protocol a Series of LF Projects, LLC; licenses/MCP-CSharp-SDK-LICENSE.txt. Retained legacy compatibility classes/tests; current desktop does not start an MCP server or cloud endpoint connection.

Pairing follows [ESP-IDF Protocomm Security2](https://github.com/espressif/esp-idf/tree/v5.5.6/components/protocomm) and official esp_prov session/sec2 protobuf and patch-1 nonce behavior. This repository contains the Windows compatible client, not a full ESP-IDF distribution or robot firmware. Implementations on other robots should retain the licenses of the libraries they use.

## Design and installer

- Design reference: [VoltAgent/awesome-design-md](https://github.com/VoltAgent/awesome-design-md), apple DESIGN.md, MIT (copyright 2026 VoltAgent); licenses/Awesome-Design-MD-LICENSE.txt. The app adapts those references to Windows Acrylic; it contains no Apple proprietary UI assets.
- Optional installer: Inno Setup, licenses/Inno-Setup-LICENSE.txt. installer/languages/ChineseSimplified.isl retains its original notices. Inno Setup compiler is installed separately, not embedded in this source tree.
- Application icon: generated using image generation and selected for this project; the repository includes the app PNG/ICO build resources.

Robot to Codex is independent and is not affiliated with, endorsed by, or sponsored by OpenAI, Espressif, Apple or any robot vendor.
