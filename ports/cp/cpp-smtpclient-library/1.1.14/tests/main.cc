// Exercises the C++ interface without a network: address parsing, message
// construction, an attachment's MIME type and base64 encoding of a file,
// credentials, and clients that are never connected.  <windows.h> comes
// first, as in a consumer that already includes it, so its min/max macros
// meet the library's headers.
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "cpp/attachment.hpp"
#include "cpp/credential.hpp"
#include "cpp/forcedsecuresmtpclient.hpp"
#include "cpp/htmlmessage.hpp"
#include "cpp/messageaddress.hpp"
#include "cpp/opportunisticsecuresmtpclient.hpp"
#include "cpp/plaintextmessage.hpp"
#include "cpp/smtpclient.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jed_utils::cpp;

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

bool address_rejected(const std::string &address) {
  try {
    MessageAddress parsed(address);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

template <class F>
bool throws_invalid_argument(F f) {
  try {
    f();
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

std::string temp_file(const std::string &suffix) {
#ifdef _WIN32
  char dir[MAX_PATH + 1];
  const DWORD n = GetTempPathA(MAX_PATH + 1, dir);
  return std::string(dir, n) + "cpp-smtpclient-" +
         std::to_string(GetCurrentProcessId()) + suffix;
#else
  const char *dir = std::getenv("TMPDIR");
  return std::string(dir && *dir ? dir : "/tmp") + "/cpp-smtpclient-" +
         std::to_string(getpid()) + suffix;
#endif
}

void addresses() {
  const MessageAddress from("First.Last+tag@Mail.Example.COM", "First Last");
  check(from.getEmailAddress() == "First.Last+tag@Mail.Example.COM", "email address");
  check(from.getDisplayName() == "First Last", "display name");
  check(from.getDomainName() == "Mail.Example.COM", "domain name");
  check(static_cast<std::string>(from) == "First Last <First.Last+tag@Mail.Example.COM>",
        "address with display name");
  const MessageAddress bare("to@example.org");
  check(bare.getDisplayName().empty() && static_cast<std::string>(bare) == "to@example.org",
        "address without display name");

  const char *const rejected[] = {"", "   ", "not an address", "user@", "@example.com",
                                  "user@@example.com", "user@exa_mple.com"};
  for (const char *address : rejected) {
    check(address_rejected(address), address);
  }
}

void messages() {
  const MessageAddress from("sender@example.com", "Sender");
  const std::vector<MessageAddress> to = {MessageAddress("to@example.org", "To")};
  const std::vector<MessageAddress> cc = {MessageAddress("cc1@example.org"),
                                          MessageAddress("cc2@example.org")};
  const std::vector<MessageAddress> bcc = {MessageAddress("bcc@example.org")};
  const std::vector<Attachment> files = {Attachment("report.pdf", "Report")};

  const PlaintextMessage text(from, to, "Subject", "Hello\nBody", cc, bcc, files);
  check(text.getMimeType() == "text/plain", "plaintext MIME type");
  check(text.getFrom().getEmailAddress() == "sender@example.com", "plaintext from");
  check(text.getSubject() == "Subject" && text.getBody() == "Hello\nBody", "plaintext subject and body");
  check(text.getToCount() == 1 && text.getTo()[0].getDisplayName() == "To", "plaintext to");
  check(text.getCcCount() == 2 && text.getCc()[1].getEmailAddress() == "cc2@example.org", "plaintext cc");
  check(text.getBccCount() == 1 && text.getBcc()[0].getEmailAddress() == "bcc@example.org", "plaintext bcc");
  check(text.getAttachmentsCount() == 1 && text.getAttachments()[0].getName() == "Report",
        "plaintext attachments");

  const HTMLMessage html(from, to, "Html", "<p>Hi</p>");
  check(html.getMimeType() == "text/html", "HTML MIME type");
  check(html.getBody() == "<p>Hi</p>" && html.getCcCount() == 0 && html.getAttachmentsCount() == 0,
        "HTML body and defaults");

  check(throws_invalid_argument([&] { PlaintextMessage(from, {}, "Subject", "Body"); }),
        "message without recipients");
}

void attachments() {
  check(Attachment("photo.PNG").getMimeType() == "image/png", "png MIME type");
  check(Attachment("notes.txt").getMimeType() == "text/plain", "txt MIME type");
  check(Attachment("sheet.xlsx").getMimeType() ==
            "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        "xlsx MIME type");
  check(Attachment("data.unknownext").getMimeType().empty(), "unknown extension");
  check(throws_invalid_argument([] { Attachment(" "); }), "blank attachment filename");

  Attachment named("image.gif", "Image", "cid-1");
  check(named.getFilename() == "image.gif" && named.getName() == "Image" &&
            named.getContentId() == "cid-1",
        "attachment fields");
  named.setContentId("cid-2");
  check(named.getContentId() == "cid-2", "attachment content ID");

  // 58 bytes: 80 base64 characters with padding, wrapped at 76 (RFC 5322).
  static const char bytes[] = "\xfb\xff\xbf\x00SMTP attachment bytes for the cabin consumer test!!!!!";
  const std::string path = temp_file(".png");
  {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes, sizeof bytes - 1);
    check(static_cast<bool>(out), "write the attachment file");
  }
  const Attachment file(path, "bytes.png");
  check(file.getMimeType() == "image/png", "file MIME type");
  check(file.getBase64EncodedFile() ==
            "+/+/AFNNVFAgYXR0YWNobWVudCBieXRlcyBmb3IgdGhlIGNhYmluIGNvbnN1bWVyIHRlc3QhISEh\r\n"
            "IQ==\r\n",
        "base64 encoding");
  std::remove(path.c_str());
  check(file.getBase64EncodedFile().empty(), "missing file encodes to nothing");
}

void credentials() {
  Credential login("user@example.com", "secret");
  check(login.getUsername() == "user@example.com" && login.getPassword() == "secret",
        "credential fields");
  check(login.getRecommendedAuthOption() == jed_utils::kImplicit, "default auth method");
  login.setPassword("changed");
  login.setRecommendedAuthOption(jed_utils::kXOauth2);
  check(login.getPassword() == "changed" && login.getRecommendedAuthOption() == jed_utils::kXOauth2,
        "credential setters");
  check(Credential("u", "p", jed_utils::kLogin).getRecommendedAuthOption() == jed_utils::kLogin,
        "auth method in constructor");
  check(throws_invalid_argument([] { Credential(" ", "p"); }), "blank username");
  check(throws_invalid_argument([] { Credential("u", ""); }), "empty password");
}

void clients() {
  OpportunisticSecureSMTPClient tls("smtp.example.com", 587);
  check(tls.getServerName() == "smtp.example.com" && tls.getServerPort() == 587,
        "client server");
  check(tls.getEhloDomain() == "localhost" && tls.getCommandTimeout() == 5 && !tls.getBatchMode(),
        "client defaults");
  check(tls.getCredentials() == nullptr && !tls.getAcceptSelfSignedCert(), "client TLS defaults");
  tls.setCredentials(Credential("user@example.com", "secret"));
  tls.setAcceptSelfSignedCert(true);
  check(tls.getCredentials() != nullptr && tls.getCredentials()->getUsername() == "user@example.com" &&
            tls.getAcceptSelfSignedCert(),
        "client setters");
  check(OpportunisticSecureSMTPClient::getErrorMessage(-2) == "Unable to connect to the socket",
        "socket error message");
  check(OpportunisticSecureSMTPClient::getErrorMessage(-51) == "Unable to initialize the SSL Context",
        "TLS error message");

  const ForcedSecureSMTPClient forced("smtp.example.com", 465);
  check(forced.getServerPort() == 465, "forced TLS client");
  const SmtpClient plain("relay.example.com", 25);
  check(plain.getServerName() == "relay.example.com" && plain.getServerPort() == 25, "plain client");
  check(throws_invalid_argument([] { SmtpClient(" ", 25); }), "blank server name");
}

}  // namespace

int main() {
  addresses();
  messages();
  attachments();
  credentials();
  clients();
  if (failures != 0) {
    std::fprintf(stderr, "%d checks failed\n", failures);
    return 1;
  }
  std::puts("cpp-smtpclient-library: all checks passed");
  return 0;
}
