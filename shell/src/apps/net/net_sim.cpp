#include "apps/net/net_sim.h"

namespace zs::net::sim {

// A small home network: the router, the Cardputer, and a handful of the
// devices you'd actually find on a LAN.
std::vector<std::string> arp_scan() {
    return {
        "192.168.1.1\t3c:37:86:1a:2b:3c\tNETGEAR",
        "192.168.1.10\tb8:27:eb:4d:5e:6f\tRaspberry Pi Foundation",
        "192.168.1.23\t1c:69:7a:aa:bb:cc\tELITEGROUP COMPUTER SYSTEMS",
        "192.168.1.31\tf0:18:98:12:34:56\tApple, Inc.",
        "192.168.1.44\t44:07:0b:98:76:54\tGoogle, Inc.",
        "192.168.1.52\td8:3a:dd:0e:1f:2a\tRaspberry Pi Trading Ltd",
        "192.168.1.77\t50:c7:bf:11:22:33\tTP-LINK TECHNOLOGIES",
        "192.168.1.91\t00:17:88:aa:19:28\tPhilips Lighting BV",
    };
}

std::vector<std::string> nmap_ports(const std::string& host) {
    return {
        "Starting Nmap 7.94 ( https://nmap.org )",
        "Nmap scan report for " + host,
        "Host is up (0.0021s latency).",
        "PORT     STATE SERVICE     VERSION",
        "22/tcp   open  ssh         OpenSSH 9.2p1 Debian",
        "53/tcp   open  domain      dnsmasq 2.89",
        "80/tcp   open  http        nginx 1.24.0",
        "443/tcp  open  ssl/http    nginx 1.24.0",
        "5000/tcp open  upnp        MiniUPnP 2.1",
        "8080/tcp open  http-proxy",
        "Nmap done: 1 IP address (1 host up) scanned in 6.42 seconds",
    };
}

std::vector<std::string> mtr(const std::string& host) {
    return {
        "Start: 2026-11-22T12:00:00+0000",
        "HOST: cardputer                   Loss%   Snt   Last   Avg  Best  Wrst StDev",
        "  1.|-- 192.168.1.1               0.0%     5    1.3   1.4   1.1   2.0   0.3",
        "  2.|-- 10.64.0.1                 0.0%     5    8.9   9.4   8.1  11.2   1.1",
        "  3.|-- 100.72.14.1               0.0%     5   11.4  12.0  10.8  14.1   1.3",
        "  4.|-- 142.250.46.1              0.0%     5   18.7  19.2  17.9  21.0   1.2",
        "  5.|-- " + host + "             0.0%     5   19.1  19.8  18.4  22.3   1.5",
    };
}

std::vector<std::string> dig(const std::string& name, const std::string& type) {
    if (type == "MX")
        return {name + ".\t\t300\tIN\tMX\t10 mail1." + name + ".",
                name + ".\t\t300\tIN\tMX\t20 mail2." + name + "."};
    if (type == "AAAA")
        return {name + ".\t\t300\tIN\tAAAA\t2606:2800:220:1:248:1893:25c8:1946"};
    if (type == "TXT")
        return {name + ".\t\t300\tIN\tTXT\t\"v=spf1 include:_spf." + name + " ~all\""};
    if (type == "NS")
        return {name + ".\t\t172800\tIN\tNS\tns1." + name + ".",
                name + ".\t\t172800\tIN\tNS\tns2." + name + "."};
    return {name + ".\t\t300\tIN\tA\t93.184.216.34"};
}

std::vector<std::string> tcpdump() {
    return {
        "12:00:01.102 IP 192.168.1.42.54211 > 192.168.1.1.53: 7+ A? example.com. (29)",
        "12:00:01.118 IP 192.168.1.1.53 > 192.168.1.42.54211: 7 1/0/0 A 93.184.216.34 (45)",
        "12:00:01.121 IP 192.168.1.42.44120 > 93.184.216.34.443: Flags [S], seq 10, win 64240",
        "12:00:01.140 IP 93.184.216.34.443 > 192.168.1.42.44120: Flags [S.], seq 88, ack 11",
        "12:00:01.141 IP 192.168.1.42.44120 > 93.184.216.34.443: Flags [.], ack 1, win 502",
        "12:00:01.203 ARP, Request who-has 192.168.1.23 tell 192.168.1.1, length 28",
        "12:00:01.204 ARP, Reply 192.168.1.23 is-at 1c:69:7a:aa:bb:cc, length 46",
        "12:00:01.310 IP 192.168.1.44.5353 > 224.0.0.251.5353: 0 PTR (QM)? _airplay._tcp.local.",
        "12:00:01.515 IP6 fe80::1.546 > ff02::1:2.547: UDP, length 56",
        "12:00:01.820 IP 192.168.1.42.44120 > 93.184.216.34.443: Flags [P.], length 517",
    };
}

std::vector<std::string> ip_addr() {
    return {
        "lo               UNKNOWN        127.0.0.1/8 ::1/128",
        "eth0             UP             192.168.1.42/24 fe80::aabb:ccdd:eeff:1122/64",
        "wlan0            UP             192.168.1.58/24 fe80::1234:5678:9abc:def0/64",
        "usb0             DOWN           ",
    };
}

std::vector<std::string> ip_route() {
    return {
        "default via 192.168.1.1 dev eth0 proto dhcp src 192.168.1.42 metric 100",
        "default via 192.168.1.1 dev wlan0 proto dhcp src 192.168.1.58 metric 600",
        "192.168.1.0/24 dev eth0 proto kernel scope link src 192.168.1.42",
        "192.168.1.0/24 dev wlan0 proto kernel scope link src 192.168.1.58",
    };
}

std::vector<std::string> ss_listen() {
    return {
        "tcp   LISTEN 0 128  0.0.0.0:22    0.0.0.0:*",
        "tcp   LISTEN 0 128  0.0.0.0:5355  0.0.0.0:*",
        "tcp   LISTEN 0 4096 127.0.0.1:631 0.0.0.0:*",
        "udp   UNCONN 0 0    0.0.0.0:68    0.0.0.0:*",
        "udp   UNCONN 0 0    0.0.0.0:5353  0.0.0.0:*",
        "udp   UNCONN 0 0    0.0.0.0:5355  0.0.0.0:*",
    };
}

std::vector<std::string> resolv() {
    return {"nameserver 192.168.1.1", "nameserver 1.1.1.1"};
}

}  // namespace zs::net::sim
