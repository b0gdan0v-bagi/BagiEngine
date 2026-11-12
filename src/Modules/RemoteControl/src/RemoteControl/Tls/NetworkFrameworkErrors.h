#pragma once

namespace BECore {

    /// Which list an error code of the macOS backend belongs to (the domains of `nw_error_t`)
    CORE_ENUM(NetworkErrorDomain, uint8_t, Posix, Dns, Tls, Other)

    /**
     * @brief The words for the person for a failure of the macOS TLS backend (Network.framework): "certificate is not trusted",
     * "host name does not match the certificate", "connection refused", ...
     *
     * The codes themselves (`errSSL*`, `errSec*`, errno) go to the log, never into this text. Pure, so a test can check it; it is
     * defined in NetworkFrameworkTlsStream.cpp and exists on macOS only. The result is a string literal.
     */
    [[nodiscard]] eastl::string_view DescribeNetworkFailure(NetworkErrorDomain domain, int code);

}  // namespace BECore
