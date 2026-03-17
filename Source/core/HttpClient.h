/*----------------------------------------------------------------------*\
| This file is part of WoW Model Viewer                                  |
|                                                                        |
| WoW Model Viewer is free software: you can redistribute it and/or      |
| modify it under the terms of the GNU General Public License as         |
| published by the Free Software Foundation, either version 3 of the     |
| License, or (at your option) any later version.                        |
|                                                                        |
| WoW Model Viewer is distributed in the hope that it will be useful,    |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with WoW Model Viewer.                                           |
| If not, see <http://www.gnu.org/licenses/>.                            |
\*----------------------------------------------------------------------*/

#pragma once

#include <functional>
#include <string>
#include <vector>

// Synchronous HTTP client using the Windows WinHTTP API.
// Supports HTTPS via the OS certificate store (no OpenSSL required).
namespace HttpClient
{
	// Simple response structure
	struct Response
	{
		int statusCode = 0;
		std::string body;
		std::string error;
		bool success = false;
	};

	// Optional progress callback: (bytesReceived, totalBytes).  totalBytes may be 0 if unknown.
	using ProgressCallback = std::function<void(size_t bytesReceived, size_t totalBytes)>;

	// Perform a synchronous HTTP(S) GET.
	Response Get(const std::string& url, const ProgressCallback& progress = nullptr);
}
