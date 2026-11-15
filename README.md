# LocalAI

A lightweight native desktop chat client for [Ollama](https://ollama.com), written in C++/Qt.
Chats stay fully local, are archived as JSON and can be organized into project folders.

> Hobby project. Not affiliated with Ollama or with the unrelated project
> [mudler/LocalAI](https://github.com/mudler/LocalAI). The user interface is currently in German.

## Features

- Streaming chat with any model installed in Ollama (`http://127.0.0.1:11434`)
- Markdown rendering, syntax-highlighted code blocks with a copy button
- Chat archive: every chat is a JSON file; title and model are stored per chat
- Project folders: create folders, start chats inside them, move existing chats in and out
- Gear menu on every chat and folder: rename, move, remove from folder, delete
- Attachments: text files, images (for vision models) and optionally PDFs; drag & drop supported
- Model manager: download and delete models, grouped by family (Qwen, Llama, Gemma, ...)
- Configurable storage folder for chats (with optional move of existing data)
- Dark theme

## Requirements

- [Ollama](https://ollama.com) running locally
- Qt 6 (Widgets, Network), CMake 3.16+, a C++17 compiler
- Optional: Qt PDF for PDF attachments (not available for MinGW, needs the MSVC kit)

## Build

With Qt Creator: open `CMakeLists.txt`, pick a Qt 6 kit, build and run.

Or from the command line:

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-Qt6>
cmake --build build
```

If Qt PDF is found, PDF attachments are enabled automatically; otherwise they are disabled.

## Usage

1. Start Ollama and make sure at least one model is installed
   (menu `☰` -> "KI-Modelle verwalten" can download one).
2. Click "Neuer Chat", enter a title and pick a model.
3. Attach files with `+` or by dragging them into the window.

Chats and project folders are stored in `%APPDATA%\OllamaUI\chats` by default.
The folder can be changed in the settings (`☰` -> "Einstellungen").

## Status

Early and experimental. Expect rough edges. Issues and pull requests are welcome.

## License

MIT, see [LICENSE](LICENSE). Qt is licensed separately, see [NOTICE.md](NOTICE.md).
