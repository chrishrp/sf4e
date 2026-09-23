#pragma once

#include <string>
#include <cstdint>

namespace sf4e {
	// Ask the player's router to open the game port, so a direct connection can
	// be made without anyone opening a router admin page.
	//
	// Hole punching only works when both ends can receive an unsolicited packet
	// on the port they advertised. Plenty of home routers will not do that on
	// their own but WILL do it on request, over UPnP, which most of them ship
	// with switched on. Every player we lose to the relay for that reason is one
	// a single SOAP call could have kept on a direct path.
	//
	// Entirely best-effort. A router that has no UPnP, or refuses, costs one
	// discovery timeout and the match proceeds on the relay exactly as before.
	class Upnp {
	public:
		// Find an Internet Gateway Device on the LAN and remember its control
		// URL. Safe to call repeatedly; the result is cached for the session.
		// Returns false when no gateway answers.
		bool Discover(int timeoutMs = 1200);

		// Map udp/<port> on the router to this machine's udp/<port>.
		// Idempotent on every router worth the name: mapping a port that is
		// already mapped to us succeeds.
		bool AddMapping(uint16_t port, const char* description);

		// Give the mapping back. Routers have a finite table and a stale entry
		// pointing at a machine that has gone can block the next player on that
		// network, so this is worth doing rather than relying on the lease.
		bool DeleteMapping(uint16_t port);

		bool Available() const { return _found; }
		const std::string& RouterName() const { return _routerName; }
		const std::string& LocalIp() const { return _localIp; }

	private:
		bool Soap(const std::string& action, const std::string& innerXml);

		bool _found = false;
		bool _searched = false;
		std::string _controlUrl;
		std::string _serviceType;
		std::string _routerName;
		std::string _localIp;
	};
}
