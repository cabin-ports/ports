#include <ipaddress/ipaddress.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

void TestIpv4Address() {
  using ipaddress::ipv4_address;
  const ipv4_address ip = ipv4_address::parse(std::string("192.168.1.10"));
  check(ip.to_uint() == 0xC0A8010Au, "192.168.1.10 to_uint");
  check(ip.to_string() == "192.168.1.10", "192.168.1.10 to_string");
  check(ip.is_private() && !ip.is_global() && !ip.is_loopback(),
        "192.168.1.10 is private");
  check(ipv4_address::from_uint(ip.to_uint() + 1).to_string() ==
            "192.168.1.11",
        "from_uint round trip");

  const ipv4_address loopback = ipv4_address::parse(std::string("127.0.0.1"));
  check(loopback.to_uint() == 0x7F000001u && loopback.is_loopback(),
        "127.0.0.1 is loopback");

  const ipv4_address dns = ipv4_address::parse(std::string("8.8.8.8"));
  check(dns.is_global() && !dns.is_private() && !dns.is_loopback(),
        "8.8.8.8 is global");
}

void TestIpv6Address() {
  using ipaddress::ipv6_address;
  const ipv6_address ip =
      ipv6_address::parse(std::string("fe80::1ff:fe23:4567:890a%eth2"));
  const ipaddress::uint128_t value = ip.to_uint();
  check(value.upper() == 0xFE80000000000000ull &&
            value.lower() == 0x01FFFE234567890Aull,
        "fe80::1ff:fe23:4567:890a to_uint");
  check(ip.to_string() == "fe80::1ff:fe23:4567:890a%eth2",
        "compressed form keeps the scope id");
  check(ip.to_string(ipaddress::format::full) ==
            "fe80:0000:0000:0000:01ff:fe23:4567:890a%eth2",
        "full form");
  check(ip.get_scope_id().get_string() == "eth2" &&
            !ip.get_scope_id().has_uint32(),
        "named scope id");
  check(ip.is_link_local() && ip.is_private() && !ip.is_loopback(),
        "fe80:: is link-local");

  const ipv6_address numbered = ipv6_address::parse(std::string("fe80::1%3"));
  check(numbered.get_scope_id().has_uint32() &&
            numbered.get_scope_id().get_uint32() == 3u,
        "numeric scope id");

  const ipv6_address loopback = ipv6_address::parse(std::string("::1"));
  check(loopback.to_uint() == 1u && loopback.is_loopback() &&
            loopback.to_string() == "::1",
        "::1 is loopback");

  const ipv6_address ula = ipv6_address::parse(std::string("fd12:3456::1"));
  check(ula.is_private() && !ula.is_global(), "fd12:3456::1 is private");

  const ipv6_address dns =
      ipv6_address::parse(std::string("2606:4700:4700::1111"));
  check(dns.is_global() && !dns.is_private(), "2606:4700:4700::1111 is global");
}

void TestNetworks() {
  using ipaddress::ipv4_address;
  using ipaddress::ipv4_network;
  using ipaddress::ipv6_address;
  using ipaddress::ipv6_network;

  const ipv4_network net = ipv4_network::parse(std::string("192.168.0.0/22"));
  check(net.contains(ipv4_address::parse(std::string("192.168.3.255"))) &&
            !net.contains(ipv4_address::parse(std::string("192.168.4.0"))),
        "/22 containment");
  check(net.addresses_count() == 1024u, "/22 has 1024 addresses");
  check(net.netmask().to_string() == "255.255.252.0" &&
            net.broadcast_address().to_string() == "192.168.3.255",
        "/22 netmask and broadcast");
  check(ipv4_network::parse(std::string("192.168.2.0/24")).subnet_of(net),
        "subnet_of");

  std::vector<std::string> quarters;
  for (const auto& subnet : net.subnets(2)) {
    quarters.push_back(subnet.to_string());
  }
  check(quarters == std::vector<std::string>{"192.168.0.0/24",
                                             "192.168.1.0/24",
                                             "192.168.2.0/24",
                                             "192.168.3.0/24"},
        "subnets(2) of a /22");
  check(net.subnets(1, 26).size() == 16, "16 /26 subnets in a /22");

  const ipv6_network net6 = ipv6_network::parse(std::string("2001:db8::/32"));
  check(net6.contains(ipv6_address::parse(std::string("2001:db8:ffff::1"))) &&
            !net6.contains(ipv6_address::parse(std::string("2001:db9::"))),
        "/32 containment");
  check(net6.subnets(1, 36).size() == 16, "16 /36 subnets in a /32");
  check(net6.subnets(4).back().to_string() == "2001:db8:f000::/36",
        "last /36 subnet");
  check(ipv6_network::parse(std::string("2001:db8::/120")).addresses_count() ==
            256u,
        "/120 has 256 addresses");
}

void TestErrors() {
  using ipaddress::error_code;

  error_code code = error_code::no_error;
  ipaddress::ipv4_address::parse(std::string("192.257.2.0"), code);
  check(code == error_code::octet_exceeded_255, "octet over 255");

  code = error_code::no_error;
  ipaddress::ipv6_address::parse(std::string("1::2::3"), code);
  check(code == error_code::most_one_double_colon_permitted,
        "two double colons");

  code = error_code::no_error;
  ipaddress::ipv4_network::parse(std::string("192.168.1.1/24"), code);
  check(code == error_code::has_host_bits_set, "strict network parse");

  error_code thrown = error_code::no_error;
  try {
    ipaddress::ipv4_address::parse(std::string("1.2.3"));
  } catch (const ipaddress::parse_error& e) {
    thrown = e.code();
  }
  check(thrown == error_code::expected_4_octets, "parse_error carries the code");
}

}  // namespace

int main() {
  TestIpv4Address();
  TestIpv6Address();
  TestNetworks();
  TestErrors();
  if (failures != 0) {
    return 1;
  }
  std::printf("vladimirshaleev-ipaddress consumer test passed\n");
  return 0;
}
