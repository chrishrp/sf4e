#include <string>
#include <cstring>
#include <cstdio>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <spdlog/spdlog.h>

#include "sf4e__Upnp.hxx"

using sf4e::Upnp;

namespace {
	// Parse "http://192.168.1.1:5000/rootDesc.xml" into its parts. Routers are
	// inconsistent about ports and paths, so nothing here may be assumed.
	bool SplitUrl(const std::string& url, std::string& host, uint16_t& port, std::string& path) {
		const std::string prefix = "http://";
		if (url.compare(0, prefix.size(), prefix) != 0) {
			return false;
		}
		size_t start = prefix.size();
		size_t slash = url.find('/', start);
		std::string hostport = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		path = (slash == std::string::npos) ? "/" : url.substr(slash);

		size_t colon = hostport.rfind(':');
		if (colon == std::string::npos) {
			host = hostport;
			port = 80;
		}
		else {
			host = hostport.substr(0, colon);
			port = (uint16_t)atoi(hostport.c_str() + colon + 1);
			if (port == 0) {
				port = 80;
			}
		}
		return !host.empty();
	}

	// One blocking HTTP request. Small on purpose: this talks to a device on the
	// LAN, a handful of times per lobby, and pulling in a whole HTTP stack for
	// that would be a poor trade.
	bool HttpRequest(const std::string& host, uint16_t port, const std::string& request, std::string& response) {
		response.clear();

		addrinfo hints = { 0 };
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		char portStr[8];
		snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);

		addrinfo* result = nullptr;
		if (getaddrinfo(host.c_str(), portStr, &hints, &result) != 0 || result == nullptr) {
			return false;
		}

		SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (s == INVALID_SOCKET) {
			freeaddrinfo(result);
			return false;
		}

		DWORD timeout = 4000;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
		setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));

		bool ok = false;
		if (connect(s, result->ai_addr, (int)result->ai_addrlen) == 0) {
			if (send(s, request.c_str(), (int)request.size(), 0) == (int)request.size()) {
				char buf[2048];
				int n;
				while ((n = recv(s, buf, sizeof(buf), 0)) > 0) {
					response.append(buf, n);
					// Device descriptions run to a few KB; anything larger is a
					// router misbehaving and not worth reading further.
					if (response.size() > 128 * 1024) {
						break;
					}
				}
				ok = !response.empty();
			}
		}

		closesocket(s);
		freeaddrinfo(result);
		return ok;
	}

	// Case-insensitive header lookup: SSDP replies vary in capitalisation and a
	// case-sensitive match silently loses the LOCATION on some routers.
	std::string HeaderValue(const std::string& text, const char* name) {
		std::string lower;
		lower.reserve(text.size());
		for (char c : text) {
			lower.push_back((char)tolower((unsigned char)c));
		}
		std::string key = name;
		for (char& c : key) {
			c = (char)tolower((unsigned char)c);
		}
		size_t at = lower.find(key);
		if (at == std::string::npos) {
			return std::string();
		}
		at += key.size();
		while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
			at++;
		}
		size_t end = text.find_first_of("\r\n", at);
		return text.substr(at, end == std::string::npos ? std::string::npos : end - at);
	}

	// Pull the text of the first <tag>...</tag>.
	std::string TagValue(const std::string& xml, const char* tag, size_t from = 0) {
		std::string open = std::string("<") + tag + ">";
		std::string close = std::string("</") + tag + ">";
		size_t a = xml.find(open, from);
		if (a == std::string::npos) {
			return std::string();
		}
		a += open.size();
		size_t b = xml.find(close, a);
		if (b == std::string::npos) {
			return std::string();
		}
		return xml.substr(a, b - a);
	}

	// The LAN address of the interface that actually reaches the internet.
	// Not simply "the first adapter": a machine with a VPN, Hyper-V or a second
	// NIC has several, and mapping a port to the wrong one produces a router
	// entry that silently points nowhere.
	std::string LocalAddressTowards(const std::string& routerHost) {
		std::string out;
		SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (s == INVALID_SOCKET) {
			return out;
		}
		sockaddr_in to = { 0 };
		to.sin_family = AF_INET;
		to.sin_port = htons(9);
		if (inet_pton(AF_INET, routerHost.c_str(), &to.sin_addr) == 1) {
			// A UDP connect performs no traffic; it just binds the socket to the
			// route the kernel would use, which is exactly the question.
			if (connect(s, (sockaddr*)&to, sizeof(to)) == 0) {
				sockaddr_in local = { 0 };
				int len = sizeof(local);
				if (getsockname(s, (sockaddr*)&local, &len) == 0) {
					char ip[INET_ADDRSTRLEN] = { 0 };
					inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
					out = ip;
				}
			}
		}
		closesocket(s);
		return out;
	}
}

