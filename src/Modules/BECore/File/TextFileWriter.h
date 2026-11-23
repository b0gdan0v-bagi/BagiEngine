#pragma once

#include <BECore/File/File.h>
#include <BECore/Format/Format.h>
#include <algorithm>

namespace BECore {

    /**
     * Text into a file through an inline buffer of N bytes, for a writer that has no place to return an error per call (a log sink,
     * a recorder). Move-only.
     *
     * - A text that does not fit the free part of the buffer flushes the buffer first; a text longer than N goes straight to the file.
     * - AppendFormat formats into the buffer with FormatToN: a text that does not fit even the empty buffer is cut at N, never
     *   allocated.
     * - The first write error is kept (HasError, TakeError) and every call after it does nothing until TakeError clears it, so the
     *   file may have lost the bytes of the failed write.
     * - Open, Append, AppendFormat and Flush allocate nothing apart from the error text of a failure. The destructor writes the
     *   buffer best effort and closes the file; Close reports what the destructor cannot.
     *
     * The buffer is part of the object: put a large N in a member, not on the stack of a deep call.
     */
    template <size_t N>
    class TextFileWriter {
        static_assert(N > 0, "TextFileWriter: the buffer needs at least one byte");

    public:
        TextFileWriter() noexcept = default;

        ~TextFileWriter() {
            if (_file.IsOpen()) {
                WriteBuffer();
            }
        }

        TextFileWriter(TextFileWriter&& other) noexcept : _file(std::move(other._file)), _used(std::exchange(other._used, size_t{0})), _error(std::move(other._error)) {
            std::copy_n(other._buffer.data(), _used, _buffer.data());
            other._error.reset();
        }

        TextFileWriter& operator=(TextFileWriter&& other) noexcept {
            if (this != &other) {
                if (_file.IsOpen()) {
                    WriteBuffer();
                }
                _file = std::move(other._file);
                _used = std::exchange(other._used, size_t{0});
                _error = std::move(other._error);
                other._error.reset();
                std::copy_n(other._buffer.data(), _used, _buffer.data());
            }
            return *this;
        }

        TextFileWriter(const TextFileWriter&) = delete;
        TextFileWriter& operator=(const TextFileWriter&) = delete;

        /// Opens @p path (a real UTF-8 path, File.h) for FileMode::Write or FileMode::Append; the file open before is flushed and closed first
        [[nodiscard]] std::expected<void, String> Open(StringView path, FileMode mode) {
            ASSERT(mode == FileMode::Write || mode == FileMode::Append, "TextFileWriter::Open: only Write and Append write text");
            auto file = File::Open(path, mode);
            if (!file) {
                return std::unexpected(std::move(file.error()));
            }
            Attach(std::move(*file));
            return {};
        }

        /// Takes over an open file (one that is writable: a write to a file opened for reading is the error HasError keeps)
        void Attach(File&& file) {
            if (_file.IsOpen()) {
                WriteBuffer();
            }
            _file = std::move(file);
            _used = 0;
            _error.reset();
        }

        [[nodiscard]] bool IsOpen() const noexcept {
            return _file.IsOpen();
        }

        void Append(StringView text) {
            if (!CanWrite()) {
                return;
            }
            if (text.size() > N - _used) {
                if (!WriteBuffer()) {
                    return;
                }
                if (text.size() > N) {
                    Record(_file.Write(text));
                    return;
                }
            }
            std::copy_n(text.data(), text.size(), _buffer.data() + _used);
            _used += text.size();
        }

        template <typename... Args>
        void AppendFormat(FormatString<Args...> fmt, Args&&... args) {
            if (!CanWrite()) {
                return;
            }
            const FormatResult fits = FormatToN(_buffer.data() + _used, N - _used, fmt, std::forward<Args>(args)...);
            if (!fits.truncated) {
                _used += fits.size;
                return;
            }
            // The failed attempt wrote past _used only. Formatting reads its arguments without moving from them, so the second
            // pass over an empty buffer (the most room there is) sees them intact.
            if (_used > 0) {
                if (!WriteBuffer()) {
                    return;
                }
                const FormatResult alone = FormatToN(_buffer.data(), N, fmt, std::forward<Args>(args)...);
                _used = std::min(alone.size, N);
            } else {
                _used = N;
            }
        }

        /// Writes the buffer and flushes the file's own buffer, so a reader of the file sees the text
        void Flush() {
            if (!CanWrite()) {
                return;
            }
            if (WriteBuffer()) {
                Record(_file.Flush());
            }
        }

        /// Writes the buffer, closes the file and returns the first error of the writer, which it clears. The file is closed even on an error.
        [[nodiscard]] std::expected<void, String> Close() {
            if (!_file.IsOpen()) {
                return {};
            }
            if (!_error) {
                WriteBuffer();
            }
            _used = 0;
            auto closed = _file.Close();
            if (_error) {
                return std::unexpected(TakeErrorText());
            }
            return closed;
        }

        [[nodiscard]] bool HasError() const noexcept {
            return _error.has_value();
        }

        /// The first write error, once; the writer writes again afterwards
        [[nodiscard]] Optional<String> TakeError() {
            return std::exchange(_error, std::nullopt);
        }

    private:
        [[nodiscard]] bool CanWrite() const noexcept {
            return _file.IsOpen() && !_error.has_value();
        }

        /// False when the writer has an error (the old one or the one this call made)
        bool WriteBuffer() {
            if (_error.has_value()) {
                return false;
            }
            const size_t count = std::exchange(_used, size_t{0});
            if (count == 0) {
                return true;
            }
            return Record(_file.Write(_buffer.data(), count));
        }

        bool Record(std::expected<void, String> result) {
            if (result) {
                return true;
            }
            _error = std::move(result.error());
            return false;
        }

        String TakeErrorText() {
            String text = std::move(*_error);
            _error.reset();
            return text;
        }

        File _file;
        size_t _used = 0;
        Optional<String> _error;
        Array<char, N> _buffer;
    };

}  // namespace BECore
