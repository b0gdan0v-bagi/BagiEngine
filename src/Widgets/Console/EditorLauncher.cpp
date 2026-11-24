#include "EditorLauncher.h"

#include <BECore/File/File.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_process.h>

namespace BECore {

    namespace {

        constexpr StringView kFileToken = "{file}";
        constexpr StringView kLineToken = "{line}";
        constexpr StringView kFileUrlPrefix = "file://";
        constexpr StringView kHexDigits = "0123456789ABCDEF";
        constexpr unsigned int kHexShift = 4;
        constexpr unsigned int kHexMask = 0xF;

        bool IsBlank(char c) {
            return c == ' ' || c == '\t';
        }

        bool IsUnreserved(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
        }

        // The words of the template, quotes removed; nullopt on a quote that does not frame a whole word
        Optional<Vector<String>> SplitTemplate(StringView text) {
            Vector<String> words;
            size_t i = 0;
            while (i < text.size()) {
                if (IsBlank(text[i])) {
                    ++i;
                    continue;
                }
                String word;
                if (text[i] == '"') {
                    const size_t close = text.find('"', i + 1);
                    if (close == StringView::npos) {
                        return std::nullopt;
                    }
                    word.assign(text.data() + i + 1, close - i - 1);
                    i = close + 1;
                    if (i < text.size() && !IsBlank(text[i])) {
                        return std::nullopt;
                    }
                } else {
                    while (i < text.size() && !IsBlank(text[i])) {
                        if (text[i] == '"') {
                            return std::nullopt;
                        }
                        word.push_back(text[i]);
                        ++i;
                    }
                }
                words.push_back(std::move(word));
            }
            return words;
        }

        // @p word with the tokens replaced in one pass: the text put in is not looked at again
        String Substitute(const String& word, StringView file, StringView lineText) {
            String result;
            const StringView view(word.data(), word.size());
            size_t i = 0;
            while (i < view.size()) {
                if (view.substr(i, kFileToken.size()) == kFileToken) {
                    result.append(file.data(), file.size());
                    i += kFileToken.size();
                } else if (view.substr(i, kLineToken.size()) == kLineToken) {
                    result.append(lineText.data(), lineText.size());
                    i += kLineToken.size();
                } else {
                    result.push_back(view[i]);
                    ++i;
                }
            }
            return result;
        }

    }  // namespace

    Optional<Vector<String>> BuildEditorLaunch(StringView launchTemplate, StringView file, uint32_t line) {
        Optional<Vector<String>> words = SplitTemplate(launchTemplate);
        if (!words || words->empty()) {
            return std::nullopt;
        }
        const String lineText = Format("{}", line);
        const StringView lineView(lineText.data(), lineText.size());
        Vector<String> arguments;
        arguments.reserve(words->size());
        for (const String& word : *words) {
            arguments.push_back(Substitute(word, file, lineView));
        }
        return arguments;
    }

    String FileUrlOf(StringView path) {
        String url(kFileUrlPrefix.data(), kFileUrlPrefix.size());
        // A path with a drive letter has no leading slash and needs the third one of `file:///`; a POSIX path brings its own
        if (path.empty() || (path.front() != '/' && path.front() != '\\')) {
            url.push_back('/');
        }
        for (const char c : path) {
            const char mapped = c == '\\' ? '/' : c;
            if (IsUnreserved(mapped) || mapped == '/' || mapped == ':') {
                url.push_back(mapped);
                continue;
            }
            const unsigned int byte = static_cast<unsigned char>(mapped);
            url.push_back('%');
            url.push_back(kHexDigits[(byte >> kHexShift) & kHexMask]);
            url.push_back(kHexDigits[byte & kHexMask]);
        }
        return url;
    }

    bool IsSiteOpenable(const char* file) {
        if (file == nullptr) {
            return false;
        }
        static HashMap<const char*, bool> answers;
        if (const auto found = answers.find(file); found != answers.end()) {
            return found->second;
        }
        const Path path(file);
        const bool openable = path.IsAbsolute() && GetFileInfo(path).has_value();
        answers.emplace(file, openable);
        return openable;
    }

    bool LaunchEditor(StringView launchTemplate, const char* file, uint32_t line, String& error) {
        error.clear();
        if (file == nullptr) {
            error = "There is no file";
            return false;
        }
        const StringView fileView(file);
        if (const Optional<Vector<String>> arguments = BuildEditorLaunch(launchTemplate, fileView, line)) {
            Vector<const char*> argv;
            argv.reserve(arguments->size() + 1);
            for (const String& argument : *arguments) {
                argv.push_back(argument.c_str());
            }
            argv.push_back(nullptr);
            SDL_Process* process = SDL_CreateProcess(argv.data(), false);
            if (process == nullptr) {
                error = SDL_GetError();
                return false;
            }
            // Destroying the handle does not stop the child; the editor outlives the call
            SDL_DestroyProcess(process);
            return true;
        }
        const String url = FileUrlOf(fileView);
        if (!SDL_OpenURL(url.c_str())) {
            error = SDL_GetError();
            return false;
        }
        return true;
    }

}  // namespace BECore