bool Upnp::Discover(int timeoutMs) {
	if (_searched) {
		return _found;
	}
	_searched = true;

	SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s == INVALID_SOCKET) {
		return false;
	}

	// Leave by the interface that reaches the internet, and bind to it.
	// A PC with a VPN, Hyper-V or a second NIC has several, and an unbound
	// socket can send the search out of one the router is not on -- which looks
	// exactly like "this router has no UPnP".
	std::string outIp = LocalAddressTowards("239.255.255.250");
	if (!outIp.empty()) {
		sockaddr_in bindAddr = { 0 };
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_addr.s_addr = inet_addr(outIp.c_str());
		bindAddr.sin_port = 0;
		bind(s, (sockaddr*)&bindAddr, sizeof(bindAddr));

		in_addr iface = { 0 };
		iface.s_addr = inet_addr(outIp.c_str());
		setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char*)&iface, sizeof(iface));
	}

	DWORD rcv = 300;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcv, sizeof(rcv));
	int ttl = 2;
	setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char*)&ttl, sizeof(ttl));

	sockaddr_in mcast = { 0 };
	mcast.sin_family = AF_INET;
	mcast.sin_port = htons(1900);
	inet_pton(AF_INET, "239.255.255.250", &mcast.sin_addr);

	// Ask for both service types, plus the gateway device itself. A fibre or
	// DSL router usually answers WANPPPConnection and a cable one
	// WANIPConnection; searching for only one misses half the routers alive.
	const char* searchTargets[3] = {
		"urn:schemas-upnp-org:service:WANIPConnection:1",
		"urn:schemas-upnp-org:service:WANPPPConnection:1",
		"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
	};

	// Send every search FIRST, then listen once.
	//
	// MX is the most a router may delay its reply, so listening for less than MX
	// throws away answers that were on their way -- which is what an MX of 2
	// paired with a 1.2 s wait was doing: the search worked and the reply was
	// discarded. Send all three, then wait comfortably longer than MX.
	for (int t = 0; t < 3; t++) {
		char request[512];
		int len = snprintf(request, sizeof(request),
			"M-SEARCH * HTTP/1.1\r\n"
			"HOST: 239.255.255.250:1900\r\n"
			"MAN: \"ssdp:discover\"\r\n"
			"MX: 1\r\n"
			"ST: %s\r\n"
			"\r\n", searchTargets[t]);
		sendto(s, request, len, 0, (sockaddr*)&mcast, sizeof(mcast));
	}

	std::string location;
	int waitMs = timeoutMs < 2000 ? 2000 : timeoutMs;
	DWORD deadline = GetTickCount() + (DWORD)waitMs;
	while (location.empty() && (int)(GetTickCount() - deadline) < 0) {
		char buf[2048];
		sockaddr_in from = { 0 };
		int fromLen = sizeof(from);
		int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromLen);
		if (n <= 0) {
			// A timeout on one read is normal; keep waiting until the deadline
			// rather than giving up on the first quiet moment.
			continue;
		}
		buf[n] = 0;
		std::string reply(buf, n);
		location = HeaderValue(reply, "location:");
	}

	closesocket(s);

	if (location.empty()) {
		spdlog::info("UPnP: no router answered the search; the game port cannot be opened "
			"automatically. Either UPnP is off in the router's settings, or Windows Firewall "
			"is dropping the reply. A direct connection may still work, and the relay always does.");
		return false;
	}

	std::string host, path;
	uint16_t port = 0;
	if (!SplitUrl(location, host, port, path)) {
		return false;
	}

	char req[1024];
	snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\nHost: %s:%u\r\nConnection: close\r\nUser-Agent: sf4e\r\n\r\n",
		path.c_str(), host.c_str(), (unsigned)port);

	std::string description;
	if (!HttpRequest(host, port, req, description)) {
		spdlog::info("UPnP: a router answered but its description could not be read.");
		return false;
	}

	// Find the WAN connection service and its control URL. The description nests
	// several services; take the first WANIP/WANPPP one and the controlURL that
	// follows it, rather than the document's first controlURL, which belongs to
	// some other service entirely.
	size_t at = description.find("WANIPConnection:1");
	if (at == std::string::npos) {
		at = description.find("WANPPPConnection:1");
	}
	if (at == std::string::npos) {
		spdlog::info("UPnP: the router has no WAN connection service to ask.");
		return false;
	}
	size_t serviceStart = description.rfind("<service>", at);
	_serviceType = TagValue(description, "serviceType", serviceStart == std::string::npos ? 0 : serviceStart);
	std::string control = TagValue(description, "controlURL", serviceStart == std::string::npos ? at : serviceStart);
	if (_serviceType.empty() || control.empty()) {
		return false;
	}

	if (control.compare(0, 7, "http://") == 0) {
		_controlUrl = control;
	}
	else {
		char full[512];
		snprintf(full, sizeof(full), "http://%s:%u%s%s",
			host.c_str(), (unsigned)port, control[0] == '/' ? "" : "/", control.c_str());
		_controlUrl = full;
	}

	_routerName = TagValue(description, "friendlyName");
	_localIp = LocalAddressTowards(host);
	if (_localIp.empty()) {
		spdlog::info("UPnP: found a router but could not tell which of this PC's addresses "
			"faces it; not mapping anything rather than mapping the wrong one.");
		return false;
	}

	_found = true;
	spdlog::info("UPnP: router '{}' can open ports; this PC is {} on its network",
		_routerName.empty() ? "(unnamed)" : _routerName, _localIp);
	return true;
}

