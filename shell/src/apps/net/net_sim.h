// Canned output for the network tools, used on the PC simulator and the
// headless CI build (there is no real network there). Each function returns
// lines in the same shape as the real program, so the apps parse sim and real
// output with the same code.
#pragma once

#include <string>
#include <vector>

namespace zs::net::sim {

std::vector<std::string> arp_scan();                       // arp-scan -lgq
std::vector<std::string> nmap_ports(const std::string& host);  // nmap normal
std::vector<std::string> mtr(const std::string& host);     // mtr -n -r
std::vector<std::string> dig(const std::string& name, const std::string& type);
std::vector<std::string> tcpdump();                        // tcpdump -l -n
std::vector<std::string> ip_addr();                        // ip -br addr
std::vector<std::string> ip_route();                       // ip route
std::vector<std::string> ss_listen();                      // ss -tulnH
std::vector<std::string> resolv();                         // /etc/resolv.conf

}  // namespace zs::net::sim
