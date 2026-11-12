#include <RemoteControl/Tls/NetworkFrameworkErrors.h>
#include <cerrno>
#include <gtest/gtest.h>

#ifdef PLATFORM_MACOS

#include <Security/Security.h>

namespace BECore::Tests {

    TEST(NetworkFrameworkErrorsTest, CertificateFailuresAreNamedInWordsForThePlayer) {
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSSLUnknownRootCert), "certificate is not trusted");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSecNotTrusted), "certificate is not trusted");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSecHostNameMismatch), "host name does not match the certificate");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSSLHostNameMismatch), "host name does not match the certificate");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSSLCertExpired), "certificate has expired");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSecCertificateNotValidYet), "certificate is not valid yet");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, errSSLClosedAbort), "the server closed the connection during the handshake");
    }

    TEST(NetworkFrameworkErrorsTest, TheChainErrorAdmitsThatItCoversTheNameToo) {
        // The system's own check reports a wrong host name with this code, so the text must not claim more than it knows
        const eastl::string_view text = DescribeNetworkFailure(NetworkErrorDomain::Tls, errSSLXCertChainInvalid);
        EXPECT_NE(text.find("certificate"), eastl::string_view::npos);
        EXPECT_NE(text.find("host name"), eastl::string_view::npos);
    }

    TEST(NetworkFrameworkErrorsTest, NetworkAndNameErrorsAreNamedInWords) {
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Posix, ECONNREFUSED), "connection refused");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Posix, ETIMEDOUT), "timed out");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Posix, ECONNRESET), "connection reset");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Posix, EHOSTUNREACH), "host unreachable");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Dns, 1), "cannot resolve the host name");
    }

    TEST(NetworkFrameworkErrorsTest, AnUnknownCodeGivesAGenericTextWithoutTheNumber) {
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Tls, -12345), "handshake failed");
        EXPECT_EQ(DescribeNetworkFailure(NetworkErrorDomain::Posix, 9999), "network error");
    }

}  // namespace BECore::Tests

#endif  // PLATFORM_MACOS