bool Upnp::Soap(const std::string& action, const std::string& innerXml) {
	if (!_found) {
		return false;
	}

	std::string host, path;
	uint16_t port = 0;
	if (!SplitUrl(_controlUrl, host, port, path)) {
		return false;
	}

	std::string body =
		"<?xml version=\"1.0\"?>"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body><u:" + action + " xmlns:u=\"" + _serviceType + "\">" +
		innerXml +
		"</u:" + action + "></s:Body></s:Envelope>";

	char header[1024];
	snprintf(header, sizeof(header),
		"POST %s HTTP/1.1\r\n"
		"Host: %s:%u\r\n"
		"Content-Type: text/xml; charset=\"utf-8\"\r\n"
		"SOAPAction: \"%s#%s\"\r\n"
		"Content-Length: %u\r\n"
		"Connection: close\r\n"
		"\r\n",
		path.c_str(), host.c_str(), (unsigned)port,
		_serviceType.c_str(), action.c_str(), (unsigned)body.size());

	std::string response;
	if (!HttpRequest(host, port, std::string(header) + body, response)) {
		return false;
	}

	// A UPnP fault still comes back as HTTP 500, so the status line is the thing
	// to read, not merely whether bytes arrived.
	return response.find(" 200 ") != std::string::npos;
}

bool Upnp::AddMapping(uint16_t port, const char* description) {
	if (!_found) {
		return false;
	}

	char inner[768];
	snprintf(inner, sizeof(inner),
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>UDP</NewProtocol>"
		"<NewInternalPort>%u</NewInternalPort>"
		"<NewInternalClient>%s</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>%s</NewPortMappingDescription>"
		"<NewLeaseDuration>0</NewLeaseDuration>",
		(unsigned)port, (unsigned)port, _localIp.c_str(),
		description ? description : "sf4e");

	if (Soap("AddPortMapping", inner)) {
		spdlog::info("UPnP: opened udp/{} on the router for this PC", port);
		return true;
	}

	// Some routers refuse a permanent lease and accept a bounded one. Worth the
	// second attempt: the alternative is the relay.
	char leased[768];
	snprintf(leased, sizeof(leased),
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>UDP</NewProtocol>"
		"<NewInternalPort>%u</NewInternalPort>"
		"<NewInternalClient>%s</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>%s</NewPortMappingDescription>"
		"<NewLeaseDuration>3600</NewLeaseDuration>",
		(unsigned)port, (unsigned)port, _localIp.c_str(),
		description ? description : "sf4e");

	if (Soap("AddPortMapping", leased)) {
		spdlog::info("UPnP: opened udp/{} on the router for this PC (one hour lease)", port);
		return true;
	}

	spdlog::info("UPnP: the router refused to open udp/{}. This usually means UPnP is "
		"switched off in its settings, or the port is already mapped to another PC.", port);
	return false;
}

bool Upnp::DeleteMapping(uint16_t port) {
	if (!_found) {
		return false;
	}
	char inner[256];
	snprintf(inner, sizeof(inner),
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>%u</NewExternalPort>"
		"<NewProtocol>UDP</NewProtocol>",
		(unsigned)port);
	return Soap("DeletePortMapping", inner);
}
