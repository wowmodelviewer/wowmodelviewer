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

#include "HttpClient.h"

#include <Windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

// ---------- helpers ----------

static std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};
	const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring ws(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), ws.data(), len);
	return ws;
}

// Crack a URL into components using WinHttpCrackUrl.
struct UrlParts
{
	std::wstring host;
	std::wstring path;       // includes query string
	INTERNET_PORT port = 0;
	bool isHttps = false;
};

static bool CrackUrl(const std::string& url, UrlParts& out)
{
	const std::wstring wurl = Utf8ToWide(url);

	URL_COMPONENTS uc{};
	uc.dwStructSize = sizeof(uc);
	uc.dwHostNameLength  = static_cast<DWORD>(-1);
	uc.dwUrlPathLength   = static_cast<DWORD>(-1);
	uc.dwExtraInfoLength = static_cast<DWORD>(-1);

	if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc))
		return false;

	out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
	out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.lpszExtraInfo && uc.dwExtraInfoLength)
		out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	out.port = uc.nPort;
	out.isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
	return true;
}

// ---------- public API ----------

HttpClient::Response HttpClient::Get(const std::string& url, const ProgressCallback& progress)
{
	Response resp;

	UrlParts parts;
	if (!CrackUrl(url, parts))
	{
		resp.error = "Failed to parse URL: " + url;
		return resp;
	}

	HINTERNET hSession = WinHttpOpen(L"WoWModelViewer",
		WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);

	if (!hSession)
	{
		resp.error = "WinHttpOpen failed";
		return resp;
	}

	// Enable automatic redirect following
	DWORD optionFlags = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
	WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &optionFlags, sizeof(optionFlags));

	HINTERNET hConnect = WinHttpConnect(hSession, parts.host.c_str(), parts.port, 0);
	if (!hConnect)
	{
		resp.error = "WinHttpConnect failed";
		WinHttpCloseHandle(hSession);
		return resp;
	}

	const DWORD flags = parts.isHttps ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
		parts.path.c_str(), nullptr,
		WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES,
		flags);

	if (!hRequest)
	{
		resp.error = "WinHttpOpenRequest failed";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return resp;
	}

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
	{
		resp.error = "WinHttpSendRequest failed";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return resp;
	}

	if (!WinHttpReceiveResponse(hRequest, nullptr))
	{
		resp.error = "WinHttpReceiveResponse failed";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return resp;
	}

	// Read status code
	DWORD statusCode = 0;
	DWORD statusSize = sizeof(statusCode);
	WinHttpQueryHeaders(hRequest,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
	resp.statusCode = static_cast<int>(statusCode);

	// Read Content-Length (may be 0 if server doesn't provide it)
	size_t totalBytes = 0;
	{
		wchar_t clBuf[32]{};
		DWORD clSize = sizeof(clBuf);
		if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
			WINHTTP_HEADER_NAME_BY_INDEX, clBuf, &clSize, WINHTTP_NO_HEADER_INDEX))
		{
			totalBytes = static_cast<size_t>(_wtoi64(clBuf));
		}
	}

	// Read body
	std::vector<char> buffer;
	size_t received = 0;
	DWORD bytesAvailable = 0;
	while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
	{
		buffer.resize(bytesAvailable);
		DWORD bytesRead = 0;
		if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead))
		{
			resp.body.append(buffer.data(), bytesRead);
			received += bytesRead;
			if (progress)
				progress(received, totalBytes);
		}
	}

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	resp.success = (resp.statusCode >= 200 && resp.statusCode < 300);
	if (!resp.success && resp.error.empty())
		resp.error = "HTTP " + std::to_string(resp.statusCode);

	return resp;
}
