#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace core
{
	inline std::vector<std::string> split(const std::string& s, char delimiter)
	{
		std::vector<std::string> tokens;
		std::string token;
		std::istringstream stream(s);
		while (std::getline(stream, token, delimiter))
			tokens.push_back(token);
		return tokens;
	}

	inline std::vector<std::string> split(const std::string& s, const std::string& delimiter)
	{
		std::vector<std::string> tokens;
		size_t start = 0;
		size_t end;
		while ((end = s.find(delimiter, start)) != std::string::npos)
		{
			tokens.push_back(s.substr(start, end - start));
			start = end + delimiter.size();
		}
		tokens.push_back(s.substr(start));
		return tokens;
	}

	inline std::string toLower(const std::string& s)
	{
		std::string result = s;
		std::transform(result.begin(), result.end(), result.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return result;
	}

	inline bool containsIgnoreCase(const std::string& s, const std::string& substr)
	{
		const auto it = std::search(s.begin(), s.end(), substr.begin(), substr.end(),
			[](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		return it != s.end();
	}

	inline bool startsWithIgnoreCase(const std::string& s, const std::string& prefix)
	{
		if (prefix.size() > s.size()) return false;
		return std::equal(prefix.begin(), prefix.end(), s.begin(),
			[](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
	}

	inline bool endsWithIgnoreCase(const std::string& s, const std::string& suffix)
	{
		if (suffix.size() > s.size()) return false;
		return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
			[](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
	}

	inline int safeStoi(const std::string& s, int fallback = 0)
	{
		if (s.empty()) return fallback;
		try { return std::stoi(s); }
		catch (...) { return fallback; }
	}
}
