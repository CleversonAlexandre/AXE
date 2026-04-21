#include "timestamp.hpp"

#include "fmt/format.h"

#if !defined(_POSIX_THREAD_SAFE_FUNCTIONS)
#   define _POSIX_THREAD_SAFE_FUNCTIONS 1
#endif

#include <ctime>

namespace axe
{
	auto timestamp() -> std::string
	{

	struct timespec ts {};
#if defined(_MSC_VER)
	timespec_get(&ts, TIME_UTC);
#else
		clock_gettime(CLOCK_REALTIME, &ts)
#endif

	struct tm time;
#if defined (_WIN32)
	localtime_s(&time, &ts.tv_sec);
#else
	localtime_s(&ts.tv_sec, &time);
#endif

	return fmt::format(
		"{:04d}{:02d}{:02d} {:02}:{:02}:{:02}.{:03d} ",
		time.tm_year + 1900,
		time.tm_mon + 1,
		time.tm_mday,
		time.tm_hour,
		time.tm_min,
		time.tm_sec,
		ts.tv_nsec / 1000000
	);
}

	auto timestampShort() -> std::string
	{
	struct timespec ts {};
	#if defined(_MSC_VER)
		timespec_get(&ts, TIME_UTC);
	#else
		clock_gettime(CLOCK_REALTIME, &ts)
	#endif

		struct tm time;
	#if defined (_WIN32)
		localtime_s(&time, &ts.tv_sec);
	#else
		localtime_s(&ts.tv_sec, &time);
	#endif

		return fmt::format(
			"{:02}:{:02}:{:02}.{:03d} ",
			time.tm_hour,
			time.tm_min,
			time.tm_sec,
			ts.tv_nsec / 1000000			
		);
	}
}