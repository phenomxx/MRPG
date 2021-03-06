#pragma once
#ifndef BOOST_VERSION
#ifndef EXISTENT_BOOST_ASIO
	#define ASIO_STANDALONE
#endif
#define _WEBSOCKETPP_CPP11_STL_
#define _WEBSOCKETPP_CPP11_RANDOM_DEVICE_
#define _WEBSOCKETPP_CPP11_TYPE_TRAITS_
#endif // !BOOST_VERSION

#ifndef NONEXISTENT_WEBSOCKETPP
#include <websocketpp/config/asio_client.hpp>
#endif
